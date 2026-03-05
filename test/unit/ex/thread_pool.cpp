//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
// Copyright (c) 2026 Michael Vandeberg
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Test that header file is self-contained.
#include <boost/capy/ex/thread_pool.hpp>

#include <boost/capy/concept/execution_context.hpp>
#include <boost/capy/concept/executor.hpp>
#include <boost/capy/ex/run_async.hpp>
#include <boost/capy/ex/work_guard.hpp>

#include "test_helpers.hpp"

#include <atomic>
#include <thread>
#include <vector>

namespace boost {
namespace capy {

namespace {

// Verify concepts at compile time
static_assert(Executor<thread_pool::executor_type>,
    "thread_pool::executor_type must satisfy Executor concept");
static_assert(ExecutionContext<thread_pool>,
    "thread_pool must satisfy ExecutionContext concept");

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

#if defined(BOOST_CAPY_TEST_CAN_GET_THREAD_NAME)
// Result storage for thread name check
struct name_check_result
{
    std::atomic<bool> done{false};
    std::atomic<bool> matches{false};
};

// Coroutine that checks thread name when resumed on pool thread.
// Arguments are forwarded to promise_type constructor.
struct name_checker
{
    struct promise_type
    {
        name_check_result& result;
        char const* prefix;

        promise_type(name_check_result& r, char const* p)
            : result(r), prefix(p) {}

        name_checker get_return_object()
        {
            return {std::coroutine_handle<promise_type>::from_promise(*this)};
        }
        std::suspend_always initial_suspend() noexcept { return {}; }
        std::suspend_never final_suspend() noexcept
        {
            result.matches.store(thread_name_starts_with(prefix));
            result.done.store(true);
            return {};
        }
        void return_void() {}
        void unhandled_exception() {}
    };

    std::coroutine_handle<promise_type> h;
    operator std::coroutine_handle<>() const { return h; }
};

name_checker check_thread_name(name_check_result& result, char const* prefix)
{
    // Parameters forwarded to promise_type constructor, not used here.
    (void)result;
    (void)prefix;
    co_return;
}
#endif

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

        // dispatch() always posts for thread_pool (returns void)
        ex.dispatch(std::noop_coroutine());
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
    testThreadNaming()
    {
        // Test custom naming prefix (construction only)
        {
            thread_pool pool(2, "test-worker-");
            (void)pool.get_executor();
        }

        // Test empty prefix
        {
            thread_pool pool(1, "");
            (void)pool.get_executor();
        }

#if defined(BOOST_CAPY_TEST_CAN_GET_THREAD_NAME)
        // Verify default thread name from within pool thread
        {
            thread_pool pool(1);
            name_check_result result;
            pool.get_executor().post(check_thread_name(result, "capy-pool-"));

            BOOST_TEST(wait_for([&]{ return result.done.load(); }));
            BOOST_TEST(result.matches.load());
        }

        // Verify custom thread name from within pool thread
        {
            thread_pool pool(1, "mypool-");
            name_check_result result;
            pool.get_executor().post(check_thread_name(result, "mypool-"));

            BOOST_TEST(wait_for([&]{ return result.done.load(); }));
            BOOST_TEST(result.matches.load());
        }

        // Verify thread naming works with index suffix
        {
            thread_pool pool(1, "idx-");
            name_check_result result;
            pool.get_executor().post(check_thread_name(result, "idx-0"));

            BOOST_TEST(wait_for([&]{ return result.done.load(); }));
            BOOST_TEST(result.matches.load());
        }
#endif
    }

    void
    testJoinDrainsWork()
    {
        thread_pool pool(2);
        auto ex = pool.get_executor();
        std::atomic<int> count{0};

        constexpr int N = 50;
        for(int i = 0; i < N; ++i)
        {
            run_async(ex,
                [&]{ count.fetch_add(1); }
            )(void_task());
        }

        pool.join();
        BOOST_TEST_EQ(count.load(), N);
    }

    void
    testJoinNoWork()
    {
        // join() on a pool with no posted work returns promptly
        thread_pool pool(2);
        pool.join();
    }

    void
    testJoinNoThreadsStarted()
    {
        // join() without ever posting (lazy start never triggered)
        thread_pool pool(2);
        // Don't call get_executor() or post anything
        pool.join();
    }

    void
    testJoinIdempotent()
    {
        thread_pool pool(1);
        pool.join();
        pool.join();  // second call should be a no-op
    }

    void
    testStopThenJoin()
    {
        thread_pool pool(2);
        pool.stop();
        pool.join();  // should return immediately
    }

    void
    testStopInterruptsJoin()
    {
        thread_pool pool(2);
        auto ex = pool.get_executor();

        // Hold work guard to keep join() blocking
        auto guard = make_work_guard(ex);

        std::atomic<bool> join_returned{false};
        std::thread joiner([&]{
            pool.join();
            join_returned.store(true);
        });

        // Give join() time to block
        std::this_thread::sleep_for(
            std::chrono::milliseconds(50));
        BOOST_TEST(!join_returned.load());

        // stop() should interrupt the blocking join()
        pool.stop();

        joiner.join();
        BOOST_TEST(join_returned.load());
    }

    void
    testDestructorAbandonsPending()
    {
        // Verify the destructor doesn't hang when work items
        // are genuinely queued but unprocessed. We block the
        // single worker thread with a spinning callback, then
        // post items that pile up in the queue. After releasing
        // the worker, the destructor's stop() causes it to exit
        // without draining the queue.
        {
            std::atomic<bool> busy{false};
            std::atomic<bool> release{false};

            thread_pool pool(1);
            auto ex = pool.get_executor();

            // Block the worker via run_async callback
            run_async(ex, [&]{
                busy.store(true);
                while(!release.load())
                    std::this_thread::yield();
            })(void_task());

            // Wait until worker is executing our callback
            while(!busy.load())
                std::this_thread::yield();

            // Queue items that can't be processed yet
            for(int i = 0; i < 50; ++i)
                ex.post(std::noop_coroutine());

            // Release worker, then pool destructs immediately.
            // stop() races with the worker — pending items
            // are abandoned and destroyed by ~impl().
            release.store(true);
        }
    }

    void
    testStopCallbackPostBack()
    {
        // Cancel a suspended task via stop_token, then let the
        // pool destruct. stop_only_awaitable uses resume_via_post
        // so the coroutine resumes on a pool thread, not on the
        // thread that calls request_stop().
        {
            thread_pool pool(1);
            auto ex = pool.get_executor();
            std::stop_source ss;

            auto make_task = []() -> task<void> {
                co_await stop_only_awaitable{};
            };

            run_async(ex, ss.get_token())(make_task());

            std::this_thread::sleep_for(
                std::chrono::milliseconds(50));

            ss.request_stop();
        }
    }

    void
    testStopCallbackWithJoin()
    {
        // Cancel a suspended task, then join() the pool.
        // Verifies work counting and join() interact correctly
        // with stop_callback cancellation.
        {
            thread_pool pool(1);
            auto ex = pool.get_executor();
            std::stop_source ss;

            auto make_task = []() -> task<void> {
                co_await stop_only_awaitable{};
            };

            run_async(ex, ss.get_token())(make_task());

            std::this_thread::sleep_for(
                std::chrono::milliseconds(50));

            ss.request_stop();
            pool.join();
        }
    }

    void
    testStopCallbackRepeated()
    {
        // Stress test: repeated cancel + pool destruction cycles.
        for(int iter = 0; iter < 50; ++iter)
        {
            thread_pool pool(2);
            auto ex = pool.get_executor();
            std::stop_source ss;

            auto make_task = []() -> task<void> {
                co_await stop_only_awaitable{};
            };

            for(int i = 0; i < 5; ++i)
                run_async(ex, ss.get_token())(make_task());

            std::this_thread::sleep_for(
                std::chrono::milliseconds(10));

            ss.request_stop();
        }
    }

    void
    testWorkGuardKeepsPoolAlive()
    {
        thread_pool pool(1);
        auto ex = pool.get_executor();
        std::atomic<bool> join_returned{false};

        auto guard = make_work_guard(ex);

        std::thread joiner([&]{
            pool.join();
            join_returned.store(true);
        });

        // Give join() time to block
        std::this_thread::sleep_for(
            std::chrono::milliseconds(50));
        BOOST_TEST(!join_returned.load());

        // Releasing the guard should allow join() to complete
        guard.reset();

        joiner.join();
        BOOST_TEST(join_returned.load());
    }

    void
    testJoinWithRunAsync()
    {
        thread_pool pool(2);
        auto ex = pool.get_executor();
        std::atomic<int> result{0};

        run_async(ex,
            [&](int v){ result.store(v); }
        )(returns_int(42));

        pool.join();
        BOOST_TEST_EQ(result.load(), 42);
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
        testThreadNaming();
        testJoinDrainsWork();
        testJoinNoWork();
        testJoinNoThreadsStarted();
        testJoinIdempotent();
        testStopThenJoin();
        testStopInterruptsJoin();
        testDestructorAbandonsPending();
        testStopCallbackPostBack();
        testStopCallbackWithJoin();
        testStopCallbackRepeated();
        testWorkGuardKeepsPoolAlive();
        testJoinWithRunAsync();
    }
};

TEST_SUITE(
    thread_pool_test,
    "boost.capy.thread_pool");

} // capy
} // boost
