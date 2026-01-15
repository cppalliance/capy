//
// Copyright (c) 2025 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Test that header file is self-contained.
// Test that header file is self-contained.
#include <boost/capy/ex/strand.hpp>

#include <boost/capy/concept/executor.hpp>
#include <boost/capy/ex/thread_pool.hpp>
#include <boost/capy/ex/detail/strand_service.hpp>

#include "test_suite.hpp"

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

namespace boost {
namespace capy {

namespace {

// Verify executor concept at compile time
static_assert(executor<strand<thread_pool::executor_type>>,
    "strand must satisfy executor concept");

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

// Simple test coroutine that increments a counter
struct counter_coro
{
    struct promise_type
    {
        std::atomic<int>* counter;

        counter_coro
        get_return_object() noexcept
        {
            return counter_coro{std::coroutine_handle<promise_type>::from_promise(*this)};
        }

        std::suspend_always
        initial_suspend() noexcept
        {
            return {};
        }

        std::suspend_never
        final_suspend() noexcept
        {
            return {};
        }

        void
        return_void() noexcept
        {
        }

        void
        unhandled_exception()
        {
            std::terminate();
        }
    };

    std::coroutine_handle<promise_type> h_;

    ~counter_coro()
    {
        if(h_)
            h_.destroy();
    }

    counter_coro(counter_coro&& other) noexcept
        : h_(other.h_)
    {
        other.h_ = nullptr;
    }

    counter_coro& operator=(counter_coro&& other) noexcept
    {
        if(h_)
            h_.destroy();
        h_ = other.h_;
        other.h_ = nullptr;
        return *this;
    }

    std::coroutine_handle<void>
    handle() const noexcept
    {
        return h_;
    }

    void
    release() noexcept
    {
        h_ = nullptr;
    }

private:
    explicit counter_coro(std::coroutine_handle<promise_type> h)
        : h_(h)
    {
    }
};

// Creates a coroutine that increments counter
inline counter_coro
make_counter_coro(std::atomic<int>& counter)
{
    return [](std::atomic<int>* counter) -> counter_coro {
        ++(*counter);
        co_return;
    }(&counter);
}

// Coroutine that records order of execution
struct order_coro
{
    struct promise_type
    {
        std::vector<int>* log;
        std::mutex* log_mutex;
        int id;

        order_coro
        get_return_object() noexcept
        {
            return order_coro{std::coroutine_handle<promise_type>::from_promise(*this)};
        }

        std::suspend_always
        initial_suspend() noexcept
        {
            return {};
        }

        std::suspend_never
        final_suspend() noexcept
        {
            return {};
        }

        void
        return_void() noexcept
        {
        }

        void
        unhandled_exception()
        {
            std::terminate();
        }
    };

    std::coroutine_handle<promise_type> h_;

    ~order_coro()
    {
        if(h_)
            h_.destroy();
    }

    order_coro(order_coro&& other) noexcept
        : h_(other.h_)
    {
        other.h_ = nullptr;
    }

    order_coro& operator=(order_coro&& other) noexcept
    {
        if(h_)
            h_.destroy();
        h_ = other.h_;
        other.h_ = nullptr;
        return *this;
    }

    std::coroutine_handle<void>
    handle() const noexcept
    {
        return h_;
    }

    void
    release() noexcept
    {
        h_ = nullptr;
    }

private:
    explicit order_coro(std::coroutine_handle<promise_type> h)
        : h_(h)
    {
    }
};

// Creates a coroutine that logs its id to a vector
inline order_coro
make_order_coro(std::vector<int>& log, std::mutex& log_mutex, int id)
{
    return [](std::vector<int>* log, std::mutex* log_mutex, int id) -> order_coro {
        std::lock_guard<std::mutex> lock(*log_mutex);
        log->push_back(id);
        co_return;
    }(&log, &log_mutex, id);
}

} // namespace

struct strand_test
{
    void
    testConstruct()
    {
        // Construct from executor
        {
            thread_pool pool(1);
            strand<thread_pool::executor_type> s(pool.get_executor());
            (void)s;
        }

        // Using implicit guide
        {
            thread_pool pool(1);
            auto s = strand(pool.get_executor());
            (void)s;
        }
    }

    void
    testCopy()
    {
        thread_pool pool(1);
        auto s1 = strand(pool.get_executor());

        // Copy construction
        auto s2 = s1;

        // Copies should be equal (share same impl)
        BOOST_TEST(s1 == s2);

        // Copy assignment
        auto s3 = strand(pool.get_executor());
        s3 = s1;
        BOOST_TEST(s1 == s3);
    }

    void
    testMove()
    {
        thread_pool pool(1);
        auto s1 = strand(pool.get_executor());

        // Move construction
        auto s2 = std::move(s1);
        (void)s2;

        // Move assignment
        auto s3 = strand(pool.get_executor());
        auto s4 = strand(pool.get_executor());
        s4 = std::move(s3);
        (void)s4;
    }

    void
    testGetInnerExecutor()
    {
        thread_pool pool(1);
        auto ex = pool.get_executor();
        strand<thread_pool::executor_type> s(ex);

        // get_inner_executor returns the wrapped executor
        BOOST_TEST(s.get_inner_executor() == ex);
    }

    void
    testContext()
    {
        thread_pool pool(1);
        auto s = strand(pool.get_executor());

        // context() returns the pool
        BOOST_TEST_EQ(&s.context(), &pool);
    }

    void
    testWorkTracking()
    {
        thread_pool pool(1);
        auto s = strand(pool.get_executor());

        // Work tracking should not throw
        s.on_work_started();
        s.on_work_started();
        s.on_work_finished();
        s.on_work_finished();
    }

    void
    testEquality()
    {
        thread_pool pool(1);

        auto s1 = strand(pool.get_executor());
        auto s2 = s1;  // Copy shares impl

        // Copies are equal
        BOOST_TEST(s1 == s2);

        // Different strands may or may not be equal
        // (depends on hash collision)
        auto s3 = strand(pool.get_executor());
        auto s4 = strand(pool.get_executor());
        // We don't test s3 == s4 since it's hash-dependent
        (void)s3;
        (void)s4;
    }

    void
    testDispatch()
    {
        thread_pool pool(1);
        auto s = strand(pool.get_executor());

        std::atomic<int> counter{0};

        auto coro = make_counter_coro(counter);
        auto result = s.dispatch(coro.handle());
        coro.release();

        // dispatch returns noop_coroutine (work was queued/run)
        BOOST_TEST(result == std::noop_coroutine());

        // Wait for work to complete
        BOOST_TEST(wait_for([&]{ return counter.load() >= 1; }));
        BOOST_TEST_EQ(counter.load(), 1);
    }

    void
    testPost()
    {
        thread_pool pool(1);
        auto s = strand(pool.get_executor());

        std::atomic<int> counter{0};

        auto coro = make_counter_coro(counter);
        s.post(coro.handle());
        coro.release();

        // Wait for work to complete
        BOOST_TEST(wait_for([&]{ return counter.load() >= 1; }));
        BOOST_TEST_EQ(counter.load(), 1);
    }

    void
    testDefer()
    {
        thread_pool pool(1);
        auto s = strand(pool.get_executor());

        std::atomic<int> counter{0};

        auto coro = make_counter_coro(counter);
        s.defer(coro.handle());
        coro.release();

        // Wait for work to complete
        BOOST_TEST(wait_for([&]{ return counter.load() >= 1; }));
        BOOST_TEST_EQ(counter.load(), 1);
    }

    void
    testOperatorCall()
    {
        thread_pool pool(1);
        auto s = strand(pool.get_executor());

        std::atomic<int> counter{0};

        auto coro = make_counter_coro(counter);
        auto result = s(coro.handle());
        coro.release();

        // operator() returns noop_coroutine
        BOOST_TEST(result == std::noop_coroutine());

        // Wait for work to complete
        BOOST_TEST(wait_for([&]{ return counter.load() >= 1; }));
    }

    void
    testMultipleWork()
    {
        thread_pool pool(2);
        auto s = strand(pool.get_executor());

        std::atomic<int> counter{0};
        constexpr int N = 100;

        std::vector<counter_coro> coros;
        coros.reserve(N);

        for(int i = 0; i < N; ++i)
        {
            coros.push_back(make_counter_coro(counter));
            s.post(coros.back().handle());
            coros.back().release();
        }

        // Wait for all work to complete
        BOOST_TEST(wait_for([&]{ return counter.load() >= N; }));
        BOOST_TEST_EQ(counter.load(), N);
    }

    void
    testConcurrentPost()
    {
        thread_pool pool(4);
        auto s = strand(pool.get_executor());

        std::atomic<int> counter{0};
        constexpr int num_threads = 4;
        constexpr int per_thread = 25;

        std::vector<std::thread> threads;
        threads.reserve(num_threads);

        for(int i = 0; i < num_threads; ++i)
        {
            threads.emplace_back([&s, &counter]{
                for(int j = 0; j < per_thread; ++j)
                {
                    auto coro = make_counter_coro(counter);
                    s.post(coro.handle());
                    coro.release();
                }
            });
        }

        for(auto& t : threads)
            t.join();

        // Wait for all work to complete
        BOOST_TEST(wait_for([&]{ return counter.load() >= num_threads * per_thread; }));
        BOOST_TEST_EQ(counter.load(), num_threads * per_thread);
    }

    void
    testServiceCreation()
    {
        // Strand should create strand_service on first use
        thread_pool pool(1);

        // Initially no strand_service
        BOOST_TEST(!pool.has_service<detail::strand_service>());

        // Creating a strand should create the service
        auto s = strand(pool.get_executor());
        BOOST_TEST(pool.has_service<detail::strand_service>());
        (void)s;
    }

    void
    testRunningInThisThread()
    {
        thread_pool pool(1);
        auto s = strand(pool.get_executor());

        // Initially not running
        // Note: This is an approximation based on lock state
        bool running = s.running_in_this_thread();
        (void)running;  // Value depends on timing
    }

    void
    testFifoOrder()
    {
        // Use multiple threads to stress-test ordering
        thread_pool pool(4);
        auto s = strand(pool.get_executor());

        std::vector<int> log;
        std::mutex log_mutex;
        constexpr int N = 50;

        std::vector<order_coro> coros;
        coros.reserve(N);

        // Post coroutines with sequential IDs
        for(int i = 0; i < N; ++i)
        {
            coros.push_back(make_order_coro(log, log_mutex, i));
            s.post(coros.back().handle());
            coros.back().release();
        }

        // Wait for all work to complete
        BOOST_TEST(wait_for([&]{
            std::lock_guard<std::mutex> lock(log_mutex);
            return static_cast<int>(log.size()) >= N;
        }));

        // Verify FIFO order
        std::lock_guard<std::mutex> lock(log_mutex);
        BOOST_TEST_EQ(static_cast<int>(log.size()), N);
        for(int i = 0; i < N; ++i)
            BOOST_TEST_EQ(log[i], i);
    }

    void
    run()
    {
        testConstruct();
        testCopy();
        testMove();
        testGetInnerExecutor();
        testContext();
        testWorkTracking();
        testEquality();
        testDispatch();
        testPost();
        testDefer();
        testOperatorCall();
        testMultipleWork();
        testConcurrentPost();
        testServiceCreation();
        testRunningInThisThread();
        testFifoOrder();
    }
};

TEST_SUITE(
    strand_test,
    "boost.capy.strand");

} // capy
} // boost
