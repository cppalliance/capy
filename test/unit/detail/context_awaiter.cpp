//
// Copyright (c) 2026 Steve Gerbino
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Test that header file is self-contained.
#include <boost/capy/detail/context_awaiter.hpp>

#include <boost/capy/ex/frame_allocator.hpp>

#include <coroutine>
#include <exception>
#include <memory_resource>
#include <tuple>
#include <type_traits>
#include <utility>

#include "test_suite.hpp"

namespace boost {
namespace capy {

namespace {

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

// Stands in for a promise: the adapter asks it for the environment.
struct env_source
{
    await_context const* env;

    await_context const* environment() const noexcept { return env; }
};

// A root that keeps its own environment and wraps every awaitable in
// the adapter, as a promise built on context_awaitable_promise_base
// does by default.
struct adapting_coro
{
    struct promise_type
    {
        await_context env_;

        await_context const* environment() const noexcept { return &env_; }

        adapting_coro get_return_object()
        {
            return {std::coroutine_handle<promise_type>::from_promise(*this)};
        }
        std::suspend_always initial_suspend() noexcept { return {}; }
        std::suspend_always final_suspend() noexcept { return {}; }
        void return_void() noexcept {}
        void unhandled_exception() noexcept { std::terminate(); }

        template<ContextAwaitable A>
        auto await_transform(A&& a)
        {
            return detail::context_awaiter<std::decay_t<A>, promise_type>{
                std::forward<A>(a), this};
        }
    };

    std::coroutine_handle<promise_type> h;
};

} // (anon)

struct context_awaiter_test
{
    // Resuming reinstalls the environment's frame allocator.
    void testResumeReinstallsAllocator()
    {
        await_context const* seen = nullptr;
        await_context env{};
        env.frame_allocator = std::pmr::null_memory_resource();
        env_source src{&env};
        detail::context_awaiter<recording_awaitable, env_source> aw{
            recording_awaitable{&seen}, &src};

        auto* const before = get_current_frame_allocator();
        set_current_frame_allocator(nullptr);
        BOOST_TEST_EQ(aw.await_resume(), 7);
        BOOST_TEST(
            get_current_frame_allocator() == std::pmr::null_memory_resource());
        set_current_frame_allocator(before);
    }

    // The environment is read when the awaiter is used, not when it is
    // built.
    void testReadsEnvironmentWhenUsed()
    {
        await_context const* seen = nullptr;
        await_context first{};
        await_context second{};
        second.frame_allocator = std::pmr::null_memory_resource();
        env_source src{&first};
        detail::context_awaiter<recording_awaitable, env_source> aw{
            recording_awaitable{&seen}, &src};

        src.env = &second;
        if(!aw.await_ready())
            std::ignore = aw.await_suspend(std::noop_coroutine());
        BOOST_TEST(seen == &second);

        auto* const before = get_current_frame_allocator();
        BOOST_TEST_EQ(aw.await_resume(), 7);
        BOOST_TEST(
            get_current_frame_allocator() == std::pmr::null_memory_resource());
        set_current_frame_allocator(before);
    }

    // The awaited operation receives the environment the adapter holds.
    void testForwardsEnvironment()
    {
        await_context const* seen = nullptr;
        int result = 0;
        auto body = [](await_context const** s, int* r) -> adapting_coro
        {
            *r = co_await recording_awaitable{s};
        };

        auto* const before = get_current_frame_allocator();
        auto c = body(&seen, &result);
        c.h.promise().env_.frame_allocator = before;
        c.h.resume();

        BOOST_TEST(seen == &c.h.promise().env_);
        BOOST_TEST_EQ(result, 7);
        BOOST_TEST(c.h.done());
        c.h.destroy();
        set_current_frame_allocator(before);
    }

    void
    run()
    {
        testResumeReinstallsAllocator();
        testReadsEnvironmentWhenUsed();
        testForwardsEnvironment();
    }
};

TEST_SUITE(
    context_awaiter_test,
    "boost.capy.detail.context_awaiter");

} // capy
} // boost
