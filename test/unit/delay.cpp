//
// Copyright (c) 2026 Michael Vandeberg
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Test that header file is self-contained.
#include <boost/capy/delay.hpp>

#include <boost/capy/ex/run_async.hpp>
#include <boost/capy/ex/thread_pool.hpp>
#include <boost/capy/task.hpp>

#include "test_helpers.hpp"
#include "test_suite.hpp"

#include <latch>
#include <memory_resource>
#include <stop_token>

namespace boost {
namespace capy {

using namespace std::chrono_literals;

struct delay_test
{
    // Test: delay completes after duration
    void
    testDelayCompletes()
    {
        thread_pool pool(1);
        std::latch done(1);
        bool completed = false;

        auto delay_task = [&]() -> task<void>
        {
            (void) co_await delay(10ms);
            completed = true;
        };

        run_async(pool.get_executor(),
            [&]() {
                done.count_down();
            },
            [&](std::exception_ptr) {
                done.count_down();
            })(delay_task());

        done.wait();
        BOOST_TEST(completed);
    }

    // Test: delay waits at least the specified duration
    void
    testDelayMinimumDuration()
    {
        thread_pool pool(1);
        std::latch done(1);

        auto delay_task = [&]() -> task<void>
        {
            (void) co_await delay(50ms);
        };

        auto start = std::chrono::steady_clock::now();

        run_async(pool.get_executor(),
            [&]() {
                done.count_down();
            },
            [&](std::exception_ptr) {
                done.count_down();
            })(delay_task());

        done.wait();
        auto elapsed = std::chrono::steady_clock::now() - start;
        BOOST_TEST(elapsed >= 50ms);
    }

    // Test: stop requested before delay suspends (early-out path)
    void
    testDelayCancellationEarlyOut()
    {
        thread_pool pool(1);
        std::latch done(1);
        std::stop_source source;

        auto delay_task = [&]() -> task<void>
        {
            (void) co_await delay(10s);
        };

        auto start = std::chrono::steady_clock::now();

        run_async(pool.get_executor(), source.get_token(),
            [&]() {
                done.count_down();
            },
            [&](std::exception_ptr) {
                done.count_down();
            })(delay_task());

        // Cancel immediately — likely before delay suspends
        source.request_stop();

        done.wait();
        auto elapsed = std::chrono::steady_clock::now() - start;
        BOOST_TEST(elapsed < 1s);
    }

    // Test: stop requested after delay is fully suspended
    //       (exercises cancel_fn stop callback path)
    void
    testDelayCancellationWhileSuspended()
    {
        thread_pool pool(1);
        std::latch done(1);
        std::latch suspended(1);
        std::stop_source source;

        auto delay_task = [&]() -> task<void>
        {
            // Signal that we're about to suspend on delay
            suspended.count_down();
            (void) co_await delay(10s);
        };

        auto start = std::chrono::steady_clock::now();

        run_async(pool.get_executor(), source.get_token(),
            [&]() {
                done.count_down();
            },
            [&](std::exception_ptr) {
                done.count_down();
            })(delay_task());

        // Wait for the task to reach the delay point
        suspended.wait();
        // Small sleep to ensure delay_awaitable::await_suspend
        // has fully completed (stop callback registered)
        std::this_thread::sleep_for(10ms);
        source.request_stop();

        done.wait();
        auto elapsed = std::chrono::steady_clock::now() - start;
        BOOST_TEST(elapsed < 1s);
    }

    // Test: zero-duration delay completes immediately
    void
    testZeroDuration()
    {
        thread_pool pool(1);
        std::latch done(1);
        bool completed = false;

        auto delay_task = [&]() -> task<void>
        {
            (void) co_await delay(0ms);
            completed = true;
        };

        run_async(pool.get_executor(),
            [&]() {
                done.count_down();
            },
            [&](std::exception_ptr) {
                done.count_down();
            })(delay_task());

        done.wait();
        BOOST_TEST(completed);
    }

    // Test: multiple sequential delays
    void
    testSequentialDelays()
    {
        thread_pool pool(1);
        std::latch done(1);
        int step = 0;

        auto delay_task = [&]() -> task<void>
        {
            (void) co_await delay(5ms);
            step = 1;
            (void) co_await delay(5ms);
            step = 2;
            (void) co_await delay(5ms);
            step = 3;
        };

        run_async(pool.get_executor(),
            [&]() {
                done.count_down();
            },
            [&](std::exception_ptr) {
                done.count_down();
            })(delay_task());

        done.wait();
        BOOST_TEST_EQ(step, 3);
    }

    // Test: destroying delay_awaitable while suspended
    //       cleans up both stop callback and timer
    void
    testDestroyWhileSuspended()
    {
// GCC emits a false -Wmaybe-uninitialized when it inlines
// the stop_callback destructor through the alignas buffer.
#if defined(__GNUC__) && !defined(__clang__)
# pragma GCC diagnostic push
# pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif
        thread_pool pool(1);
        auto ex = pool.get_executor();
        std::stop_source source;
        io_env env{ex, source.get_token(),
            std::pmr::get_default_resource()};

        {
            delay_awaitable da(std::chrono::seconds(10));
            // Manually suspend — registers timer and stop callback
            da.await_suspend(std::noop_coroutine(), &env);
            // da destroyed here without calling await_resume
        }

        // If cleanup was incomplete, requesting stop or waiting
        // for the timer would access freed memory (UB/crash).
        source.request_stop();
        std::this_thread::sleep_for(20ms);
        BOOST_TEST(true);
#if defined(__GNUC__) && !defined(__clang__)
# pragma GCC diagnostic pop
#endif
    }

    // Test: concurrent delays on a multi-threaded pool
    //       exercises use_service race and shared timer_service
    void
    testConcurrentDelays()
    {
        constexpr int N = 10;
        thread_pool pool(4);
        std::latch done(N);

        auto delay_task = [](int i) -> task<void>
        {
            (void) co_await delay(10ms * i);
        };

        for(int i = 0; i < N; ++i)
        {
            run_async(pool.get_executor(),
                [&]() { done.count_down(); },
                [&](std::exception_ptr) {
                    done.count_down();
                })(delay_task(i));
        }

        done.wait();
        BOOST_TEST(true);
    }

    void
    run()
    {
        testDelayCompletes();
        testDelayMinimumDuration();
        testDelayCancellationEarlyOut();
        testDelayCancellationWhileSuspended();
        testZeroDuration();
        testSequentialDelays();
        testDestroyWhileSuspended();
        testConcurrentDelays();
    }
};

TEST_SUITE(delay_test, "capy.delay");

} // capy
} // boost
