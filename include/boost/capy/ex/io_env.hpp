//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_IO_ENV_HPP
#define BOOST_CAPY_IO_ENV_HPP

#include <boost/capy/detail/config.hpp>
#include <boost/capy/ex/executor_ref.hpp>

#include <coroutine>
#include <memory_resource>
#include <stop_token>

namespace boost {
namespace capy {

/** Callable that posts a continuation to an executor.

    Use this as the callback type for `std::stop_callback` instead
    of resuming a coroutine handle directly. Direct resumption runs
    the coroutine inline on whatever thread calls `request_stop()`,
    which bypasses the executor and corrupts the thread-local
    frame allocator.

    Prefer @ref io_env::post_resume and the @ref stop_resume_callback
    alias to construct these—see examples there.

    @see io_env::post_resume, stop_resume_callback
*/
struct resume_via_post
{
    executor_ref ex;
    mutable continuation cont;

    // post() must not throw; stop_callback requires a
    // non-throwing invocable.
    void operator()() const noexcept
    {
        ex.post(cont);
    }
};

/** Execution environment for IoAwaitables.

    This struct bundles the execution context passed through
    coroutine chains via the IoAwaitable protocol. It contains
    the executor for resumption, a stop token for cancellation,
    and an optional frame allocator for coroutine frame allocation.

    @par Lifetime

    Launch functions (@ref run_async, @ref run) own the `io_env` and
    guarantee it outlives all tasks and awaitables in the launched
    chain. Awaitables receive `io_env const*` in `await_suspend`
    and should store it directly, never copy the pointed-to object.

    @par Stop Callback Contract

    Awaitables that register a `std::stop_callback` **must not**
    resume the coroutine handle directly. The callback fires
    synchronously on the thread that calls `request_stop()`, which
    may not be an executor-managed thread. Resuming inline poisons
    that thread's TLS frame allocator with the pool's allocator,
    causing use-after-free on the next coroutine allocation.

    Use @ref io_env::post_resume and @ref stop_resume_callback:
    @code
    std::optional<stop_resume_callback> stop_cb_;
    // In await_suspend:
    stop_cb_.emplace(env->stop_token, env->post_resume(h));
    @endcode

    @par Thread Safety
    The referenced executor and allocator must remain valid
    for the lifetime of any coroutine using this environment.

    @see IoAwaitable, IoRunnable, resume_via_post
*/
struct io_env
{
    /** The executor for coroutine resumption. */
    executor_ref executor;

    /** The stop token for cancellation propagation. */
    std::stop_token stop_token;

    /** The frame allocator for coroutine frame allocation.

        When null, the default allocator is used.
    */
    std::pmr::memory_resource* frame_allocator = nullptr;

    /** Create a resume_via_post callable for this environment.

        Convenience method for registering @ref stop_resume_callback
        instances. Wraps the coroutine handle in a @ref continuation
        and pairs it with this environment's executor. Equivalent to
        `resume_via_post{executor, continuation{h}}`.

        @par Example
        @code
        stop_cb_.emplace(env->stop_token, env->post_resume(h));
        @endcode

        @param h The coroutine handle to wrap in a continuation
            and post on cancellation.

        @return A @ref resume_via_post callable that holds a
        non-owning @ref executor_ref and a @ref continuation.
        The callable must not outlive the executor it references.

        @see resume_via_post, stop_resume_callback
    */
    resume_via_post
    post_resume(std::coroutine_handle<> h) const noexcept
    {
        return resume_via_post{executor, continuation{h}};
    }
};

/** Type alias for a stop callback that posts through the executor.

    Use this to declare the stop callback member in your awaitable:
    @code
    std::optional<stop_resume_callback> stop_cb_;
    @endcode

    @see resume_via_post, io_env::post_resume
*/
using stop_resume_callback = std::stop_callback<resume_via_post>;

} // capy
} // boost

#endif
