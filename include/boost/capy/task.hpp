//
// Copyright (c) 2025 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_TASK_HPP
#define BOOST_CAPY_TASK_HPP

#include <boost/capy/detail/config.hpp>

#ifdef BOOST_CAPY_HAS_CORO

#include <coroutine>
#include <exception>
#include <functional>
#include <utility>
#include <variant>

namespace boost {
namespace capy {

template<class T>
class task
{
public:
    struct promise_type
    {
        std::variant<std::monostate, T, std::exception_ptr> result_;
        std::function<void()> on_done_;

        task get_return_object()
        {
            return task{std::coroutine_handle<promise_type>::from_promise(*this)};
        }

        std::suspend_always initial_suspend() noexcept { return {}; }

        auto final_suspend() noexcept
        {
            struct awaiter
            {
                promise_type* p_;
                bool await_ready() noexcept { return false; }
                void await_suspend(std::coroutine_handle<>) noexcept
                {
                    if (p_->on_done_)
                        p_->on_done_();
                }
                void await_resume() noexcept {}
            };
            return awaiter{this};
        }

        void return_value(T v) { result_.template emplace<1>(std::move(v)); }
        void unhandled_exception() { result_.template emplace<2>(std::current_exception()); }
    };

private:
    std::coroutine_handle<promise_type> h_;

public:
    explicit task(std::coroutine_handle<promise_type> h) : h_(h) {}
    ~task() { if (h_) h_.destroy(); }

    task(task&& o) noexcept : h_(std::exchange(o.h_, {})) {}
    task& operator=(task&&) = delete;

    // For awaiting from another task<U>
    bool await_ready() const noexcept { return false; }

    std::coroutine_handle<> await_suspend(std::coroutine_handle<> caller) noexcept
    {
        h_.promise().on_done_ = [caller]{ caller.resume(); };
        return h_;
    }

    T await_resume()
    {
        auto& r = h_.promise().result_;
        if (r.index() == 2)
            std::rethrow_exception(std::get<2>(r));
        return std::move(std::get<1>(r));
    }

    // For external drivers
    std::coroutine_handle<promise_type> handle() const noexcept { return h_; }

    std::coroutine_handle<promise_type> release() noexcept
    {
        return std::exchange(h_, {});
    }
};

} // capy
} // boost

#endif

#endif
