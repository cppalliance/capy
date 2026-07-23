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
#include <boost/capy/ex/run.hpp>
#include <boost/capy/ex/run_async.hpp>
#include <boost/capy/ex/work_guard.hpp>

#include "test_helpers.hpp"

#include <array>
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

// Probe coroutine starts suspended; resuming it completes and
// auto-destroys the frame (suspend_never final). If never
// resumed, probe_coro's dtor destroys it.
struct probe_coro
{
    struct promise_type
    {
        probe_coro
        get_return_object() noexcept
        {
            return probe_coro{
                std::coroutine_handle<promise_type>::from_promise(*this)};
        }
        std::suspend_always initial_suspend() noexcept { return {}; }
        std::suspend_never final_suspend() noexcept { return {}; }
        void return_void() noexcept {}
        void unhandled_exception() { std::terminate(); }
    };

    std::coroutine_handle<promise_type> h_;

    ~probe_coro() { if(h_) h_.destroy(); }

    probe_coro(probe_coro&& other) noexcept
        : h_(other.h_) { other.h_ = nullptr; }

    std::coroutine_handle<void> handle() const noexcept { return h_; }
    void release() noexcept { h_ = nullptr; }

private:
    explicit probe_coro(std::coroutine_handle<promise_type> h)
        : h_(h) {}
};

inline probe_coro
make_probe()
{
    co_return;
}

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
        // continuation must outlive pool (LIFO destruction order)
        continuation c{std::noop_coroutine()};
        thread_pool pool(1);
        auto ex = pool.get_executor();

        // Post a noop coroutine and verify no exceptions
        ex.post(c);

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
        // From outside any pool, dispatch() posts.
        auto probe = make_probe();
        auto probe_h = probe.handle();
        auto* target = new continuation{probe_h};

        std::coroutine_handle<> returned;
        {
            thread_pool pool(1);
            auto ex = pool.get_executor();
            returned = ex.dispatch(*target);
        }

        BOOST_TEST(returned != probe_h);
        if(returned != probe_h)
            probe.release();
        delete target;
    }

    void
    testDispatchSymmetricTransfer()
    {
        // From a worker thread of the same pool, dispatch()
        // returns c.h for symmetric transfer and does not
        // enqueue the continuation.
        auto probe = make_probe();
        auto probe_h = probe.handle();

        // Heap-allocated so target outlives the pool if a buggy
        // implementation erroneously posts it.
        auto* target = new continuation{probe_h};

        std::atomic<bool> done{false};
        std::coroutine_handle<> returned;

        {
            thread_pool pool(1);
            auto ex = pool.get_executor();

            run_async(ex, [&]{
                returned = ex.dispatch(*target);
                done.store(true);
            })(void_task());

            BOOST_TEST(wait_for([&]{ return done.load(); }));
        }

        // On symmetric transfer the returned handle equals the
        // target's handle and the probe is never enqueued.
        BOOST_TEST(returned == probe_h);

        // If the dispatch posted (buggy), the pool destructor's
        // drain_abandoned already destroyed probe_h; release so
        // the probe_coro dtor does not double-destroy.
        if(returned != probe_h)
            probe.release();
        delete target;
    }

    void
    testDispatchCrossPool()
    {
        // Worker threads of pool A are not workers of pool B:
        // dispatch() on B from an A worker must post, not
        // symmetric-transfer.
        auto probe = make_probe();
        auto probe_h = probe.handle();
        auto* target = new continuation{probe_h};

        std::atomic<bool> done{false};
        std::coroutine_handle<> returned;

        {
            thread_pool pool_a(1);
            thread_pool pool_b(1);
            auto ex_a = pool_a.get_executor();
            auto ex_b = pool_b.get_executor();

            run_async(ex_a, [&]{
                returned = ex_b.dispatch(*target);
                done.store(true);
            })(void_task());

            BOOST_TEST(wait_for([&]{ return done.load(); }));
        }

        BOOST_TEST(returned != probe_h);
        if(returned != probe_h)
            probe.release();
        delete target;
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
        // Pre-allocate continuations: must outlive the pool
        // (LIFO destruction order).
        constexpr int num_threads = 8;
        constexpr int posts_per_thread = 10;
        std::vector<std::array<continuation, posts_per_thread>> all_conts(num_threads);
        for(auto& arr : all_conts)
            for(auto& c : arr)
                c.h = std::noop_coroutine();

        thread_pool pool(4);
        auto ex = pool.get_executor();

        std::atomic<int> post_count{0};

        std::vector<std::thread> threads;
        threads.reserve(num_threads);

        for(int i = 0; i < num_threads; ++i)
        {
            threads.emplace_back([&ex, &post_count, conts = all_conts[i].data()]{
                // Multiple threads posting concurrently
                for(int j = 0; j < posts_per_thread; ++j)
                {
                    ex.post(conts[j]);
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
            name_check_result result;
            auto nc = check_thread_name(result, "capy-pool-");
            continuation c{nc.h};
            thread_pool pool(1);
            pool.get_executor().post(c);

            BOOST_TEST(wait_for([&]{ return result.done.load(); }));
            BOOST_TEST(result.matches.load());
        }

        // Verify custom thread name from within pool thread
        {
            name_check_result result;
            auto nc = check_thread_name(result, "mypool-");
            continuation c{nc.h};
            thread_pool pool(1, "mypool-");
            pool.get_executor().post(c);

            BOOST_TEST(wait_for([&]{ return result.done.load(); }));
            BOOST_TEST(result.matches.load());
        }

        // Verify thread naming works with index suffix
        {
            name_check_result result;
            auto nc = check_thread_name(result, "idx-0");
            continuation c{nc.h};
            thread_pool pool(1, "idx-");
            pool.get_executor().post(c);

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
            std::array<continuation, 50> conts;

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
            {
                conts[i].h = std::noop_coroutine();
                ex.post(conts[i]);
            }

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

    static task<void>
    hop_and_count(
        thread_pool::executor_type worker_ex, std::atomic<int>& count)
    {
        count += co_await boost::capy::run(worker_ex)(returns_int(1));
    }

    void
    testForeignPostAfterJoin()
    {
        // The worker pool's thread posts the continuation back into a
        // pool that joins and dies immediately after the work drains,
        // exercising the window where the poster is still inside
        // post() during destruction.
        thread_pool worker(1);
        auto worker_ex = worker.get_executor();
        std::atomic<int> count{0};

        for(int i = 0; i < 50; ++i)
        {
            thread_pool pool(1);
            run_async(pool.get_executor())(
                hop_and_count(worker_ex, count));
            pool.join();
        }

        BOOST_TEST_EQ(count.load(), 50);
        worker.join();
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
        testDispatchSymmetricTransfer();
        testDispatchCrossPool();
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
        testForeignPostAfterJoin();
    }
};

TEST_SUITE(
    thread_pool_test,
    "boost.capy.thread_pool");

} // capy
} // boost
