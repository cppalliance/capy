//
// Copyright (c) 2025 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_TEST_CUSTOM_TASK_HPP
#define BOOST_CAPY_TEST_CUSTOM_TASK_HPP

#include <boost/capy/coro.hpp>
#include <boost/capy/ex/io_awaitable_support.hpp>

#include <coroutine>
#include <exception>
#include <optional>
#include <stop_token>
#include <type_traits>
#include <utility>

namespace boost {
namespace capy {
namespace test {

template<typename T>
struct custom_task_result_base
{
    std::optional<T> result_;

    void return_value(T value) { result_ = std::move(value); }
    T&& result() noexcept { return std::move(*result_); }
};

template<>
struct custom_task_result_base<void>
{
    void return_void() {}
};

/** A custom task type that satisfies IoLaunchableTask.

    This task is intentionally NOT capy::task to prove that
    run_async and run_on work with any IoLaunchableTask type.
*/
template<typename T>
struct custom_task
{
    struct promise_type
        : io_awaitable_support<promise_type>
        , custom_task_result_base<T>
    {
        std::exception_ptr ep_;

        custom_task get_return_object()
        {
            return custom_task{
                std::coroutine_handle<promise_type>::from_promise(*this)};
        }

        std::suspend_always initial_suspend() noexcept { return {}; }

        auto final_suspend() noexcept
        {
            struct awaiter
            {
                promise_type* p_;
                bool await_ready() const noexcept { return false; }
                coro await_suspend(coro) const noexcept { return p_->complete(); }
                void await_resume() const noexcept {}
            };
            return awaiter{this};
        }

        void unhandled_exception() { ep_ = std::current_exception(); }
        std::exception_ptr exception() const noexcept { return ep_; }
    };

    std::coroutine_handle<promise_type> h_;

    explicit custom_task(std::coroutine_handle<promise_type> h) : h_(h) {}

    ~custom_task() { if(h_) h_.destroy(); }

    custom_task(custom_task const&) = delete;
    custom_task& operator=(custom_task const&) = delete;
    custom_task(custom_task&& o) noexcept : h_(std::exchange(o.h_, nullptr)) {}
    custom_task& operator=(custom_task&& o) noexcept
    {
        if(this != &o) { if(h_) h_.destroy(); h_ = std::exchange(o.h_, nullptr); }
        return *this;
    }

    bool await_ready() const noexcept { return false; }

    auto await_resume()
    {
        if(h_.promise().ep_)
            std::rethrow_exception(h_.promise().ep_);
        if constexpr (!std::is_void_v<T>)
            return std::move(*h_.promise().result_);
    }

    template<typename Ex>
    coro await_suspend(coro cont, Ex const& caller_ex, std::stop_token token)
    {
        h_.promise().set_continuation(cont, caller_ex);
        h_.promise().set_executor(caller_ex);
        h_.promise().set_stop_token(token);
        return h_;
    }

    std::coroutine_handle<promise_type> handle() const noexcept { return h_; }
    void release() noexcept { h_ = nullptr; }
};

static_assert(IoLaunchableTask<custom_task<int>>);
static_assert(IoLaunchableTask<custom_task<void>>);

} // namespace test
} // namespace capy
} // namespace boost

#endif
