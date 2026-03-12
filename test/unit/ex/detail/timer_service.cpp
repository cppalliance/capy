//
// Copyright (c) 2026 Michael Vandeberg
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Test that header file is self-contained.
#include <boost/capy/ex/detail/timer_service.hpp>

#include <boost/capy/ex/thread_pool.hpp>

#include "test_helpers.hpp"

#include <atomic>
#include <latch>

namespace boost {
namespace capy {

using namespace std::chrono_literals;

struct timer_service_test
{
    // Test: timer fires after duration
    void
    testBasicFire()
    {
        thread_pool pool(1);
        auto& ts = pool.get_executor().context()
            .use_service<detail::timer_service>();

        std::latch done(1);
        bool fired = false;

        detail::timer_service::timer_id id;
        ts.schedule_after(1ms, [&] {
            fired = true;
            done.count_down();
        }, id);

        done.wait();
        BOOST_TEST(fired);
    }

    // Test: cancel prevents callback from firing
    void
    testCancelBeforeFire()
    {
        thread_pool pool(1);
        auto& ts = pool.get_executor().context()
            .use_service<detail::timer_service>();

        bool fired = false;

        detail::timer_service::timer_id id;
        ts.schedule_after(1s, [&] {
            fired = true;
        }, id);

        ts.cancel(id);

        // Give some time to confirm it doesn't fire
        std::this_thread::sleep_for(20ms);
        BOOST_TEST(!fired);
    }

    // Test: cancel on already-fired timer is safe
    void
    testCancelAfterFire()
    {
        thread_pool pool(1);
        auto& ts = pool.get_executor().context()
            .use_service<detail::timer_service>();

        std::latch done(1);

        detail::timer_service::timer_id id;
        ts.schedule_after(1ms, [&] {
            done.count_down();
        }, id);

        done.wait();
        // Should not block or crash
        ts.cancel(id);
    }

    // Test: multiple timers fire in deadline order
    void
    testFiringOrder()
    {
        thread_pool pool(1);
        auto& ts = pool.get_executor().context()
            .use_service<detail::timer_service>();

        std::vector<int> order;
        std::mutex mu;
        std::latch done(3);

        auto const scale = failsafe_scale;

        detail::timer_service::timer_id id;
        ts.schedule_after(30ms * scale, [&] {
            std::lock_guard lock(mu);
            order.push_back(3);
            done.count_down();
        }, id);
        ts.schedule_after(10ms * scale, [&] {
            std::lock_guard lock(mu);
            order.push_back(1);
            done.count_down();
        }, id);
        ts.schedule_after(20ms * scale, [&] {
            std::lock_guard lock(mu);
            order.push_back(2);
            done.count_down();
        }, id);

        done.wait();
        BOOST_TEST_EQ(order.size(), 3u);
        BOOST_TEST_EQ(order[0], 1);
        BOOST_TEST_EQ(order[1], 2);
        BOOST_TEST_EQ(order[2], 3);
    }

    // Test: zero duration fires promptly
    void
    testZeroDuration()
    {
        thread_pool pool(1);
        auto& ts = pool.get_executor().context()
            .use_service<detail::timer_service>();

        std::latch done(1);
        auto start = std::chrono::steady_clock::now();

        detail::timer_service::timer_id id;
        ts.schedule_after(0ms, [&] {
            done.count_down();
        }, id);

        done.wait();
        auto elapsed = std::chrono::steady_clock::now() - start;
        BOOST_TEST(elapsed < 100ms);
    }

    // Test: many timers scheduled concurrently
    void
    testManyConcurrent()
    {
        thread_pool pool(1);
        auto& ts = pool.get_executor().context()
            .use_service<detail::timer_service>();

        constexpr int N = 100;
        std::atomic<int> count{0};
        std::latch done(N);

        detail::timer_service::timer_id id;
        for(int i = 0; i < N; ++i)
        {
            ts.schedule_after(1ms, [&] {
                count.fetch_add(1, std::memory_order_relaxed);
                done.count_down();
            }, id);
        }

        done.wait();
        BOOST_TEST_EQ(count.load(), N);
    }

    // Test: cancel subset of timers
    void
    testCancelSubset()
    {
        thread_pool pool(1);
        auto& ts = pool.get_executor().context()
            .use_service<detail::timer_service>();

        std::atomic<int> count{0};
        std::latch done(1);

        detail::timer_service::timer_id id1;
        ts.schedule_after(10ms, [&] {
            count.fetch_add(1, std::memory_order_relaxed);
        }, id1);
        detail::timer_service::timer_id id2;
        ts.schedule_after(10ms, [&] {
            count.fetch_add(1, std::memory_order_relaxed);
            done.count_down();
        }, id2);
        detail::timer_service::timer_id id3;
        ts.schedule_after(10ms, [&] {
            count.fetch_add(1, std::memory_order_relaxed);
        }, id3);

        ts.cancel(id1);
        ts.cancel(id3);

        // Wait for the uncancelled timer to fire
        done.wait();
        // Give time for any incorrectly-uncancelled timers
        std::this_thread::sleep_for(20ms);
        BOOST_TEST_EQ(count.load(), 1);
    }

    // Test: shutdown with pending timers doesn't crash
    void
    testShutdownWithPending()
    {
        {
            thread_pool pool(1);
            auto& ts = pool.get_executor().context()
                .use_service<detail::timer_service>();

            // Schedule timers far in the future
            detail::timer_service::timer_id id;
            ts.schedule_after(10s, [] {}, id);
            ts.schedule_after(10s, [] {}, id);
            ts.schedule_after(10s, [] {}, id);

            // pool destructor calls shutdown — should not hang
        }
        BOOST_TEST(true);
    }

    // Test: timer fires at or after the specified duration
    void
    testFiresAtOrAfter()
    {
        thread_pool pool(1);
        auto& ts = pool.get_executor().context()
            .use_service<detail::timer_service>();

        std::latch done(1);
        auto start = std::chrono::steady_clock::now();
        auto dur = 50ms;

        detail::timer_service::timer_id id;
        ts.schedule_after(dur, [&] {
            done.count_down();
        }, id);

        done.wait();
        auto elapsed = std::chrono::steady_clock::now() - start;
        BOOST_TEST(elapsed >= dur);
    }

    // Test: cancel blocks while callback is executing
    void
    testCancelBlocksDuringExecution()
    {
        thread_pool pool(1);
        auto& ts = pool.get_executor().context()
            .use_service<detail::timer_service>();

        std::atomic<bool> callback_started{false};
        std::atomic<bool> callback_finished{false};
        std::latch started(1);

        detail::timer_service::timer_id id;
        ts.schedule_after(1ms, [&] {
            callback_started.store(true);
            started.count_down();
            std::this_thread::sleep_for(50ms);
            callback_finished.store(true);
        }, id);

        // Wait for callback to start executing
        started.wait();
        BOOST_TEST(callback_started.load());

        // cancel() must block until callback finishes
        ts.cancel(id);
        BOOST_TEST(callback_finished.load());
    }

    void
    run()
    {
        testBasicFire();
        testCancelBeforeFire();
        testCancelAfterFire();
        testFiringOrder();
        testZeroDuration();
        testManyConcurrent();
        testCancelSubset();
        testShutdownWithPending();
        testFiresAtOrAfter();
        testCancelBlocksDuringExecution();
    }
};

TEST_SUITE(timer_service_test, "capy.ex.timer_service");

} // capy
} // boost