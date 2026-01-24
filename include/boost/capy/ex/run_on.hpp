//
// Copyright (c) 2025 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_RUN_ON_HPP
#define BOOST_CAPY_RUN_ON_HPP

#include <boost/capy/detail/config.hpp>
#include <boost/capy/concept/executor.hpp>
#include <boost/capy/concept/io_launchable_task.hpp>
#include <boost/capy/ex/executor_ref.hpp>

#include <stop_token>
#include <utility>

namespace boost {
namespace capy {
namespace detail {

/** Awaitable that binds an IoAwaitableTask to a specific executor.

    Stores the executor and inner task by value. When co_awaited, the
    co_await expression's lifetime extension keeps both alive for the
    duration of the operation.

    @tparam Task The IoAwaitableTask type
    @tparam Ex The executor type
*/
template<IoLaunchableTask Task, Executor Ex>
struct [[nodiscard]]
    run_on_awaitable
{
    Ex ex_;
    Task inner_;

    run_on_awaitable(Ex ex, Task inner)
        : ex_(std::move(ex))
        , inner_(std::move(inner))
    {
    }

    bool await_ready() const noexcept
    {
        return inner_.await_ready();
    }

    auto await_resume()
    {
        return inner_.await_resume();
    }

    // IoAwaitable: receives caller's executor and stop_token for completion dispatch
    template<typename Caller>
    coro await_suspend(coro cont, Caller const& caller_ex, std::stop_token token)
    {
        auto h = inner_.handle();
        auto& p = h.promise();
        p.set_executor(ex_);
        p.set_continuation(cont, caller_ex);
        p.set_stop_token(token);
        return h;
    }

    // Non-copyable
    run_on_awaitable(run_on_awaitable const&) = delete;
    run_on_awaitable& operator=(run_on_awaitable const&) = delete;

    // Movable
    run_on_awaitable(run_on_awaitable&& other) noexcept
        : ex_(std::move(other.ex_))
        , inner_(std::move(other.inner_))
    {
    }

    run_on_awaitable& operator=(run_on_awaitable&& other) noexcept
    {
        if(this != &other)
        {
            ex_ = std::move(other.ex_);
            inner_ = std::move(other.inner_);
        }
        return *this;
    }
};

} // namespace detail

/** Binds a task to execute on a specific executor.

    The executor is stored by value in the returned awaitable.
    When co_awaited, the inner task receives this executor through
    direct promise configuration.

    @param ex The executor on which the task should run (copied by value).
    @param t The IoAwaitableTask to bind to the executor.

    @return An awaitable that runs t on the specified executor.
*/
template<Executor Ex, IoLaunchableTask Task>
[[nodiscard]] auto run_on(Ex ex, Task t)
{
    return detail::run_on_awaitable<Task, Ex>{
        std::move(ex), std::move(t)};
}

} // namespace capy
} // namespace boost

#endif
