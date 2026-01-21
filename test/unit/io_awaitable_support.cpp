//
// Copyright (c) 2025 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Test that header file is self-contained.
#include <boost/capy/io_awaitable.hpp>

#include <boost/capy/ex/thread_pool.hpp>

#include "test_suite.hpp"

#include <coroutine>
#include <string>
#include <utility>

namespace boost {
namespace capy {

struct test_coro
{
    struct promise_type : io_awaitable_support<promise_type>
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
    struct promise_type : io_awaitable_support<promise_type>
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

struct io_awaitable_support_test
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
    testAwaitTransformInterceptsStopTokenTag()
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

        coro.h_.promise().await_transform(get_executor());
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
    testSetAndGetExecutor()
    {
        thread_pool pool(1);
        auto executor = pool.get_executor();

        auto coro = []() -> test_coro { co_return; }();
        coro.h_.promise().set_executor(executor);

        auto retrieved = coro.h_.promise().executor();
        BOOST_TEST(static_cast<bool>(retrieved));
        BOOST_TEST(retrieved == executor_ref(executor));
    }

    void
    testDefaultExecutor()
    {
        auto coro = []() -> test_coro { co_return; }();
        auto ex = coro.h_.promise().executor();

        BOOST_TEST(!static_cast<bool>(ex));
    }

    void
    testAwaitTransformInterceptsExecutorTag()
    {
        thread_pool pool(1);
        auto executor = pool.get_executor();

        auto coro = []() -> test_coro { co_return; }();
        coro.h_.promise().set_executor(executor);

        auto awaiter = coro.h_.promise().await_transform(get_executor());

        BOOST_TEST(awaiter.await_ready());

        auto ex = awaiter.await_resume();
        BOOST_TEST(static_cast<bool>(ex));
        BOOST_TEST(ex == executor_ref(executor));
    }

    void
    testExecutorAwaiterNeverSuspends()
    {
        auto coro = []() -> test_coro { co_return; }();
        auto awaiter = coro.h_.promise().await_transform(get_executor());

        BOOST_TEST(awaiter.await_ready());
        awaiter.await_suspend(any_coro{});
    }

    void
    run()
    {
        testSetAndGetStopToken();
        testDefaultStopToken();
        testAwaitTransformInterceptsStopTokenTag();
        testAwaitTransformDelegatesToTransformAwaitable();
        testStopTokenAwaiterNeverSuspends();
        testSetAndGetExecutor();
        testDefaultExecutor();
        testAwaitTransformInterceptsExecutorTag();
        testExecutorAwaiterNeverSuspends();
    }
};

TEST_SUITE(
    io_awaitable_support_test,
    "boost.capy.io_awaitable_support");

} // namespace capy
} // namespace boost
