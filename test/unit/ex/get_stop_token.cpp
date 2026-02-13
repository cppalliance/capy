//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Test that header file is self-contained.
#include <boost/capy/ex/this_coro.hpp>

#include <boost/capy/ex/io_awaitable_promise_base.hpp>
#include <boost/capy/ex/io_env.hpp>

#include "test_suite.hpp"

#include <coroutine>
#include <memory_resource>
#include <utility>

namespace boost {
namespace capy {

struct env_test_coro
{
    struct promise_type : io_awaitable_promise_base<promise_type>
    {
        env_test_coro get_return_object()
        {
            return env_test_coro{
                std::coroutine_handle<promise_type>::from_promise(*this)};
        }

        std::suspend_always initial_suspend() noexcept { return {}; }
        std::suspend_always final_suspend() noexcept { return {}; }
        void return_void() {}
        void unhandled_exception() {}
    };

    std::coroutine_handle<promise_type> h_;

    ~env_test_coro()
    {
        if(h_)
            h_.destroy();
    }

    env_test_coro(env_test_coro const&) = delete;
    env_test_coro& operator=(env_test_coro const&) = delete;

    env_test_coro(env_test_coro&& other) noexcept
        : h_(std::exchange(other.h_, nullptr))
    {
    }

private:
    explicit env_test_coro(std::coroutine_handle<promise_type> h)
        : h_(h)
    {
    }
};

struct this_coro_environment_access_test
{
    void
    testAwaitTransformReturnsEnvRef()
    {
        auto c = []() -> env_test_coro { co_return; }();

        io_env env;
        c.h_.promise().set_environment(&env);

        auto awaiter = c.h_.promise().await_transform(this_coro::environment);

        BOOST_TEST(awaiter.await_ready());

        auto retrieved = awaiter.await_resume();
        static_assert(std::is_same_v<decltype(retrieved), io_env const*>);
        (void)retrieved;
    }

    void
    testEnvironmentContainsSetValues()
    {
        auto c = []() -> env_test_coro { co_return; }();

        std::stop_source source;
        io_env env;
        env.stop_token = source.get_token();

        c.h_.promise().set_environment(&env);

        auto awaiter = c.h_.promise().await_transform(this_coro::environment);
        auto retrieved = awaiter.await_resume();

        BOOST_TEST(retrieved->stop_token.stop_possible());
        BOOST_TEST(!retrieved->stop_token.stop_requested());

        source.request_stop();
        BOOST_TEST(retrieved->stop_token.stop_requested());
    }

    void
    testEnvironmentAwaiterNeverSuspends()
    {
        auto c = []() -> env_test_coro { co_return; }();
        io_env env;
        c.h_.promise().set_environment(&env);
        auto awaiter = c.h_.promise().await_transform(this_coro::environment);

        BOOST_TEST(awaiter.await_ready());
        awaiter.await_suspend(c.h_);
    }

    void
    testDirectStopTokenAccessor()
    {
        auto c = []() -> env_test_coro { co_return; }();

        std::stop_source source;
        io_env env;
        env.stop_token = source.get_token();
        c.h_.promise().set_environment(&env);

        auto awaiter = c.h_.promise().await_transform(this_coro::stop_token);
        BOOST_TEST(awaiter.await_ready());

        auto token = awaiter.await_resume();
        BOOST_TEST(token.stop_possible());
        BOOST_TEST(!token.stop_requested());

        source.request_stop();
        BOOST_TEST(token.stop_requested());
    }

    void
    testDirectExecutorAccessor()
    {
        auto c = []() -> env_test_coro { co_return; }();

        io_env env;
        c.h_.promise().set_environment(&env);

        auto awaiter = c.h_.promise().await_transform(this_coro::executor);
        BOOST_TEST(awaiter.await_ready());

        auto ex = awaiter.await_resume();
        BOOST_TEST(!static_cast<bool>(ex));
    }

    void
    testDirectAllocatorAccessor()
    {
        auto c = []() -> env_test_coro { co_return; }();

        auto* mr = std::pmr::new_delete_resource();
        io_env env;
        env.allocator = mr;
        c.h_.promise().set_environment(&env);

        auto awaiter = c.h_.promise().await_transform(this_coro::allocator);
        BOOST_TEST(awaiter.await_ready());

        auto* alloc = awaiter.await_resume();
        BOOST_TEST(alloc == mr);
    }

    void
    run()
    {
        testAwaitTransformReturnsEnvRef();
        testEnvironmentContainsSetValues();
        testEnvironmentAwaiterNeverSuspends();
        testDirectStopTokenAccessor();
        testDirectExecutorAccessor();
        testDirectAllocatorAccessor();
    }
};

TEST_SUITE(
    this_coro_environment_access_test,
    "boost.capy.ex.this_coro.environment_access");

} // namespace capy
} // namespace boost
