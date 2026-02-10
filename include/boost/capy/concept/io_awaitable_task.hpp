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
#include <boost/capy/ex/io_env.hpp>

#include <concepts>

namespace boost {
namespace capy {

/** Concept for task types with promise-based context injection.

    Extends @ref IoAwaitable with a `promise_type` that stores the
    execution environment. This enables coroutine launch functions
    such as @ref run to inject context at the root of a coroutine
    chain without going through `await_suspend`.

    @tparam T The task type.

    @par Syntactic Requirements

    @li `T` must satisfy @ref IoAwaitable
    @li `T::promise_type` must be a valid type
    @li `p.set_environment(env)` must be valid and `noexcept`
    @li `p.set_continuation(cont, ex)` must be valid and `noexcept`
    @li `p.environment()` must return `io_env const&` and be `noexcept`
    @li `p.complete()` must return `coro` and be `noexcept`

    @par Semantic Requirements

    The `set_environment` operation injects context:

    @li Called by launch functions before resuming the task
    @li The promise stores the address of the @ref io_env, not a copy.
        The referenced @ref io_env is owned by the launch function and
        is guaranteed to outlive the task.
    @li Values propagate to nested `co_await` expressions

    The `environment` accessor retrieves stored context:

    @li Returns a const reference to the @ref io_env owned by the
        launch function or parent awaitable
    @li Used by awaitables to schedule resumption, check cancellation,
        and allocate memory

    The `set_continuation` and `complete` operations manage resumption:

    @li `set_continuation` stores who to resume when the task completes
    @li `complete` returns the coroutine handle to resume at completion

    @par Conforming Signatures

    @code
    struct T
    {
        struct promise_type
        {
            void set_environment( io_env const& env ) noexcept;
            void set_continuation( coro cont, executor_ref ex ) noexcept;
            io_env const& environment() const noexcept;
            coro complete() const noexcept;
        };

        bool await_ready() const noexcept;
        std::coroutine_handle<> await_suspend(
            std::coroutine_handle<> h, io_env const& env );
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

        coro await_suspend( coro cont, io_env const& env )
        {
            h_.promise().set_environment( env );
            h_.promise().set_continuation( cont, env.executor );
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
        io_env const& env,
        executor_ref ex,
        coro cont)
    {
        { p.set_environment(env) } noexcept;
        { p.set_continuation(cont, ex) } noexcept;
        { cp.environment() } noexcept -> std::same_as<io_env const&>;
        { cp.complete() } noexcept -> std::same_as<coro>;
    };

} // namespace capy
} // namespace boost

#endif
