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
#include <boost/capy/ex/thread_pool.hpp>

#include "test_suite.hpp"

#include <coroutine>
#include <utility>

namespace boost {
namespace capy {

struct tag_test_coro
{
    struct promise_type : io_awaitable_promise_base<promise_type>
    {
        tag_test_coro get_return_object()
        {
            return tag_test_coro{
                std::coroutine_handle<promise_type>::from_promise(*this)};
        }

        std::suspend_always initial_suspend() noexcept { return {}; }
        std::suspend_always final_suspend() noexcept { return {}; }
        void return_void() {}
        void unhandled_exception() {}
    };

    std::coroutine_handle<promise_type> h_;

    ~tag_test_coro()
    {
        if(h_)
            h_.destroy();
    }

    tag_test_coro(tag_test_coro const&) = delete;
    tag_test_coro& operator=(tag_test_coro const&) = delete;

    tag_test_coro(tag_test_coro&& other) noexcept
        : h_(std::exchange(other.h_, nullptr))
    {
    }

private:
    explicit tag_test_coro(std::coroutine_handle<promise_type> h)
        : h_(h)
    {
    }
};

struct this_coro_tags_test
{
    void
    testEnvironmentTagType()
    {
        this_coro::environment_tag tag1;
        this_coro::environment_tag tag2{};
        (void)tag1;
        (void)tag2;

        static_assert(std::is_trivially_copyable_v<this_coro::environment_tag>);
    }

    void
    testEnvironmentConstant()
    {
        auto tag = this_coro::environment;
        static_assert(std::is_same_v<
            decltype(this_coro::environment), this_coro::environment_tag const>);
        (void)tag;
    }

    void
    testExecutorTagType()
    {
        this_coro::executor_tag tag1;
        this_coro::executor_tag tag2{};
        (void)tag1;
        (void)tag2;

        static_assert(std::is_trivially_copyable_v<this_coro::executor_tag>);
    }

    void
    testExecutorConstant()
    {
        auto tag = this_coro::executor;
        static_assert(std::is_same_v<
            decltype(this_coro::executor), this_coro::executor_tag const>);
        (void)tag;
    }

    void
    testStopTokenTagType()
    {
        this_coro::stop_token_tag tag1;
        this_coro::stop_token_tag tag2{};
        (void)tag1;
        (void)tag2;

        static_assert(std::is_trivially_copyable_v<this_coro::stop_token_tag>);
    }

    void
    testStopTokenConstant()
    {
        auto tag = this_coro::stop_token;
        static_assert(std::is_same_v<
            decltype(this_coro::stop_token), this_coro::stop_token_tag const>);
        (void)tag;
    }

    void
    testAllocatorTagType()
    {
        this_coro::allocator_tag tag1;
        this_coro::allocator_tag tag2{};
        (void)tag1;
        (void)tag2;

        static_assert(std::is_trivially_copyable_v<this_coro::allocator_tag>);
    }

    void
    testAllocatorConstant()
    {
        auto tag = this_coro::allocator;
        static_assert(std::is_same_v<
            decltype(this_coro::allocator), this_coro::allocator_tag const>);
        (void)tag;
    }

    void
    testAwaitTransformExecutor()
    {
        thread_pool pool(1);
        auto executor = pool.get_executor();

        auto c = []() -> tag_test_coro { co_return; }();
        io_env env;
        env.executor = executor_ref(executor);
        c.h_.promise().set_environment(&env);

        auto awaiter = c.h_.promise().await_transform(this_coro::executor);

        BOOST_TEST(awaiter.await_ready());

        auto ex = awaiter.await_resume();
        static_assert(std::is_same_v<decltype(ex), executor_ref>);
        BOOST_TEST(static_cast<bool>(ex));
        BOOST_TEST(ex == executor_ref(executor));
    }

    void
    testAwaitTransformStopToken()
    {
        auto c = []() -> tag_test_coro { co_return; }();

        std::stop_source source;
        io_env env;
        env.stop_token = source.get_token();
        c.h_.promise().set_environment(&env);

        auto awaiter = c.h_.promise().await_transform(this_coro::stop_token);

        BOOST_TEST(awaiter.await_ready());

        auto token = awaiter.await_resume();
        static_assert(std::is_same_v<decltype(token), std::stop_token>);
        BOOST_TEST(token.stop_possible());
        BOOST_TEST(!token.stop_requested());

        source.request_stop();
        // The returned token is a copy, so it still reflects the source
        BOOST_TEST(token.stop_requested());
    }

    void
    testAwaitTransformAllocator()
    {
        auto c = []() -> tag_test_coro { co_return; }();

        auto* mr = std::pmr::new_delete_resource();
        io_env env;
        env.allocator = mr;
        c.h_.promise().set_environment(&env);

        auto awaiter = c.h_.promise().await_transform(this_coro::allocator);

        BOOST_TEST(awaiter.await_ready());

        auto* alloc = awaiter.await_resume();
        static_assert(std::is_same_v<decltype(alloc), std::pmr::memory_resource*>);
        BOOST_TEST(alloc == mr);
    }

    void
    testAwaitTransformAllocatorDefaultIsNull()
    {
        auto c = []() -> tag_test_coro { co_return; }();

        auto awaiter = c.h_.promise().await_transform(this_coro::allocator);
        auto* alloc = awaiter.await_resume();
        BOOST_TEST(alloc == nullptr);
    }

    void
    run()
    {
        testEnvironmentTagType();
        testEnvironmentConstant();
        testExecutorTagType();
        testExecutorConstant();
        testStopTokenTagType();
        testStopTokenConstant();
        testAllocatorTagType();
        testAllocatorConstant();
        testAwaitTransformExecutor();
        testAwaitTransformStopToken();
        testAwaitTransformAllocator();
        testAwaitTransformAllocatorDefaultIsNull();
    }
};

TEST_SUITE(
    this_coro_tags_test,
    "boost.capy.ex.this_coro.tags");

} // namespace capy
} // namespace boost
