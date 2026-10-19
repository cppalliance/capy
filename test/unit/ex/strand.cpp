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
#include <boost/capy/ex/strand.hpp>

// Full strand_impl definition for white-box collision tests.
#include "src/ex/detail/strand_impl.hpp"

#include <boost/capy/concept/executor.hpp>
#include <boost/capy/ex/any_executor.hpp>
#include <boost/capy/ex/abandon.hpp>
#include <boost/capy/ex/run.hpp>
#include <boost/capy/ex/this_coro.hpp>
#include <boost/capy/ex/thread_pool.hpp>
#include <boost/capy/task.hpp>
#include <boost/capy/test/run_blocking.hpp>

#include "test_helpers.hpp"
#include "test_suite.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <stdexcept>
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

// Fails its next posts, running a hook in each failing post first.
struct reentrant_executor
{
    std::queue<continuation>* q_;
    test_io_context* ctx_;
    int* fails_;
    std::function<void()>* on_fail_;

    bool operator==(reentrant_executor const& o) const noexcept
    {
        return q_ == o.q_;
    }
    test_io_context& context() const noexcept { return *ctx_; }
    void on_work_started() const noexcept {}
    void on_work_finished() const noexcept {}
    std::coroutine_handle<> dispatch(continuation& c) const
    {
        post(c);
        return std::noop_coroutine();
    }
    void post(continuation& c) const
    {
        if(*fails_ > 0)
        {
            --*fails_;
            if(*on_fail_)
                std::exchange(*on_fail_, nullptr)();
            throw std::runtime_error("post");
        }
        q_->push(c);
    }
};

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

struct lifetime_coro
{
    struct promise_type
    {
        lifetime_coro
        get_return_object() noexcept
        {
            return lifetime_coro{
                std::coroutine_handle<promise_type>::from_promise(*this)};
        }
        std::suspend_always initial_suspend() noexcept { return {}; }
        std::suspend_never  final_suspend()   noexcept { return {}; }
        void return_void() noexcept {}
        void unhandled_exception() { std::terminate(); }
    };

    std::coroutine_handle<promise_type> h_;

    ~lifetime_coro() { if(h_) h_.destroy(); }
    lifetime_coro(lifetime_coro&& other) noexcept : h_(other.h_) { other.h_ = nullptr; }
    lifetime_coro& operator=(lifetime_coro&& other) noexcept
    {
        if(h_) h_.destroy();
        h_ = other.h_;
        other.h_ = nullptr;
        return *this;
    }

    std::coroutine_handle<void> handle() const noexcept { return h_; }
    void release() noexcept { h_ = nullptr; }

private:
    explicit lifetime_coro(std::coroutine_handle<promise_type> h) : h_(h) {}
    friend lifetime_coro make_lifetime_coro(std::atomic<bool>&);
};

inline lifetime_coro
make_lifetime_coro(std::atomic<bool>& flag)
{
    return [](std::atomic<bool>* f) -> lifetime_coro {
        f->store(true);
        co_return;
    }(&flag);
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

        // Copies share the same impl.
        BOOST_TEST(s1 == s2);

        // Distinct strands have distinct impls.
        auto s3 = strand(pool.get_executor());
        BOOST_TEST(!(s1 == s3));
    }

    void
    testNoEqualityCollisions()
    {
        thread_pool pool(1);
        constexpr int N = 1000;

        std::vector<strand<thread_pool::executor_type>> strands;
        strands.reserve(N);
        for(int i = 0; i < N; ++i)
            strands.push_back(strand(pool.get_executor()));

        int collisions = 0;
        for(int i = 0; i < N; ++i)
            for(int j = i + 1; j < N; ++j)
                if(strands[i] == strands[j])
                    ++collisions;

        BOOST_TEST_EQ(collisions, 0);
    }

    void
    testStrandsAreIndependent()
    {
        // Two threads so two strands can run concurrently. Construct
        // enough strands that the first and last would have shared an
        // impl under the previous 211-slot pooled design; verify the
        // new per-strand design lets them run in parallel.
        thread_pool pool(2);

        constexpr int N = 212;  // > 211 forces a hash-pool collision pre-refactor
        std::vector<strand<thread_pool::executor_type>> strands;
        strands.reserve(N);
        for(int i = 0; i < N; ++i)
            strands.push_back(strand(pool.get_executor()));

        auto& sA = strands.front();
        auto& sB = strands.back();

        std::atomic<bool> a_started{false};
        std::atomic<bool> a_done{false};
        std::atomic<bool> b_done{false};

        struct latched_coro
        {
            struct promise_type
            {
                latched_coro
                get_return_object() noexcept
                {
                    return latched_coro{
                        std::coroutine_handle<promise_type>::from_promise(*this)};
                }
                std::suspend_always initial_suspend() noexcept { return {}; }
                std::suspend_never  final_suspend()   noexcept { return {}; }
                void return_void() noexcept {}
                void unhandled_exception() { std::terminate(); }
            };
            std::coroutine_handle<promise_type> h_;
        };

        auto make_latched =
            [](std::atomic<bool>* started,
               std::atomic<bool>& done,
               std::chrono::milliseconds delay) -> latched_coro
        {
            if(started) started->store(true);
            std::this_thread::sleep_for(delay);
            done.store(true);
            co_return;
        };

        auto coro_a = make_latched(
            &a_started, a_done, std::chrono::milliseconds(200));
        continuation ca{coro_a.h_};
        sA.post(ca);
        coro_a.h_ = nullptr;

        // Wait until A is actively sleeping
        BOOST_TEST(wait_for([&]{ return a_started.load(); }));

        auto coro_b = make_latched(
            nullptr, b_done, std::chrono::milliseconds(0));
        continuation cb{coro_b.h_};
        sB.post(cb);
        coro_b.h_ = nullptr;

        // B should complete while A is still sleeping
        BOOST_TEST(wait_for(
            [&]{ return b_done.load(); },
            std::chrono::milliseconds(150)));
        BOOST_TEST(!a_done.load());

        // Let A finish so the test cleans up
        BOOST_TEST(wait_for([&]{ return a_done.load(); }));
    }

    void
    testTransientStrandLifetime()
    {
        thread_pool pool(1);
        std::atomic<bool> done{false};
        std::weak_ptr<detail::strand_impl> impl_weak;

        // c must outlive its time in the strand queue; the strand
        // links it intrusively rather than copying.
        continuation c;
        {
            auto s = strand(pool.get_executor());
            impl_weak = s.impl_;
            auto coro = make_lifetime_coro(done);
            c.h = coro.handle();
            s.post(c);
            coro.release();
        }   // strand handle dropped here

        BOOST_TEST(wait_for([&]{ return done.load(); }));
        // After the invoker drains and exits, the impl shared_ptr in
        // its coroutine frame releases. The weak_ptr should expire.
        BOOST_TEST(wait_for([&]{ return impl_weak.expired(); }));
    }

    void
    testManyStrandsStress()
    {
        thread_pool pool(4);
        constexpr int num_strands = 10000;
        constexpr int posts_per_strand = 3;

        std::atomic<int> total{0};

        std::vector<strand<thread_pool::executor_type>> strands;
        strands.reserve(num_strands);
        for(int i = 0; i < num_strands; ++i)
            strands.push_back(strand(pool.get_executor()));

        std::vector<counter_coro> coros;
        coros.reserve(num_strands * posts_per_strand);
        std::vector<continuation> conts;
        conts.reserve(num_strands * posts_per_strand);

        for(int i = 0; i < num_strands; ++i)
        {
            for(int j = 0; j < posts_per_strand; ++j)
            {
                coros.push_back(make_counter_coro(total));
                conts.push_back({coros.back().handle()});
                strands[i].post(conts.back());
                coros.back().release();
            }
        }

        BOOST_TEST(wait_for(
            [&]{ return total.load() >= num_strands * posts_per_strand; },
            std::chrono::milliseconds(30000)));
        BOOST_TEST_EQ(total.load(), num_strands * posts_per_strand);
    }

    void
    testMutexPoolCollisionIsolation()
    {
        // 193 mutexes in the service pool. With > 193 strands, at least
        // two must share a mutex. Scan to find a colliding pair, then
        // verify they run concurrently when posted to in parallel.
        thread_pool pool(2);

        constexpr int N = 200;
        std::vector<strand<thread_pool::executor_type>> strands;
        strands.reserve(N);
        for(int i = 0; i < N; ++i)
            strands.push_back(strand(pool.get_executor()));

        // Find a colliding pair via the borrowed mutex pointer.
        int idx_a = -1, idx_b = -1;
        for(int i = 0; i < N && idx_b < 0; ++i)
        {
            for(int j = i + 1; j < N; ++j)
            {
                if(strands[i].impl_->mutex_ == strands[j].impl_->mutex_)
                {
                    idx_a = i;
                    idx_b = j;
                    break;
                }
            }
        }
        BOOST_TEST(idx_a >= 0);    // pigeonhole guarantees a hit
        if(idx_a < 0)
            return;

        auto& sA = strands[idx_a];
        auto& sB = strands[idx_b];

        std::atomic<int> max_active{0};
        std::atomic<int> active{0};
        std::atomic<int> done{0};

        // Each coroutine increments active, then waits at a rendezvous
        // until both have arrived (or timeout). If colliding strands run
        // in parallel, both observe active==2; if they serialize, the
        // first waits the full timeout and max_active never reaches 2.
        auto make_busy = [&]() -> counter_coro {
            return [](std::atomic<int>* a,
                      std::atomic<int>* m,
                      std::atomic<int>* d) -> counter_coro
            {
                int cur = ++(*a);
                int prev = m->load();
                while(cur > prev && !m->compare_exchange_weak(prev, cur)) {}
                auto deadline = std::chrono::steady_clock::now() +
                    std::chrono::seconds(2);
                while(a->load() < 2 &&
                      std::chrono::steady_clock::now() < deadline)
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                int cur2 = a->load();
                int prev2 = m->load();
                while(cur2 > prev2 && !m->compare_exchange_weak(prev2, cur2)) {}
                --(*a);
                ++(*d);
                co_return;
            }(&active, &max_active, &done);
        };

        auto coroA = make_busy();
        auto coroB = make_busy();
        continuation cA{coroA.handle()};
        continuation cB{coroB.handle()};
        sA.post(cA);
        sB.post(cB);
        coroA.release();
        coroB.release();

        BOOST_TEST(wait_for(
            [&]{ return done.load() >= 2; },
            std::chrono::seconds(10)));
        BOOST_TEST_EQ(max_active.load(), 2);
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
        continuation c{coro.handle()};
        s.post(c);
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
        continuation c{coro.handle()};
        s.dispatch(c);
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
        std::vector<continuation> conts;
        conts.reserve(N);

        for(int i = 0; i < N; ++i)
        {
            coros.push_back(make_counter_coro(counter));
            conts.push_back({coros.back().handle()});
            s.post(conts.back());
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

        // Storage hoisted out of the threads so each continuation
        // outlives its time in the strand queue.
        std::vector<std::vector<counter_coro>> coros_per_thread(num_threads);
        std::vector<std::vector<continuation>> conts_per_thread(num_threads);
        for(int i = 0; i < num_threads; ++i)
        {
            coros_per_thread[i].reserve(per_thread);
            conts_per_thread[i].reserve(per_thread);
        }

        for(int i = 0; i < num_threads; ++i)
        {
            threads.emplace_back(
                [&s, &counter,
                 &my_coros = coros_per_thread[i],
                 &my_conts = conts_per_thread[i]]
                {
                    for(int j = 0; j < per_thread; ++j)
                    {
                        my_coros.push_back(make_counter_coro(counter));
                        my_conts.push_back({my_coros.back().handle()});
                        s.post(my_conts.back());
                        my_coros.back().release();
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
        std::vector<continuation> conts;
        conts.reserve(N);

        // Post coroutines with sequential IDs
        for(int i = 0; i < N; ++i)
        {
            coros.push_back(make_order_coro(log, log_mutex, i));
            conts.push_back({coros.back().handle()});
            s.post(conts.back());
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
        std::vector<continuation> conts;
        conts.reserve(N);

        for(int i = 0; i < N; ++i)
        {
            coros.push_back(make_tracking_coro());
            conts.push_back({coros.back().handle()});
            s.post(conts.back());
            coros.back().release();
        }

        BOOST_TEST(wait_for([&]{ return completed.load() >= N; }));

        // Strand should serialize - max_active should be 1
        BOOST_TEST_EQ(max_active.load(), 1);
        BOOST_TEST_EQ(completed.load(), N);
    }

    // After co_await run(strand)(...) returns, caller must be outside
    // the strand. User-reported bug: today it is still inside.
    void
    testExitStrandAfterRun()
    {
        bool running_in_strand_after_run = true;
        bool inner_ran = false;

        test::run_blocking()([&]() -> task<void> {
            auto ex = co_await this_coro::executor;
            auto str = capy::strand(ex);

            co_await capy::run(str)([&]() -> task<void> {
                inner_ran = true;
                co_return;
            }());

            running_in_strand_after_run = str.running_in_this_thread();
        }());

        BOOST_TEST(inner_ran);
        BOOST_TEST(!running_in_strand_after_run);
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
            std::vector<continuation> conts;
            conts.reserve(N);

            for(int i = 0; i < N; ++i)
            {
                coros.push_back(make_counter_coro(counter));
                conts.push_back({coros.back().handle()});
                s.post(conts.back());
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

    // A started chain queued on a strand when its context shuts down
    // comes down with every frame freed.
    void
    testShutdownDestroysQueuedParentedChain()
    {
        alloc_counts st;
        int child_down = 0, parent_down = 0;
        parked slot;
        continuation c;
        std::queue<continuation> q;
        {
            auto ctx = std::make_unique<test_io_context>();
            queuing_executor qe(q);
            qe.ctx_ = ctx.get();
            strand<queuing_executor> s{qe};
            auto child = [](parked* sl, destroy_count_t)
                -> task<void>
            {
                co_await park_awaitable{sl};
            };
            auto parent = [&](destroy_count_t) -> task<void>
            {
                co_await child(&slot, destroy_count_t(child_down));
            };
            run_async(s, counting_allocator<std::byte>(st))(
                parent(destroy_count_t(parent_down)));
            pump(q);
            BOOST_TEST(slot);

            c = slot;
            s.post(c);
            // The strand's runner is lost; the chain stays queued.
            while(!q.empty())
            {
                capy::abandon(q.front());
                q.pop();
            }
            BOOST_TEST_EQ(parent_down, 0);
            ctx.reset();
            BOOST_TEST(q.empty());
        }
        BOOST_TEST_EQ(child_down, 1);
        BOOST_TEST_EQ(parent_down, 1);
        BOOST_TEST(st.balanced());
    }

    // Service shutdown destroys what is still queued on every strand
    // of the context, not only the first.
    void
    testShutdownDestroysQueuedAcrossStrands()
    {
        int down[4] = {};
        continuation conts[4] = {};
        std::queue<continuation> q;
        auto probe = [](destroy_count_t) -> task<void> { co_return; };
        for(std::size_t i = 0; i < 4; ++i)
        {
            auto t = probe(destroy_count_t(down[i]));
            conts[i].h = t.handle();
            t.release();
        }
        auto ctx = std::make_unique<test_io_context>();
        queuing_executor qe(q);
        qe.ctx_ = ctx.get();
        strand<queuing_executor> s1{qe};
        strand<queuing_executor> s2{qe};
        s1.post(conts[0]);
        s1.post(conts[1]);
        s2.post(conts[2]);
        s2.post(conts[3]);
        BOOST_TEST_EQ(q.size(), 2u);
        while(!q.empty())
        {
            capy::abandon(q.front());
            q.pop();
        }
        for(int d : down)
            BOOST_TEST_EQ(d, 0);

        ctx.reset();
        for(int d : down)
            BOOST_TEST_EQ(d, 1);
    }

    // The strand service gives up every strand's queued work in its
    // shutdown(), so a strand dropped afterwards has nothing left to
    // give up, and destroying the services posts nothing.
    void
    testShutdownLeavesNothingForStrandDestructor()
    {
        int down[2] = {};
        continuation conts[2] = {};
        std::queue<continuation> q;
        auto probe = [](destroy_count_t) -> task<void> { co_return; };
        for(std::size_t i = 0; i < 2; ++i)
        {
            auto t = probe(destroy_count_t(down[i]));
            conts[i].h = t.handle();
            t.release();
        }
        auto ctx = std::make_unique<closable_test_context>();
        queuing_executor qe(q);
        qe.ctx_ = ctx.get();
        auto s = std::make_unique<strand<queuing_executor>>(qe);
        s->post(conts[0]);
        s->post(conts[1]);
        BOOST_TEST_EQ(q.size(), 1u);
        capy::abandon(q.front());
        q.pop();
        for(int d : down)
            BOOST_TEST_EQ(d, 0);

        ctx->shutdown();
        for(int d : down)
            BOOST_TEST_EQ(d, 1);
        s.reset();
        ctx.reset();
        for(int d : down)
            BOOST_TEST_EQ(d, 1);
        BOOST_TEST(q.empty());
    }

    void
    testShutdownReleasesLiveStrand()
    {
        // Destroy the pool while a strand still owns a registered impl,
        // so strand_service::shutdown() walks the live impl list and
        // detaches it. The returned strand is then in a detached state
        // and is only destroyed (never used).
        auto make = [] {
            thread_pool pool(1);
            return strand(pool.get_executor());
        };
        auto s = make();
        (void)s;
    }

    // A strand's first post racing its service's shutdown either queues
    // an invoker the test then destroys, or is destroyed at once; the
    // closed invoker cache is never reopened.
    void
    testFirstPostRacesShutdown()
    {
        struct locked_queue
        {
            std::mutex m;
            std::vector<continuation> v;
        };
        struct locked_executor
        {
            locked_queue* q_;
            test_io_context* ctx_;

            bool operator==(locked_executor const& o) const noexcept
            {
                return q_ == o.q_;
            }
            test_io_context& context() const noexcept { return *ctx_; }
            void on_work_started() const noexcept {}
            void on_work_finished() const noexcept {}
            std::coroutine_handle<> dispatch(continuation& c) const
            {
                post(c);
                return std::noop_coroutine();
            }
            void post(continuation& c) const
            {
                std::lock_guard<std::mutex> lk(q_->m);
                q_->v.push_back(c);
            }
        };
        auto probe = [](destroy_count_t) -> task<void> { co_return; };
        for(int i = 0; i < 1000; ++i)
        {
            int down = 0;
            locked_queue q;
            auto ctx = std::make_unique<closable_test_context>();
            locked_executor le{&q, ctx.get()};
            auto s = std::make_unique<strand<locked_executor>>(le);
            auto t = probe(destroy_count_t(down));
            continuation c{t.handle()};
            t.release();
            std::atomic<int> go{0};
            std::thread a([&]{
                go.fetch_add(1);
                while(go.load() < 2)
                    std::this_thread::yield();
                s->post(c);
            });
            go.fetch_add(1);
            while(go.load() < 2)
                std::this_thread::yield();
            ctx->shutdown();
            a.join();
            for(auto& queued : q.v)
                capy::abandon(queued);
            q.v.clear();
            s.reset();
            ctx.reset();
            BOOST_TEST_EQ(down, 1);
        }
    }

    // An inner post that throws leaves the strand as it was: the caller
    // still holds its continuation, and the next post runs.
    void
    testInnerPostThrows()
    {
        struct throwing_executor
        {
            std::queue<continuation>* q_;
            test_io_context* ctx_;
            bool* fail_;

            bool operator==(throwing_executor const& o) const noexcept
            {
                return q_ == o.q_;
            }
            test_io_context& context() const noexcept { return *ctx_; }
            void on_work_started() const noexcept {}
            void on_work_finished() const noexcept {}
            std::coroutine_handle<> dispatch(continuation& c) const
            {
                post(c);
                return std::noop_coroutine();
            }
            void post(continuation& c) const
            {
                if(*fail_)
                    throw std::runtime_error("post");
                q_->push(c);
            }
        };
        std::atomic<int> ran{0};
        std::queue<continuation> q;
        bool fail = true;
        auto held = make_counter_coro(ran);
        auto ctx = std::make_unique<closable_test_context>();
        auto s = std::make_unique<strand<throwing_executor>>(
            throwing_executor{&q, ctx.get(), &fail});
        continuation c1{held.handle()};
        BOOST_TEST_THROWS(s->post(c1), std::runtime_error);
        fail = false;
        auto next = make_counter_coro(ran);
        continuation c2{next.handle()};
        next.release();
        s->post(c2);
        while(!q.empty())
        {
            auto c = q.front();
            q.pop();
            c.h.resume();
        }
        BOOST_TEST_EQ(ran.load(), 1);
        s.reset();
        ctx.reset();
    }

    // Work queued behind a continuation whose invoker could not be
    // posted still runs with no later post to restart the strand, or
    // is given up if no invoker can be posted for it either.
    void
    testPostQueuedBehindThrowingPost()
    {
        auto attempt = [](int fails, int& ran, int& down)
        {
            std::atomic<int> count{0};
            std::queue<continuation> q;
            std::function<void()> on_fail;
            auto held = make_counter_coro(count);
            auto behind = [](std::atomic<int>* n, destroy_count_t)
                -> counter_coro
            {
                ++*n;
                co_return;
            }(&count, destroy_count_t(down));
            continuation c1{held.handle()};
            continuation c2{behind.handle()};
            behind.release();
            auto ctx = std::make_unique<closable_test_context>();
            auto s = std::make_unique<strand<reentrant_executor>>(
                reentrant_executor{&q, ctx.get(), &fails, &on_fail});
            on_fail = [&] { s->post(c2); };
            BOOST_TEST_THROWS(s->post(c1), std::runtime_error);
            while(!q.empty())
            {
                auto c = q.front();
                q.pop();
                c.h.resume();
            }
            ran = count.load();
            s.reset();
            ctx.reset();
        };
        {
            int ran = 0;
            int down = 0;
            attempt(1, ran, down);
            BOOST_TEST_EQ(ran, 1);
            BOOST_TEST_EQ(down, 1);
        }
        {
            int ran = 0;
            int down = 0;
            attempt(2, ran, down);
            BOOST_TEST_EQ(ran, 0);
            BOOST_TEST_EQ(down, 1);
        }
    }

    // A post that throws after a shutdown took its continuation has
    // nothing to hand back: the shutdown already destroyed it.
    void
    testPostThrowsAfterShutdownTookIt()
    {
        int down = 0;
        int fails = 1;
        std::queue<continuation> q;
        std::function<void()> on_fail;
        auto probe = [](destroy_count_t) -> task<void> { co_return; };
        auto t = probe(destroy_count_t(down));
        continuation c{t.handle()};
        t.release();
        auto ctx = std::make_unique<closable_test_context>();
        auto s = std::make_unique<strand<reentrant_executor>>(
            reentrant_executor{&q, ctx.get(), &fails, &on_fail});
        on_fail = [&] { ctx->shutdown(); };
        bool threw = false;
        try
        {
            s->post(c);
        }
        catch(std::runtime_error const&)
        {
            threw = true;
        }
        BOOST_TEST(!threw);
        BOOST_TEST_EQ(down, 1);
        BOOST_TEST(q.empty());
        s.reset();
        ctx.reset();
    }

    // When the retried invoker for work queued behind a failed post
    // also fails after a shutdown took that work, the caller keeps its
    // own continuation and the shutdown has destroyed the rest.
    void
    testRetryThrowsAfterShutdownTookFollowers()
    {
        int held_down = 0, behind_down = 0;
        int fails = 2;
        std::queue<continuation> q;
        std::function<void()> on_fail;
        auto probe = [](destroy_count_t) -> task<void> { co_return; };
        {
            auto held = probe(destroy_count_t(held_down));
            auto behind = probe(destroy_count_t(behind_down));
            continuation c1{held.handle()};
            continuation c2{behind.handle()};
            behind.release();
            auto ctx = std::make_unique<closable_test_context>();
            auto s = std::make_unique<strand<reentrant_executor>>(
                reentrant_executor{&q, ctx.get(), &fails, &on_fail});
            on_fail = [&]
            {
                s->post(c2);
                on_fail = [&] { ctx->shutdown(); };
            };
            BOOST_TEST_THROWS(s->post(c1), std::runtime_error);
            BOOST_TEST_EQ(held_down, 0);
            BOOST_TEST_EQ(behind_down, 1);
            BOOST_TEST(q.empty());
            s.reset();
            ctx.reset();
        }
        BOOST_TEST_EQ(held_down, 1);
    }

    // A strand made after its service shut down is closed from the
    // start: what is posted to it is destroyed, and its handle can be
    // dropped after the context is gone.
    void
    testStrandCreatedAfterShutdownIsClosed()
    {
        int down = 0;
        std::queue<continuation> q;
        auto probe = [](destroy_count_t) -> task<void> { co_return; };
        auto t = probe(destroy_count_t(down));
        continuation c{t.handle()};
        t.release();
        auto ctx = std::make_unique<closable_test_context>();
        queuing_executor qe(q);
        qe.ctx_ = ctx.get();
        strand<queuing_executor> before{qe};
        ctx->shutdown();
        auto after = std::make_unique<strand<queuing_executor>>(qe);
        after->post(c);
        BOOST_TEST_EQ(down, 1);
        BOOST_TEST(q.empty());
        ctx.reset();
        after.reset();
    }

    // The same when the strand service itself is first created after
    // the shutdown.
    void
    testStrandServiceCreatedAfterShutdown()
    {
        int down = 0;
        std::queue<continuation> q;
        auto probe = [](destroy_count_t) -> task<void> { co_return; };
        auto t = probe(destroy_count_t(down));
        continuation c{t.handle()};
        t.release();
        auto ctx = std::make_unique<closable_test_context>();
        queuing_executor qe(q);
        qe.ctx_ = ctx.get();
        ctx->shutdown();
        auto s = std::make_unique<strand<queuing_executor>>(qe);
        s->post(c);
        BOOST_TEST_EQ(down, 1);
        BOOST_TEST(q.empty());
        ctx.reset();
        s.reset();
    }

    // An older service's destructor runs after the strand service is
    // gone; a post from it is destroyed without touching that service.
    void
    testPostAfterServiceDestroyed()
    {
        static int down;
        down = 0;
        struct older_service : execution_context::service
        {
            std::queue<continuation> q;
            std::optional<strand<queuing_executor>> s;
            continuation c;

            explicit older_service(execution_context&) {}

            ~older_service()
            {
                auto probe = [](destroy_count_t) -> task<void>
                {
                    co_return;
                };
                auto t = probe(destroy_count_t(down));
                c.h = t.handle();
                t.release();
                s->post(c);
            }

            void shutdown() override {}
        };
        {
            test_io_context ctx;
            auto& svc = ctx.make_service<older_service>();
            queuing_executor qe(svc.q);
            qe.ctx_ = &ctx;
            svc.s.emplace(qe);
        }
        BOOST_TEST_EQ(down, 1);
    }

    // A strand whose service has shut down abandons what is posted to
    // it, since nothing will ever run it.
    void
    testPostAfterShutdownDestroyed()
    {
        int down = 0;
        std::queue<continuation> q;
        auto probe = [](destroy_count_t) -> task<void> { co_return; };
        auto t = probe(destroy_count_t(down));
        continuation c{t.handle()};
        t.release();
        auto ctx = std::make_unique<closable_test_context>();
        queuing_executor qe(q);
        qe.ctx_ = ctx.get();
        strand<queuing_executor> s{qe};
        ctx->shutdown();
        s.post(c);
        BOOST_TEST_EQ(down, 1);
        BOOST_TEST(q.empty());
    }

    // Coroutines queued behind a drained invoker have no runner once
    // the strand is dropped; the strand destroys them.
    void
    testQueuedCoroutinesDestroyedWithStrand()
    {
        int a_down = 0, b_down = 0;
        std::queue<continuation> q;
        auto probe = [](destroy_count_t) -> task<void> { co_return; };
        auto ta = probe(destroy_count_t(a_down));
        auto tb = probe(destroy_count_t(b_down));
        continuation ca{ta.handle()};
        continuation cb{tb.handle()};
        ta.release();
        tb.release();
        {
            strand<queuing_executor> s{queuing_executor(q)};
            s.post(ca);
            s.post(cb);
            BOOST_TEST_EQ(q.size(), 1u);
            capy::abandon(q.front());
            q.pop();
        }
        BOOST_TEST_EQ(a_down, 1);
        BOOST_TEST_EQ(b_down, 1);
    }

    // A frame's parameter holds the last strand handle, so tearing the
    // frame down destroys a parented coroutine queued on the strand
    // while the frame's own parent is still deferred.
    void
    testStrandParameterDestroysQueuedParentedCoroutine()
    {
        int pa_down = 0, pb_down = 0;
        parked slot_q, slot_f;
        std::queue<continuation> q;
        auto qa = [](parked* s) -> task<void>
        {
            co_await park_awaitable{s};
        };
        auto pa = [qa](parked* s, destroy_count_t)
            -> task<void>
        {
            co_await qa(s);
        };
        auto fb = [](strand<queuing_executor>, parked* s)
            -> task<void>
        {
            co_await park_awaitable{s};
        };
        auto pb = [fb](
            strand<queuing_executor> st,
            parked* s,
            destroy_count_t) -> task<void>
        {
            co_await fb(std::move(st), s);
        };
        int n = 0;
        test_executor ex(n);
        run_async(ex)(pa(&slot_q, destroy_count_t(pa_down)));
        BOOST_TEST(slot_q);

        continuation c{slot_q};
        {
            strand<queuing_executor> s{queuing_executor(q)};
            s.post(c);
            BOOST_TEST_EQ(q.size(), 1u);
            capy::abandon(q.front());
            q.pop();
            run_async(ex)(pb(std::move(s), &slot_f, destroy_count_t(pb_down)));
        }
        BOOST_TEST(slot_f);
        BOOST_TEST_EQ(pa_down, 0);

        capy::abandon(slot_f);
        BOOST_TEST_EQ(pa_down, 1);
        BOOST_TEST_EQ(pb_down, 1);
    }

    // The last strand handle sits in a frame that an outer teardown
    // destroys, so every queued coroutine and its parent are destroyed
    // inside that teardown, however many there are.
    void
    testCascadeDropsLargeStrandBatch()
    {
        constexpr std::size_t count = 64;
        int parents_down = 0, holder_down = 0;
        std::array<parked, count> slots{};
        std::array<continuation, count> conts{};
        parked slot_leaf;
        std::queue<continuation> q;
        auto queued = [](parked* s) -> task<void>
        {
            co_await park_awaitable{s};
        };
        auto parent = [queued](parked* s, destroy_count_t)
            -> task<void>
        {
            co_await queued(s);
        };
        auto leaf = [](parked* s) -> task<void>
        {
            co_await park_awaitable{s};
        };
        auto holder = [leaf](
            strand<queuing_executor>,
            parked* s,
            destroy_count_t) -> task<void>
        {
            co_await leaf(s);
        };
        int n = 0;
        test_executor ex(n);
        for(std::size_t i = 0; i < count; ++i)
            run_async(ex)(parent(&slots[i], destroy_count_t(parents_down)));

        {
            strand<queuing_executor> s{queuing_executor(q)};
            for(std::size_t i = 0; i < count; ++i)
            {
                conts[i] = slots[i];
                s.post(conts[i]);
            }
            BOOST_TEST_EQ(q.size(), 1u);
            capy::abandon(q.front());
            q.pop();
            run_async(ex)(holder(
                std::move(s), &slot_leaf, destroy_count_t(holder_down)));
        }
        BOOST_TEST(slot_leaf);
        BOOST_TEST_EQ(parents_down, 0);

        capy::abandon(slot_leaf);
        BOOST_TEST_EQ(parents_down, static_cast<int>(count));
        BOOST_TEST_EQ(holder_down, 1);
    }

    void
    run()
    {
        testShutdownDestroysQueuedAcrossStrands();
        testShutdownLeavesNothingForStrandDestructor();
        testShutdownDestroysQueuedParentedChain();
        testShutdownReleasesLiveStrand();
        testConstruct();
        testCopy();
        testMove();
        testGetInnerExecutor();
        testContext();
        testWorkTracking();
        testEquality();
        testNoEqualityCollisions();
        testStrandsAreIndependent();
        testTransientStrandLifetime();
        testManyStrandsStress();
        testMutexPoolCollisionIsolation();
        testRunningInThisThread();
        testPost();
        testDispatch();
        testMultipleWork();
        testConcurrentPost();
        testFifoOrder();
        testSerialization();
        testExitStrandAfterRun();
        testAnyExecutor();
        testQueuedCoroutinesDestroyedWithStrand();
        testPostAfterShutdownDestroyed();
        testStrandCreatedAfterShutdownIsClosed();
        testFirstPostRacesShutdown();
        testInnerPostThrows();
        testPostQueuedBehindThrowingPost();
        testPostThrowsAfterShutdownTookIt();
        testRetryThrowsAfterShutdownTookFollowers();
        testStrandServiceCreatedAfterShutdown();
        testPostAfterServiceDestroyed();
        testStrandParameterDestroysQueuedParentedCoroutine();
        testCascadeDropsLargeStrandBatch();
    }
};

TEST_SUITE(
    strand_test,
    "boost.capy.strand");

} // capy
} // boost
