//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Test that header file is self-contained.
#include <boost/capy/ex/context_awaitable_promise_base.hpp>

#include <boost/capy/ex/await_context.hpp>
#include <boost/capy/ex/frame_allocator.hpp>
#include <boost/capy/ex/thread_pool.hpp>

#include "test_suite.hpp"

#include <coroutine>
#include <memory_resource>
#include <string>
#include <utility>

namespace boost {
namespace capy {

struct test_coro
{
    struct promise_type : context_awaitable_promise_base<promise_type>
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
    struct promise_type : context_awaitable_promise_base<promise_type>
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

// Completes inside await_suspend, recording the environment it was given.
struct recording_awaitable
{
    await_context const** seen;

    bool await_ready() const noexcept { return false; }

    bool await_suspend(std::coroutine_handle<>, await_context const* env)
    {
        *seen = env;
        return false;
    }

    int await_resume() const noexcept { return 7; }
};

struct context_awaitable_promise_base_test
{
    void
    testSetAndGetEnvironment()
    {
        std::stop_source source;
        await_context env;
        env.stop_token = source.get_token();

        auto c = []() -> test_coro { co_return; }();
        c.h_.promise().set_environment(&env);

        auto const* retrieved = c.h_.promise().environment();
        BOOST_TEST(retrieved->stop_token.stop_possible());
        BOOST_TEST(!retrieved->stop_token.stop_requested());

        source.request_stop();
        BOOST_TEST(retrieved->stop_token.stop_requested());
    }

    void
    testDefaultEnvironment()
    {
        auto c = []() -> test_coro { co_return; }();
        await_context env;
        c.h_.promise().set_environment(&env);
        auto const* retrieved = c.h_.promise().environment();

        BOOST_TEST(!retrieved->stop_token.stop_possible());
        BOOST_TEST(!retrieved->stop_token.stop_requested());
        BOOST_TEST(!static_cast<bool>(retrieved->executor));
    }

    void
    testAwaitTransformInterceptsEnvironmentTag()
    {
        auto c = []() -> test_coro { co_return; }();

        std::stop_source source;
        await_context env;
        env.stop_token = source.get_token();
        c.h_.promise().set_environment(&env);

        auto awaiter = c.h_.promise().await_transform(this_coro::environment);

        BOOST_TEST(awaiter.await_ready());

        auto const* retrieved = awaiter.await_resume();
        BOOST_TEST(retrieved->stop_token.stop_possible());
    }

    void
    testAwaitTransformDelegatesToTransformAwaitable()
    {
        auto c = []() -> custom_transform_coro { co_return; }();

        await_context env;
        c.h_.promise().set_environment(&env);

        BOOST_TEST_EQ(c.h_.promise().transform_count_, 0);

        struct dummy_awaitable
        {
            bool await_ready() { return true; }
            void await_suspend(std::coroutine_handle<>) {}
            void await_resume() {}
        };

        c.h_.promise().await_transform(dummy_awaitable{});
        BOOST_TEST_EQ(c.h_.promise().transform_count_, 1);

        // None of the this_coro tags should delegate
        c.h_.promise().await_transform(this_coro::environment);
        BOOST_TEST_EQ(c.h_.promise().transform_count_, 1);

        c.h_.promise().await_transform(this_coro::executor);
        BOOST_TEST_EQ(c.h_.promise().transform_count_, 1);

        c.h_.promise().await_transform(this_coro::stop_token);
        BOOST_TEST_EQ(c.h_.promise().transform_count_, 1);

        c.h_.promise().await_transform(this_coro::frame_allocator);
        BOOST_TEST_EQ(c.h_.promise().transform_count_, 1);
    }

    void
    testEnvironmentAwaiterNeverSuspends()
    {
        auto c = []() -> test_coro { co_return; }();
        await_context env;
        c.h_.promise().set_environment(&env);
        auto awaiter = c.h_.promise().await_transform(this_coro::environment);

        BOOST_TEST(awaiter.await_ready());
        awaiter.await_suspend(c.h_);
    }

    void
    testSetAndGetExecutorViaEnvironment()
    {
        thread_pool pool(1);
        auto executor = pool.get_executor();

        auto c = []() -> test_coro { co_return; }();
        await_context env;
        env.executor = executor_ref(executor);
        c.h_.promise().set_environment(&env);

        auto const* retrieved = c.h_.promise().environment();
        BOOST_TEST(static_cast<bool>(retrieved->executor));
        BOOST_TEST(retrieved->executor == executor_ref(executor));
    }

    // The default transform hands the coroutine's environment to each
    // ContextAwaitable it awaits, and reinstalls its frame allocator.
    void
    testDefaultTransformPassesEnvironment()
    {
        await_context const* seen = nullptr;
        int result = 0;
        auto c = [](await_context const** s, int* r) -> test_coro
        {
            *r = co_await recording_awaitable{s};
        }(&seen, &result);

        auto* const before = get_current_frame_allocator();
        await_context env;
        env.frame_allocator = std::pmr::null_memory_resource();
        c.h_.promise().set_environment(&env);
        set_current_frame_allocator(nullptr);
        c.h_.resume();
        auto* const reinstalled = get_current_frame_allocator();
        set_current_frame_allocator(before);

        BOOST_TEST(seen == &env);
        BOOST_TEST_EQ(result, 7);
        BOOST_TEST(c.h_.done());
        // Resuming from the awaitable reinstalled the environment's
        // frame allocator.
        BOOST_TEST(reinstalled == std::pmr::null_memory_resource());
    }

    void
    run()
    {
        testSetAndGetEnvironment();
        testDefaultTransformPassesEnvironment();
        testDefaultEnvironment();
        testAwaitTransformInterceptsEnvironmentTag();
        testAwaitTransformDelegatesToTransformAwaitable();
        testEnvironmentAwaiterNeverSuspends();
        testSetAndGetExecutorViaEnvironment();
    }
};

TEST_SUITE(
    context_awaitable_promise_base_test,
    "boost.capy.ex.context_awaitable_promise_base");

} // namespace capy
} // namespace boost
