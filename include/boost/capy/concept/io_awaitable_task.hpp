//
// Copyright (c) 2025 Vinnie Falco (vinnie dot falco at gmail dot com)
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

/** Concept for I/O awaitable task types.

    A task is an I/O awaitable task if it satisfies @ref IoAwaitable and
    its `promise_type` provides the interface for context injection:

    @li `set_executor(executor_ref)` — stores the executor
    @li `set_stop_token(std::stop_token)` — stores the stop token
    @li `executor()` — retrieves the stored executor
    @li `stop_token()` — retrieves the stored stop token

    This concept formalizes the contract between launch functions
    (`run_async`, `run`) and task types. Launch functions are the
    root of a coroutine chain and must set context directly on the
    promise rather than going through `await_suspend`.

    @tparam T The task type.

    @par Requirements
    @li `T` must satisfy @ref IoAwaitable
    @li `T::promise_type` must exist
    @li The promise must provide `set_executor` and `set_stop_token`
    @li The promise must provide `executor` and `stop_token` accessors

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

        coro await_suspend(coro cont, executor_ref ex, std::stop_token token)
        {
            h_.promise().set_executor(ex);
            h_.promise().set_stop_token(token);
            // ... set continuation, return handle ...
        }

        void await_resume() {}
    };

    static_assert(IoAwaitableTask<my_task>);
    @endcode

    @see IoAwaitable
    @see io_awaitable_support
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
