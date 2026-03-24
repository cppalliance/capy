//
// Copyright (c) 2026 Michael Vandeberg
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Test that header file is self-contained.
#include <boost/capy/ex/frame_allocator.hpp>

#include "test_suite.hpp"

#include <coroutine>
#include <memory_resource>
#include <utility>

namespace boost {
namespace capy {

/*
safe_resume Tests
=================

Verifies that safe_resume saves and restores the thread-local
frame allocator around h.resume(). A plain coroutine (no
io_awaitable_promise_base) is used so that operator new does
not read TLS — we only care about the TLS value itself.
*/

namespace {

// Minimal coroutine that overwrites TLS with a known value, then suspends.
struct spoiler_coro
{
    struct promise_type
    {
        std::pmr::memory_resource* spoil_to = nullptr;

        spoiler_coro get_return_object()
        {
            return spoiler_coro{std::coroutine_handle<promise_type>::from_promise(*this)};
        }

        std::suspend_always initial_suspend() noexcept { return {}; }
        std::suspend_always final_suspend() noexcept { return {}; }
        void return_void() {}
        void unhandled_exception() {}
    };

    std::coroutine_handle<promise_type> h_;

    ~spoiler_coro()
    {
        if(h_)
            h_.destroy();
    }

    spoiler_coro(spoiler_coro&& o) noexcept
        : h_(std::exchange(o.h_, nullptr))
    {
    }

    spoiler_coro& operator=(spoiler_coro&&) = delete;
    spoiler_coro(spoiler_coro const&) = delete;

private:
    explicit spoiler_coro(std::coroutine_handle<promise_type> h)
        : h_(h)
    {
    }

    friend promise_type;
};

// Coroutine body: overwrite TLS and suspend.
spoiler_coro make_spoiler(std::pmr::memory_resource* spoil_to)
{
    set_current_frame_allocator(spoil_to);
    co_return;
}

// Coroutine body: overwrite TLS, then safe_resume another coroutine, then suspend.
spoiler_coro make_nested_spoiler(
    std::pmr::memory_resource* spoil_to,
    std::coroutine_handle<> inner)
{
    set_current_frame_allocator(spoil_to);
    safe_resume(inner);
    // After safe_resume, TLS should be spoil_to again
    co_return;
}

} // anonymous namespace

struct safe_resume_test
{
    void
    testSafeResumePreservesTLS()
    {
        auto* original = std::pmr::null_memory_resource();
        auto* spoil = std::pmr::new_delete_resource();

        auto coro = make_spoiler(spoil);

        set_current_frame_allocator(original);
        safe_resume(coro.h_);
        BOOST_TEST(get_current_frame_allocator() == original);

        set_current_frame_allocator(nullptr);
    }

    void
    testSafeResumePreservesNull()
    {
        auto* spoil = std::pmr::new_delete_resource();

        auto coro = make_spoiler(spoil);

        set_current_frame_allocator(nullptr);
        safe_resume(coro.h_);
        BOOST_TEST(get_current_frame_allocator() == nullptr);
    }

    void
    testRawResumeDoesNotPreserveTLS()
    {
        // Documents the problem that safe_resume fixes:
        // raw .resume() lets the coroutine spoil TLS.
        auto* original = std::pmr::null_memory_resource();
        auto* spoil = std::pmr::new_delete_resource();

        auto coro = make_spoiler(spoil);

        set_current_frame_allocator(original);
        coro.h_.resume();
        BOOST_TEST(get_current_frame_allocator() == spoil);

        set_current_frame_allocator(nullptr);
    }

    void
    testNestedSafeResume()
    {
        auto* outer_value = std::pmr::null_memory_resource();
        auto* middle_value = std::pmr::new_delete_resource();
        auto* inner_value = std::pmr::get_default_resource();

        auto inner = make_spoiler(inner_value);
        auto outer = make_nested_spoiler(middle_value, inner.h_);

        set_current_frame_allocator(outer_value);
        safe_resume(outer.h_);
        // Outer safe_resume should restore outer_value,
        // regardless of what happened inside.
        BOOST_TEST(get_current_frame_allocator() == outer_value);

        set_current_frame_allocator(nullptr);
    }

    void
    run()
    {
        testSafeResumePreservesTLS();
        testSafeResumePreservesNull();
        testRawResumeDoesNotPreserveTLS();
        testNestedSafeResume();
    }
};

TEST_SUITE(safe_resume_test, "capy.safe_resume");

} // capy
} // boost
