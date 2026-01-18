//
// Copyright (c) 2025 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Test that header file is self-contained.
#include <boost/capy/ex/thread_pool.hpp>

#include <boost/capy/concept/executor.hpp>

#include "test_suite.hpp"

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

namespace boost {
namespace capy {

namespace {

// Verify Executor concept at compile time
static_assert(Executor<thread_pool::executor_type>,
    "thread_pool::executor_type must satisfy Executor concept");

// Simple service for testing inherited functionality
struct test_service : execution_context::service
{
    int value = 0;

    explicit test_service(execution_context&) {}

    test_service(execution_context&, int v)
        : value(v)
    {
    }

    void shutdown() override {}
};

// Helper to wait for a condition with timeout
template<class Pred>
bool wait_for(Pred pred, std::chrono::milliseconds timeout = std::chrono::milliseconds(5000))
{
    auto start = std::chrono::steady_clock::now();
    while(!pred())
    {
        if(std::chrono::steady_clock::now() - start > timeout)
            return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return true;
}

} // namespace

struct thread_pool_test
{
    void
    testConstruct()
    {
        // Default construction (hardware concurrency)
        {
            thread_pool pool;
        }

        // Explicit thread count
        {
            thread_pool pool(2);
        }

        // Single thread
        {
            thread_pool pool(1);
        }
    }

    void
    testGetExecutor()
    {
        thread_pool pool(1);
        auto ex = pool.get_executor();

        // Multiple calls return equal executors
        auto ex2 = pool.get_executor();
        BOOST_TEST(ex == ex2);
    }

    void
    testContext()
    {
        thread_pool pool(1);
        auto ex = pool.get_executor();

        // context() returns reference to owning thread_pool
        BOOST_TEST_EQ(&ex.context(), &pool);
    }

    void
    testExecutorEquality()
    {
        thread_pool pool1(1);
        thread_pool pool2(1);

        auto ex1a = pool1.get_executor();
        auto ex1b = pool1.get_executor();
        auto ex2 = pool2.get_executor();

        // Same pool = equal
        BOOST_TEST(ex1a == ex1b);

        // Different pools = not equal
        BOOST_TEST(!(ex1a == ex2));
    }

    void
    testPostWork()
    {
        thread_pool pool(1);
        auto ex = pool.get_executor();

        // Post a noop coroutine and verify no exceptions
        ex.post(std::noop_coroutine());

        // Basic test: pool constructs and destructs without issue
        (void)ex;
    }

    void
    testWorkCounting()
    {
        thread_pool pool(1);
        auto ex = pool.get_executor();

        // Work counting should not throw
        ex.on_work_started();
        ex.on_work_started();
        ex.on_work_finished();
        ex.on_work_finished();
    }

    void
    testDispatch()
    {
        thread_pool pool(1);
        auto ex = pool.get_executor();

        // dispatch() returns noop_coroutine (always posts for thread_pool)
        auto result = ex.dispatch(std::noop_coroutine());
        BOOST_TEST(result == std::noop_coroutine());
    }

    void
    testServiceManagement()
    {
        // thread_pool inherits service management from execution_context
        thread_pool pool(1);

        // Initially no services
        BOOST_TEST(!pool.has_service<test_service>());
        BOOST_TEST_EQ(pool.find_service<test_service>(), nullptr);

        // use_service creates if not present
        auto& svc = pool.use_service<test_service>();
        BOOST_TEST(pool.has_service<test_service>());

        // Returns same instance
        auto& svc2 = pool.use_service<test_service>();
        BOOST_TEST_EQ(&svc, &svc2);
    }

    void
    testMakeService()
    {
        thread_pool pool(1);

        // make_service with arguments
        auto& svc = pool.make_service<test_service>(42);
        BOOST_TEST_EQ(svc.value, 42);

        // Duplicate throws
        BOOST_TEST_THROWS(
            pool.make_service<test_service>(100),
            std::invalid_argument);

        // Original value unchanged
        BOOST_TEST_EQ(pool.find_service<test_service>()->value, 42);
    }

    void
    testConcurrentPost()
    {
        thread_pool pool(4);
        auto ex = pool.get_executor();

        constexpr int num_threads = 8;
        std::atomic<int> post_count{0};

        std::vector<std::thread> threads;
        threads.reserve(num_threads);

        for(int i = 0; i < num_threads; ++i)
        {
            threads.emplace_back([&ex, &post_count]{
                // Multiple threads posting concurrently
                for(int j = 0; j < 10; ++j)
                {
                    ex.post(std::noop_coroutine());
                    ++post_count;
                }
            });
        }

        for(auto& t : threads)
            t.join();

        // All posts should complete without issue
        BOOST_TEST_EQ(post_count.load(), num_threads * 10);
    }

    void
    testDefaultExecutor()
    {
        // Default-constructed executor
        thread_pool::executor_type ex;

        // Should be in a valid but unassociated state
        // (calling context() on it would be UB, so we don't test that)
        (void)ex;
    }

    void
    run()
    {
        testConstruct();
        testGetExecutor();
        testContext();
        testExecutorEquality();
        testPostWork();
        testWorkCounting();
        testDispatch();
        testServiceManagement();
        testMakeService();
        testConcurrentPost();
        testDefaultExecutor();
    }
};

TEST_SUITE(
    thread_pool_test,
    "boost.capy.thread_pool");

} // capy
} // boost
