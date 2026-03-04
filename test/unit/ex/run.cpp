//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Test that header file is self-contained.
#include <boost/capy/ex/run.hpp>

#include <boost/capy/concept/io_awaitable.hpp>
#include <boost/capy/ex/executor_ref.hpp>
#include <boost/capy/ex/run_async.hpp>
#include <boost/capy/ex/this_coro.hpp>
#include <boost/capy/task.hpp>

#include "test/unit/custom_task.hpp"
#include "test/unit/test_helpers.hpp"

#include <memory>

namespace boost {
namespace capy {

static_assert(IoAwaitable<detail::run_awaitable_ex<task<void>, executor_ref, true>>);
static_assert(IoAwaitable<detail::run_awaitable_ex<task<int>, executor_ref, true>>);
static_assert(IoAwaitable<detail::run_awaitable<task<void>, true>>);
static_assert(IoAwaitable<detail::run_awaitable<task<int>, true>>);

using test::custom_task;

// Test allocator that wraps std::allocator
template<class T>
struct test_allocator
{
    using value_type = T;

    test_allocator() = default;

    template<class U>
    test_allocator(test_allocator<U> const&) noexcept {}

    T* allocate(std::size_t n)
    {
        return std::allocator<T>{}.allocate(n);
    }

    void deallocate(T* p, std::size_t n)
    {
        std::allocator<T>{}.deallocate(p, n);
    }
};

//----------------------------------------------------------
// run Tests
//----------------------------------------------------------

struct run_test
{
    static custom_task<int>
    custom_returns_int()
    {
        co_return 456;
    }

    static custom_task<void>
    custom_returns_void()
    {
        co_return;
    }

    static task<int>
    returns_int()
    {
        co_return 123;
    }

    static task<void>
    returns_void()
    {
        co_return;
    }

    void
    testCustomTaskType()
    {
        // Proves run works with any IoRunnable, not just capy::task
        int dispatch_count = 0;
        test_executor ex(1, dispatch_count);
        int result = 0;

        auto outer = [&]() -> task<int> {
            co_return co_await capy::run(ex)(custom_returns_int());
        };

        run_async(ex, [&](int v) { result = v; })(outer());

        BOOST_TEST_EQ(result, 456);
    }

    void
    testCustomTaskTypeVoid()
    {
        // Proves run works with void custom tasks
        int dispatch_count = 0;
        test_executor ex(1, dispatch_count);
        bool called = false;

        auto outer = [&]() -> task<void> {
            co_await capy::run(ex)(custom_returns_void());
        };

        run_async(ex, [&]() { called = true; })(outer());

        BOOST_TEST(called);
    }

    void
    testExWithStopToken()
    {
        // Test run(ex, st)
        int dispatch_count = 0;
        test_executor ex(1, dispatch_count);
        std::stop_source source;
        int result = 0;

        auto outer = [&]() -> task<int> {
            co_return co_await capy::run(ex, source.get_token())(returns_int());
        };

        run_async(ex, [&](int v) { result = v; })(outer());

        BOOST_TEST_EQ(result, 123);
    }

    void
    testStopTokenOnly()
    {
        // Test run(st) - no executor, just stop_token
        int dispatch_count = 0;
        test_executor ex(1, dispatch_count);
        std::stop_source source;
        int result = 0;

        auto outer = [&]() -> task<int> {
            co_return co_await capy::run(source.get_token())(returns_int());
        };

        run_async(ex, [&](int v) { result = v; })(outer());

        BOOST_TEST_EQ(result, 123);
    }

    void
    testMemoryResourceOnly()
    {
        // Test run(mr) - no executor, just memory_resource
        int dispatch_count = 0;
        test_executor ex(1, dispatch_count);
        int result = 0;
        auto* mr = std::pmr::get_default_resource();

        auto outer = [&]() -> task<int> {
            co_return co_await capy::run(mr)(returns_int());
        };

        run_async(ex, [&](int v) { result = v; })(outer());

        BOOST_TEST_EQ(result, 123);
    }

    void
    testExWithMemoryResource()
    {
        // Test run(ex, mr)
        int dispatch_count = 0;
        test_executor ex(1, dispatch_count);
        int result = 0;
        auto* mr = std::pmr::get_default_resource();

        auto outer = [&]() -> task<int> {
            co_return co_await capy::run(ex, mr)(returns_int());
        };

        run_async(ex, [&](int v) { result = v; })(outer());

        BOOST_TEST_EQ(result, 123);
    }

    void
    testExWithStopTokenAndMemoryResource()
    {
        // Test run(ex, st, mr)
        int dispatch_count = 0;
        test_executor ex(1, dispatch_count);
        std::stop_source source;
        int result = 0;
        auto* mr = std::pmr::get_default_resource();

        auto outer = [&]() -> task<int> {
            co_return co_await capy::run(ex, source.get_token(), mr)(returns_int());
        };

        run_async(ex, [&](int v) { result = v; })(outer());

        BOOST_TEST_EQ(result, 123);
    }

    void
    testStopTokenWithMemoryResource()
    {
        // Test run(st, mr)
        int dispatch_count = 0;
        test_executor ex(1, dispatch_count);
        std::stop_source source;
        int result = 0;
        auto* mr = std::pmr::get_default_resource();

        auto outer = [&]() -> task<int> {
            co_return co_await capy::run(source.get_token(), mr)(returns_int());
        };

        run_async(ex, [&](int v) { result = v; })(outer());

        BOOST_TEST_EQ(result, 123);
    }

    void
    testExWithAllocator()
    {
        // Test run(ex, alloc) - standard allocator with executor
        int dispatch_count = 0;
        test_executor ex(1, dispatch_count);
        int result = 0;
        test_allocator<std::byte> alloc;

        auto outer = [&]() -> task<int> {
            co_return co_await capy::run(ex, alloc)(returns_int());
        };

        run_async(ex, [&](int v) { result = v; })(outer());

        BOOST_TEST_EQ(result, 123);
    }

    void
    testExWithStopTokenAndAllocator()
    {
        // Test run(ex, st, alloc) - executor + stop token + standard allocator
        int dispatch_count = 0;
        test_executor ex(1, dispatch_count);
        std::stop_source source;
        int result = 0;
        test_allocator<std::byte> alloc;

        auto outer = [&]() -> task<int> {
            co_return co_await capy::run(ex, source.get_token(), alloc)(returns_int());
        };

        run_async(ex, [&](int v) { result = v; })(outer());

        BOOST_TEST_EQ(result, 123);
    }

    void
    testAllocatorOnly()
    {
        // Test run(alloc) - standard allocator only (no executor)
        int dispatch_count = 0;
        test_executor ex(1, dispatch_count);
        int result = 0;
        test_allocator<std::byte> alloc;

        auto outer = [&]() -> task<int> {
            co_return co_await capy::run(alloc)(returns_int());
        };

        run_async(ex, [&](int v) { result = v; })(outer());

        BOOST_TEST_EQ(result, 123);
    }

    void
    testStopTokenWithAllocator()
    {
        // Test run(st, alloc) - stop token + standard allocator (no executor)
        int dispatch_count = 0;
        test_executor ex(1, dispatch_count);
        std::stop_source source;
        int result = 0;
        test_allocator<std::byte> alloc;

        auto outer = [&]() -> task<int> {
            co_return co_await capy::run(source.get_token(), alloc)(returns_int());
        };

        run_async(ex, [&](int v) { result = v; })(outer());

        BOOST_TEST_EQ(result, 123);
    }

    void
    testVoidWithStopToken()
    {
        // Test run(ex, st) with void return type
        int dispatch_count = 0;
        test_executor ex(1, dispatch_count);
        std::stop_source source;
        bool called = false;

        auto outer = [&]() -> task<void> {
            co_await capy::run(ex, source.get_token())(returns_void());
        };

        run_async(ex, [&]() { called = true; })(outer());

        BOOST_TEST(called);
    }

    void
    testVoidWithMemoryResource()
    {
        // Test run(mr) with void return type
        int dispatch_count = 0;
        test_executor ex(1, dispatch_count);
        bool called = false;
        auto* mr = std::pmr::get_default_resource();

        auto outer = [&]() -> task<void> {
            co_await capy::run(mr)(returns_void());
        };

        run_async(ex, [&]() { called = true; })(outer());

        BOOST_TEST(called);
    }

    //----------------------------------------------------------
    // Stop Token Propagation
    //----------------------------------------------------------

    static task<bool>
    check_stop_requested()
    {
        auto token = co_await this_coro::stop_token;
        co_return token.stop_requested();
    }

    void
    testStopTokenInheritance()
    {
        // Verify run(ex) inherits the caller's stop token
        int dispatch_count = 0;
        test_executor ex(1, dispatch_count);
        std::stop_source source;
        source.request_stop();
        bool result = false;

        auto outer = [&]() -> task<bool> {
            // run(ex) with no explicit stop token should inherit
            // the caller's token (which is stopped)
            co_return co_await capy::run(ex)(check_stop_requested());
        };

        run_async(ex, source.get_token(),
            [&](bool v) { result = v; })(outer());

        BOOST_TEST(result);
    }

    void
    testStopTokenOverrideInnerStopped()
    {
        // Stop the inner (override) token only.
        // Inner task should see stopped; outer should not.
        int dispatch_count = 0;
        test_executor ex(1, dispatch_count);
        std::stop_source caller_source;
        std::stop_source override_source;
        override_source.request_stop();

        bool outer_stopped = true;
        bool inner_stopped = false;

        auto outer = [&]() -> task<void> {
            auto token = co_await this_coro::stop_token;
            outer_stopped = token.stop_requested();
            inner_stopped = co_await capy::run(ex, override_source.get_token())(
                check_stop_requested());
        };

        run_async(ex, caller_source.get_token())(outer());

        BOOST_TEST(!outer_stopped);
        BOOST_TEST(inner_stopped);
    }

    void
    testStopTokenOverrideOuterStopped()
    {
        // Stop the outer (caller) token only.
        // Outer task should see stopped; inner (override) should not.
        int dispatch_count = 0;
        test_executor ex(1, dispatch_count);
        std::stop_source caller_source;
        caller_source.request_stop();
        std::stop_source override_source;

        bool outer_stopped = false;
        bool inner_stopped = true;

        auto outer = [&]() -> task<void> {
            auto token = co_await this_coro::stop_token;
            outer_stopped = token.stop_requested();
            inner_stopped = co_await capy::run(ex, override_source.get_token())(
                check_stop_requested());
        };

        run_async(ex, caller_source.get_token())(outer());

        BOOST_TEST(outer_stopped);
        BOOST_TEST(!inner_stopped);
    }

    //----------------------------------------------------------
    // Allocator Propagation
    //----------------------------------------------------------

    void
    testAllocatorPropagation()
    {
        // Verify allocator is non-null inside task launched via run_async
        int dispatch_count = 0;
        test_executor ex(1, dispatch_count);
        bool result = false;

        auto check = []() -> task<bool> {
            auto* alloc = co_await this_coro::frame_allocator;
            co_return alloc != nullptr;
        };

        run_async(ex, [&](bool v) { result = v; })(check());

        BOOST_TEST(result);
    }

    void
    testAllocatorPropagationThroughRun()
    {
        // Verify allocator propagates through run(ex) to inner task
        int dispatch_count = 0;
        test_executor ex(1, dispatch_count);
        bool result = false;

        auto inner = []() -> task<bool> {
            auto* alloc = co_await this_coro::frame_allocator;
            co_return alloc != nullptr;
        };

        auto outer = [&]() -> task<bool> {
            co_return co_await capy::run(ex)(inner());
        };

        run_async(ex, [&](bool v) { result = v; })(outer());

        BOOST_TEST(result);
    }

    void
    run()
    {
        testCustomTaskType();
        testCustomTaskTypeVoid();
        testExWithStopToken();
        testStopTokenOnly();
        testMemoryResourceOnly();
        testExWithMemoryResource();
        testExWithStopTokenAndMemoryResource();
        testStopTokenWithMemoryResource();
        testExWithAllocator();
        testExWithStopTokenAndAllocator();
        testAllocatorOnly();
        testStopTokenWithAllocator();
        testVoidWithStopToken();
        testVoidWithMemoryResource();
        testStopTokenInheritance();
        testStopTokenOverrideInnerStopped();
        testStopTokenOverrideOuterStopped();
        testAllocatorPropagation();
        testAllocatorPropagationThroughRun();
    }
};

TEST_SUITE(
    run_test,
    "boost.capy.ex.run");

} // capy
} // boost
