//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_CONCEPT_IO_AWAITABLE_TASK_HPP
#define BOOST_CAPY_CONCEPT_IO_AWAITABLE_TASK_HPP

#include <boost/capy/detail/config.hpp>
#include <boost/capy/concept/io_awaitable.hpp>
#include <boost/capy/coro.hpp>
#include <boost/capy/ex/executor_ref.hpp>

#include <concepts>
#include <stop_token>

namespace boost {
namespace capy {

/** Concept for task types with promise-based context injection.

    Extends @ref IoAwaitable with a `promise_type` that stores executor
    and stop token state. This enables launch functions (`run`, `run_async`)
    to inject context at the root of a coroutine chain without going
    through `await_suspend`.

    @tparam T The task type.

    @par Syntactic Requirements

    @li `T` must satisfy @ref IoAwaitable
    @li `T::promise_type` must be a valid type
    @li `p.set_executor(ex)` must be valid and `noexcept`
    @li `p.set_stop_token(st)` must be valid and `noexcept`
    @li `p.set_continuation(cont, ex)` must be valid and `noexcept`
    @li `p.executor()` must return `executor_ref` and be `noexcept`
    @li `p.stop_token()` must return `std::stop_token const&` and be `noexcept`
    @li `p.complete()` must return `coro` and be `noexcept`

    @par Semantic Requirements

    The `set_executor` and `set_stop_token` operations inject context:

    @li Called by launch functions before resuming the task
    @li The promise stores these values for use by child awaitables
    @li Values propagate to nested `co_await` expressions

    The `executor` and `stop_token` accessors retrieve stored context:

    @li Return the values set by launch functions or parent tasks
    @li Used by awaitables to schedule resumption and check cancellation

    The `set_continuation` and `complete` operations manage resumption:

    @li `set_continuation` stores who to resume when the task completes
    @li `complete` returns the coroutine handle to resume at completion

    @par Conforming Signatures

    @code
    struct T
    {
        struct promise_type
        {
            void set_executor( executor_ref ex ) noexcept;
            void set_stop_token( std::stop_token st ) noexcept;
            void set_continuation( coro cont, executor_ref ex ) noexcept;
            executor_ref executor() const noexcept;
            std::stop_token const& stop_token() const noexcept;
            coro complete() const noexcept;
        };

        bool await_ready() const noexcept;
        coro await_suspend( coro h, executor_ref ex, std::stop_token token );
        R await_resume();
    };
    @endcode

    @par Example

    @code
    struct my_task
    {
        struct promise_type : io_awaitable_support<promise_type>
        {
            my_task get_return_object();
            std::suspend_always initial_suspend() noexcept;
            std::suspend_always final_suspend() noexcept;
            void return_void();
            void unhandled_exception();
        };

        std::coroutine_handle<promise_type> h_;

        bool await_ready() const noexcept { return false; }

        coro await_suspend( coro cont, executor_ref ex, std::stop_token token )
        {
            h_.promise().set_executor( ex );
            h_.promise().set_stop_token( token );
            h_.promise().set_continuation( cont, ex );
            return h_;
        }

        void await_resume() {}
    };

    static_assert( IoAwaitableTask<my_task> );
    @endcode

    @see IoAwaitable, IoLaunchableTask, io_awaitable_support
*/
template<typename T>
concept IoAwaitableTask =
    IoAwaitable<T> &&
    requires { typename T::promise_type; } &&
    requires(
        typename T::promise_type& p,
        typename T::promise_type const& cp,
        executor_ref ex,
        std::stop_token st,
        coro cont)
    {
        { p.set_executor(ex) } noexcept;
        { p.set_stop_token(st) } noexcept;
        { p.set_continuation(cont, ex) } noexcept;
        { cp.executor() } noexcept -> std::same_as<executor_ref>;
        { cp.stop_token() } noexcept -> std::same_as<std::stop_token const&>;
        { cp.complete() } noexcept -> std::same_as<coro>;
    };

} // namespace capy
} // namespace boost

#endif
