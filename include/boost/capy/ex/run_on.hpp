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
#include <boost/capy/ex/any_executor_ref.hpp>
#include <boost/capy/task.hpp>

#include <stop_token>
#include <utility>

namespace boost {
namespace capy {
namespace detail {

/** Awaitable that binds a task to a specific executor.

    Stores the executor by value. When co_awaited, the co_await
    expression's lifetime extension keeps the executor alive for
    the duration of the operation.

    @tparam T The task's return type
    @tparam Ex The executor type
*/
template<typename T, Executor Ex>
struct [[nodiscard]] run_on_awaitable
{
    Ex ex_;
    std::coroutine_handle<typename task<T>::promise_type> h_;

    run_on_awaitable(
        Ex ex,
        std::coroutine_handle<typename task<T>::promise_type> h)
        : ex_(std::move(ex))
        , h_(h)
    {
    }

    bool await_ready() const noexcept
    {
        return false;
    }

    auto await_resume()
    {
        if(h_.promise().ep_)
            std::rethrow_exception(h_.promise().ep_);
        if constexpr (std::is_void_v<T>)
            return;
        else
            return std::move(*h_.promise().result_);
    }

    // IoAwaitable: receives caller's executor and stop_token for completion dispatch
    template<typename Caller>
    any_coro await_suspend(any_coro continuation, Caller const& caller_ex, std::stop_token token)
    {
        // 'this' is kept alive by co_await until completion
        // ex_ is valid for the entire operation
        h_.promise().ex_ = ex_;
        h_.promise().caller_ex_ = caller_ex;
        h_.promise().continuation_ = continuation;
#if BOOST_CAPY_HAS_STOP_TOKEN
        h_.promise().set_stop_token(token);
#else
        (void)token;
#endif
        h_.promise().needs_dispatch_ = true;
        return h_;
    }

    ~run_on_awaitable()
    {
        if(h_ && !h_.done())
            h_.destroy();
    }

    // Non-copyable
    run_on_awaitable(run_on_awaitable const&) = delete;
    run_on_awaitable& operator=(run_on_awaitable const&) = delete;

    // Movable
    run_on_awaitable(run_on_awaitable&& other) noexcept
        : ex_(std::move(other.ex_))
        , h_(std::exchange(other.h_, nullptr))
    {
    }

    run_on_awaitable& operator=(run_on_awaitable&& other) noexcept
    {
        if(this != &other)
        {
            if(h_ && !h_.done())
                h_.destroy();
            ex_ = std::move(other.ex_);
            h_ = std::exchange(other.h_, nullptr);
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
    @param t The task to bind to the executor.

    @return An awaitable that runs t on the specified executor.
*/
template<Executor Ex, typename T>
[[nodiscard]] auto run_on(Ex ex, task<T> t)
{
    return detail::run_on_awaitable<T, Ex>{
        std::move(ex), t.release()};
}

} // namespace capy
} // namespace boost

#endif
