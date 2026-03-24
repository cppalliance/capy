//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_CONCEPT_EXECUTOR_HPP
#define BOOST_CAPY_CONCEPT_EXECUTOR_HPP

#include <boost/capy/detail/config.hpp>
#include <boost/capy/continuation.hpp>

#include <concepts>
#include <coroutine>
#include <type_traits>

namespace boost {
namespace capy {

class execution_context;

/** Concept for types that schedule coroutine execution.

    An executor embodies a set of rules for determining how and where
    coroutines are executed. It provides operations to submit work
    and to track outstanding work for graceful shutdown.

    Ordinary users writing coroutine tasks do not interact with
    `dispatch` and `post` directly. These operations are used by
    authors of coroutine machinery -- `promise_type` implementations,
    awaitables, `await_transform` -- to implement asynchronous
    algorithms such as `when_all`, `when_any`, `async_mutex`,
    channels, and similar primitives.

    @tparam E The executor type.

    @par Syntactic Requirements

    @li `E` must be nothrow copy and move constructible
    @li `ce == ce2` must return a type convertible to `bool`, `noexcept`
    @li `ce.context()` must return an lvalue reference to a type derived
        from `execution_context`, `noexcept`
    @li `ce.on_work_started()` must be valid and `noexcept`
    @li `ce.on_work_finished()` must be valid and `noexcept`
    @li `ce.dispatch(c)` must return `std::coroutine_handle<>`
    @li `ce.post(c)` must be valid

    @par Semantic Requirements

    The `context` operation returns the owning context:

    @li Returns a reference to the execution context that created
        this executor
    @li The context outlives all executors created from it

    The `on_work_started` and `on_work_finished` operations track work:

    @li Calls must be paired; each `on_work_started` must have a
        matching `on_work_finished`
    @li The context uses this count to determine when shutdown
        is complete
    @li These are not intended for direct use by callers. They
        are public so that work guards can invoke them. This
        enables user-defined guards with additional tracking
        behaviors, without the library needing to grant friendship
        to types it cannot anticipate

    The `dispatch` operation returns a handle for symmetric transfer:

    Every coroutine resumption must go through either symmetric
    transfer or the scheduler queue -- never through an inline
    `resume()` or `dispatch()` that creates a frame below the
    resumed coroutine.

    @li If the executor determines it is safe to resume inline
        (e.g., already on the correct thread), returns `c.h` for
        the caller to use in symmetric transfer
    @li Otherwise, posts the continuation for later execution and
        returns `std::noop_coroutine()`
    @li The caller is responsible for using the returned handle
        appropriately: returning it from `await_suspend` for
        symmetric transfer, or calling `.resume()` if at the
        event loop pump level

    A conforming implementation might look like:

    @code
    std::coroutine_handle<> dispatch(
        continuation& c ) const
    {
        if( ctx_.is_running_on_this_thread() )
            return c.h;            // symmetric transfer
        post( c );
        return std::noop_coroutine();
    }
    @endcode

    The `post` operation queues for later execution:

    @li Never blocks the caller
    @li The coroutine executes on the executor's associated context

    @par Continuation Lifetime

    Both `dispatch` and `post` operate on the caller's
    continuation object by reference. The continuation must
    remain at a stable address and must not be moved or
    destroyed until the executor has dequeued and resumed it.
    Destroying or moving a continuation while it is linked
    into an executor's queue is undefined behavior.

    When `dispatch` returns `c.h` (the inline case), the
    continuation is not enqueued and may be reused or
    destroyed immediately. When `dispatch` falls through to
    `post`, the continuation is enqueued and the lifetime
    requirement applies.

    @par Frame Allocator TLS

    The library propagates a frame allocator via thread-local
    storage. When a custom executor's event loop calls
    `.resume()` to drain its work queue, it must use
    `safe_resume()` from `<boost/capy/ex/frame_allocator.hpp>`
    instead of calling `h.resume()` directly. This saves and
    restores the thread-local frame allocator around the call,
    preventing a resumed coroutine from permanently overwriting
    the caller's value.

    @par Executor Validity

    An executor becomes invalid when the first call to
    `ctx.shutdown()` returns. Calling `dispatch`, `post`,
    `on_work_started`, or `on_work_finished` on an invalid executor
    is undefined behavior. Copy, comparison, and `context()` remain
    valid until the context is destroyed.

    @par Thread Safety
    Distinct objects: Safe.
    Shared objects: Safe for copy, comparison, and `context()`.

    @par Conforming Signatures

    @code
    class E
    {
    public:
        execution_context& context() const noexcept;

        void on_work_started() const noexcept;
        void on_work_finished() const noexcept;

        std::coroutine_handle<> dispatch(
            continuation& c ) const;
        void post( continuation& c ) const;

        bool operator==( E const& ) const noexcept;
    };
    @endcode

    @see ExecutionContext, execution_context
*/
template<class E>
concept Executor =
    std::is_nothrow_copy_constructible_v<E> &&
    std::is_nothrow_move_constructible_v<E> &&
    requires(E& e, E const& ce, E const& ce2, continuation c) {
        { ce == ce2 } noexcept -> std::convertible_to<bool>;
        { ce.context() } noexcept;
        requires std::is_lvalue_reference_v<decltype(ce.context())> &&
            std::derived_from<
                std::remove_reference_t<decltype(ce.context())>,
                execution_context>;
        { ce.on_work_started() } noexcept;
        { ce.on_work_finished() } noexcept;

        { ce.dispatch(c) } -> std::same_as<std::coroutine_handle<>>;
        { ce.post(c) };
    };

} // capy
} // boost

#endif
