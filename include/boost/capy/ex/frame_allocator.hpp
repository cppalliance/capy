//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_FRAME_ALLOCATOR_HPP
#define BOOST_CAPY_FRAME_ALLOCATOR_HPP

#include <boost/capy/detail/config.hpp>

#include <coroutine>
#include <memory_resource>

/*  Design rationale (pdimov):

    This accessor is a thin wrapper over a thread-local pointer.
    It returns exactly what was stored, including nullptr. No
    dynamic initializer on the thread-local; a dynamic TLS
    initializer moves you into a costlier implementation bucket
    on some platforms - avoid it.

    Null handling is the caller's responsibility (e.g. in
    promise_type::operator new). The accessor must not substitute
    a default, because there are multiple valid choices
    (new_delete_resource, the default pmr resource, etc.). If
    the allocator is not set, it reports "not set" and the
    caller interprets that however it wants.
*/

namespace boost {
namespace capy {

namespace detail {

inline std::pmr::memory_resource*&
current_frame_allocator_ref() noexcept
{
    static thread_local std::pmr::memory_resource* mr = nullptr;
    return mr;
}

} // namespace detail

/** Return the current frame allocator for this thread.

    These accessors exist to implement the allocator
    propagation portion of the @ref IoAwaitable protocol.
    Launcher functions (`run_async`, `run`) set the
    thread-local value before invoking a child coroutine.
    The child's `promise_type::operator new` reads it to
    allocate the coroutine frame from the correct resource.

    The value is only valid during a narrow execution
    window. Between a coroutine's resumption
    and the next suspension point, the protocol guarantees
    that TLS contains the allocator associated with the
    currently running chain. Outside that window the value
    is indeterminate. Only code that implements an
    @ref IoAwaitable should call these functions.

    A return value of `nullptr` means "not specified" -
    no allocator is established for this chain.
    The awaitable is free to use whatever allocation
    strategy makes best sense (e.g.
    `std::pmr::new_delete_resource()`).

    Use of the frame allocator is optional. An awaitable
    that does not consult this value to allocate its
    coroutine frame is never wrong. However, a conforming
    awaitable must still propagate the allocator faithfully
    so that downstream coroutines can use it.

    @return The thread-local memory_resource pointer,
    or `nullptr` if none is set.

    @see set_current_frame_allocator, IoAwaitable
*/
inline
std::pmr::memory_resource*
get_current_frame_allocator() noexcept
{
    return detail::current_frame_allocator_ref();
}

/** Set the current frame allocator for this thread.

    Installs @p mr as the frame allocator read by the
    next coroutine's `promise_type::operator new` on
    this thread. Only launcher functions and
    @ref IoAwaitable machinery should call this; see
    @ref get_current_frame_allocator for the full protocol
    description.

    Passing `nullptr` means "not specified" - no
    particular allocator is established for the chain.

    @param mr The memory_resource to install, or
    `nullptr` to clear.

    @see get_current_frame_allocator, IoAwaitable
*/
inline void
set_current_frame_allocator(
    std::pmr::memory_resource* mr) noexcept
{
    detail::current_frame_allocator_ref() = mr;
}

/** Resume a coroutine handle with frame-allocator TLS protection.

    Saves the current thread-local frame allocator before
    calling `h.resume()`, then restores it after the call
    returns. This prevents a resumed coroutine's
    `await_resume` from permanently overwriting the caller's
    allocator value.

    Between a coroutine's resumption and its next child
    invocation, arbitrary user code may run. If that code
    resumes a coroutine from a different chain on this
    thread, the other coroutine's `await_resume` overwrites
    TLS with its own allocator. Without save/restore, the
    original coroutine's next child would allocate from
    the wrong resource.

    Event loops, strand dispatch loops, and any code that
    calls `.resume()` on a coroutine handle should use
    this function instead of calling `.resume()` directly.
    See the @ref Executor concept documentation for details.

    @param h The coroutine handle to resume.

    @see get_current_frame_allocator, set_current_frame_allocator
*/
inline void
safe_resume(std::coroutine_handle<> h) noexcept
{
    auto* saved = get_current_frame_allocator();
    h.resume();
    set_current_frame_allocator(saved);
}

} // namespace capy
} // namespace boost

#endif
