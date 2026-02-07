//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Test that header file is self-contained.
#include <boost/capy/ex/strand.hpp>

#include <boost/capy/concept/executor.hpp>
#include <boost/capy/ex/any_executor.hpp>
#include <boost/capy/ex/thread_pool.hpp>

#include "test_suite.hpp"

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

namespace boost {
namespace capy {

namespace {

// Verify strand satisfies Executor concept at compile time
static_assert(Executor<strand<thread_pool::executor_type>>,
    "strand must satisfy Executor concept");
static_assert(Executor<strand<any_executor>>,
    "strand<any_executor> must satisfy Executor concept");

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

        // Using deduction guide
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

        // Copies share same implementation
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

        BOOST_TEST(s.get_inner_executor() == ex);
    }

    void
    testContext()
    {
        thread_pool pool(1);
        auto s = strand(pool.get_executor());

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
        auto s2 = s1;

        // Copies are equal
        BOOST_TEST(s1 == s2);

        // Different strands from same pool may or may not be equal
        // depending on internal hash collision
        auto s3 = strand(pool.get_executor());
        (void)s3;
    }

    void
    testRunningInThisThread()
    {
        thread_pool pool(1);
        auto s = strand(pool.get_executor());

        // Not running in strand from main thread
        BOOST_TEST(!s.running_in_this_thread());
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

        BOOST_TEST(wait_for([&]{ return counter.load() >= 1; }));
        BOOST_TEST_EQ(counter.load(), 1);
    }

    void
    testDispatch()
    {
        thread_pool pool(1);
        auto s = strand(pool.get_executor());

        std::atomic<int> counter{0};

        auto coro = make_counter_coro(counter);
        s.dispatch(coro.handle());
        coro.release();

        BOOST_TEST(wait_for([&]{ return counter.load() >= 1; }));
        BOOST_TEST_EQ(counter.load(), 1);
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

        BOOST_TEST(wait_for([&]{ return counter.load() >= num_threads * per_thread; }));
        BOOST_TEST_EQ(counter.load(), num_threads * per_thread);
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
    testSerialization()
    {
        // Verify coroutines don't run concurrently
        thread_pool pool(4);
        auto s = strand(pool.get_executor());

        std::atomic<int> active{0};
        std::atomic<int> max_active{0};
        std::atomic<int> completed{0};
        constexpr int N = 50;

        // Coroutine that tracks concurrent execution
        struct tracking_coro
        {
            struct promise_type
            {
                tracking_coro
                get_return_object() noexcept
                {
                    return tracking_coro{
                        std::coroutine_handle<promise_type>::from_promise(*this)};
                }

                std::suspend_always initial_suspend() noexcept { return {}; }
                std::suspend_never final_suspend() noexcept { return {}; }
                void return_void() noexcept {}
                void unhandled_exception() { std::terminate(); }
            };

            std::coroutine_handle<promise_type> h_;

            ~tracking_coro()
            {
                if(h_)
                    h_.destroy();
            }

            tracking_coro(tracking_coro&& other) noexcept
                : h_(other.h_)
            {
                other.h_ = nullptr;
            }

            tracking_coro& operator=(tracking_coro&& other) noexcept
            {
                if(h_)
                    h_.destroy();
                h_ = other.h_;
                other.h_ = nullptr;
                return *this;
            }

            std::coroutine_handle<void> handle() const noexcept { return h_; }

            void release() noexcept { h_ = nullptr; }

        private:
            explicit tracking_coro(std::coroutine_handle<promise_type> h)
                : h_(h)
            {
            }
        };

        auto make_tracking_coro = [&]() -> tracking_coro {
            int current = ++active;
            int expected = max_active.load();
            while(current > expected)
            {
                if(max_active.compare_exchange_weak(expected, current))
                    break;
            }
            // Small delay to increase chance of overlap if serialization fails
            std::this_thread::sleep_for(std::chrono::microseconds(10));
            --active;
            ++completed;
            co_return;
        };

        std::vector<tracking_coro> coros;
        coros.reserve(N);

        for(int i = 0; i < N; ++i)
        {
            coros.push_back(make_tracking_coro());
            s.post(coros.back().handle());
            coros.back().release();
        }

        BOOST_TEST(wait_for([&]{ return completed.load() >= N; }));

        // Strand should serialize - max_active should be 1
        BOOST_TEST_EQ(max_active.load(), 1);
        BOOST_TEST_EQ(completed.load(), N);
    }

    void
    testAnyExecutor()
    {
        // Construct strand from any_executor
        {
            thread_pool pool(1);
            any_executor ex = pool.get_executor();
            strand<any_executor> s(ex);
            (void)s;
        }

        // Using deduction guide
        {
            thread_pool pool(1);
            any_executor ex = pool.get_executor();
            auto s = strand(ex);
            static_assert(std::is_same_v<decltype(s), strand<any_executor>>);
            (void)s;
        }

        // Post work through strand<any_executor>
        {
            thread_pool pool(2);
            any_executor ex = pool.get_executor();
            auto s = strand(ex);

            std::atomic<int> counter{0};
            constexpr int N = 20;

            std::vector<counter_coro> coros;
            coros.reserve(N);

            for(int i = 0; i < N; ++i)
            {
                coros.push_back(make_counter_coro(counter));
                s.post(coros.back().handle());
                coros.back().release();
            }

            BOOST_TEST(wait_for([&]{ return counter.load() >= N; }));
            BOOST_TEST_EQ(counter.load(), N);
        }

        // Copy and equality
        {
            thread_pool pool(1);
            any_executor ex = pool.get_executor();
            auto s1 = strand(ex);
            auto s2 = s1;
            BOOST_TEST(s1 == s2);
        }
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
        testRunningInThisThread();
        testPost();
        testDispatch();
        testMultipleWork();
        testConcurrentPost();
        testFifoOrder();
        testSerialization();
        testAnyExecutor();
    }
};

TEST_SUITE(
    strand_test,
    "boost.capy.strand");

} // capy
} // boost
