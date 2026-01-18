//
// Copyright (c) 2025 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Test that header file is self-contained.
#include <boost/capy/ex/stop_token_support.hpp>

#include "test_suite.hpp"

#include <coroutine>
#include <string>
#include <utility>

namespace boost {
namespace capy {

struct test_coro
{
    struct promise_type : stop_token_support<promise_type>
    {
        test_coro get_return_object()
        {
            return test_coro{
                std::coroutine_handle<promise_type>::from_promise(*this)};
        }

        std::suspend_always initial_suspend() noexcept { return {}; }
        std::suspend_always final_suspend() noexcept { return {}; }
        void return_void() {}
        void unhandled_exception() {}
    };

    std::coroutine_handle<promise_type> h_;

    ~test_coro()
    {
        if(h_)
            h_.destroy();
    }

    test_coro(test_coro const&) = delete;
    test_coro& operator=(test_coro const&) = delete;

    test_coro(test_coro&& other) noexcept
        : h_(std::exchange(other.h_, nullptr))
    {
    }

private:
    explicit test_coro(std::coroutine_handle<promise_type> h)
        : h_(h)
    {
    }
};

struct custom_transform_coro
{
    struct promise_type : stop_token_support<promise_type>
    {
        int transform_count_ = 0;

        custom_transform_coro get_return_object()
        {
            return custom_transform_coro{
                std::coroutine_handle<promise_type>::from_promise(*this)};
        }

        std::suspend_always initial_suspend() noexcept { return {}; }
        std::suspend_always final_suspend() noexcept { return {}; }
        void return_void() {}
        void unhandled_exception() {}

        template<typename A>
        decltype(auto) transform_awaitable(A&& a)
        {
            ++transform_count_;
            return std::forward<A>(a);
        }
    };

    std::coroutine_handle<promise_type> h_;

    ~custom_transform_coro()
    {
        if(h_)
            h_.destroy();
    }

    custom_transform_coro(custom_transform_coro const&) = delete;
    custom_transform_coro& operator=(custom_transform_coro const&) = delete;

    custom_transform_coro(custom_transform_coro&& other) noexcept
        : h_(std::exchange(other.h_, nullptr))
    {
    }

private:
    explicit custom_transform_coro(std::coroutine_handle<promise_type> h)
        : h_(h)
    {
    }
};

struct stop_token_support_test
{
    void
    testSetAndGetStopToken()
    {
        std::stop_source source;
        auto token = source.get_token();

        auto coro = []() -> test_coro { co_return; }();
        coro.h_.promise().set_stop_token(token);

        auto retrieved = coro.h_.promise().stop_token();
        BOOST_TEST(retrieved.stop_possible());
        BOOST_TEST(!retrieved.stop_requested());

        source.request_stop();
        BOOST_TEST(retrieved.stop_requested());
    }

    void
    testDefaultStopToken()
    {
        auto coro = []() -> test_coro { co_return; }();
        auto token = coro.h_.promise().stop_token();

        BOOST_TEST(!token.stop_possible());
        BOOST_TEST(!token.stop_requested());
    }

    void
    testAwaitTransformInterceptsTag()
    {
        auto coro = []() -> test_coro { co_return; }();

        std::stop_source source;
        coro.h_.promise().set_stop_token(source.get_token());

        auto awaiter = coro.h_.promise().await_transform(get_stop_token());

        BOOST_TEST(awaiter.await_ready());

        auto token = awaiter.await_resume();
        BOOST_TEST(token.stop_possible());
    }

    void
    testAwaitTransformDelegatesToTransformAwaitable()
    {
        auto coro = []() -> custom_transform_coro { co_return; }();

        BOOST_TEST_EQ(coro.h_.promise().transform_count_, 0);

        struct dummy_awaitable
        {
            bool await_ready() { return true; }
            void await_suspend(std::coroutine_handle<>) {}
            void await_resume() {}
        };

        coro.h_.promise().await_transform(dummy_awaitable{});
        BOOST_TEST_EQ(coro.h_.promise().transform_count_, 1);

        coro.h_.promise().await_transform(get_stop_token());
        BOOST_TEST_EQ(coro.h_.promise().transform_count_, 1);
    }

    void
    testStopTokenAwaiterNeverSuspends()
    {
        auto coro = []() -> test_coro { co_return; }();
        auto awaiter = coro.h_.promise().await_transform(get_stop_token());

        BOOST_TEST(awaiter.await_ready());
        awaiter.await_suspend(any_coro{});
    }

    void
    run()
    {
        testSetAndGetStopToken();
        testDefaultStopToken();
        testAwaitTransformInterceptsTag();
        testAwaitTransformDelegatesToTransformAwaitable();
        testStopTokenAwaiterNeverSuspends();
    }
};

TEST_SUITE(
    stop_token_support_test,
    "boost.capy.ex.stop_token_support");

} // namespace capy
} // namespace boost
