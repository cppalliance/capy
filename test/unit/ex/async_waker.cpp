//
// Copyright (c) 2026 Steve Gerbino
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Test that header file is self-contained.
#include <boost/capy/ex/async_waker.hpp>

#include <boost/capy/cond.hpp>
#include <boost/capy/ex/run_async.hpp>
#include <boost/capy/ex/thread_pool.hpp>
#include <boost/capy/task.hpp>

#include <chrono>
#include <stop_token>
#include <thread>

#include "test_suite.hpp"

namespace boost {
namespace capy {

struct async_waker_test
{
    // wake() before wait(): the token is latched and the
    // wait completes immediately (the race-free escape hatch).
    void testLatchedTokenBeforeWait()
    {
        thread_pool pool(1);
        async_waker waker;
        bool ok = false;

        waker.wake();

        auto t = [](async_waker& waker, bool& ok_out) -> task<> {
            auto [ec] = co_await waker.wait();
            ok_out = !ec;
        };
        run_async(pool.get_executor())(t(waker, ok));
        pool.join();
        BOOST_TEST(ok);
    }

    void testWakeAfterWaitResumes()
    {
        thread_pool pool(1);
        async_waker waker;
        bool ok = false;

        auto waiter = [](async_waker& waker, bool& ok_out) -> task<> {
            auto [ec] = co_await waker.wait();
            ok_out = !ec;
        };
        auto wake_task = [](async_waker& waker) -> task<> {
            waker.wake();
            co_return;
        };
        run_async(pool.get_executor())(waiter(waker, ok));
        run_async(pool.get_executor())(wake_task(waker));
        pool.join();
        BOOST_TEST(ok);
    }

    // The core use case: a user-provided thread wakes into the
    // executor. The waiter must resume on the executor, not on
    // the waking thread.
    void testCrossThreadWake()
    {
        thread_pool pool(1);
        async_waker waker;
        bool ok = false;
        std::thread::id resume_id;
        std::thread::id waker_id;

        auto t = [](async_waker& waker, bool& ok_out,
            std::thread::id& resume_id_out) -> task<> {
            auto [ec] = co_await waker.wait();
            resume_id_out = std::this_thread::get_id();
            ok_out = !ec;
        };
        run_async(pool.get_executor())(t(waker, ok, resume_id));

        std::thread th([&waker, &waker_id] {
            waker_id = std::this_thread::get_id();
            std::this_thread::sleep_for(
                std::chrono::milliseconds(20));
            waker.wake();
        });
        pool.join();
        th.join();
        BOOST_TEST(ok);
        // Pins the resume locus: the pool has one thread, so a
        // mismatch here would mean the wait resumed inline on the
        // waking thread instead of being posted through the executor.
        BOOST_TEST(resume_id != waker_id);
    }

    void testStopCancelsWait()
    {
        thread_pool pool(1);
        async_waker waker;
        std::stop_source src;
        bool canceled = false;

        auto t = [](async_waker& waker, bool& out) -> task<> {
            auto [ec] = co_await waker.wait();
            out = (ec == cond::canceled);
        };
        run_async(pool.get_executor(), src.get_token())(t(waker, canceled));

        std::thread th([&src] {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(20));
            src.request_stop();
        });
        pool.join();
        th.join();
        BOOST_TEST(canceled);
    }

    void testAlreadyStoppedCompletesCanceled()
    {
        thread_pool pool(1);
        async_waker waker;
        std::stop_source src;
        src.request_stop();
        bool canceled = false;

        auto t = [](async_waker& waker, bool& out) -> task<> {
            auto [ec] = co_await waker.wait();
            out = (ec == cond::canceled);
        };
        run_async(pool.get_executor(), src.get_token())(t(waker, canceled));
        pool.join();
        BOOST_TEST(canceled);
    }

    // Token latched while nobody waits survives a completed wait:
    // wake, wait (consumes), wake, wait (consumes again).
    void testReuseAfterCompletion()
    {
        thread_pool pool(1);
        async_waker waker;
        int count = 0;

        auto t = [](async_waker& waker, int& count_out) -> task<> {
            waker.wake();
            {
                auto [ec] = co_await waker.wait();
                if(!ec)
                    ++count_out;
            }
            waker.wake();
            {
                auto [ec] = co_await waker.wait();
                if(!ec)
                    ++count_out;
            }
        };
        run_async(pool.get_executor())(t(waker, count));
        pool.join();
        BOOST_TEST_EQ(count, 2);
    }

    // Extra wakes collapse into one token (single-token latch,
    // not a counting semaphore). With no token left, the second
    // wait can only complete via cancellation; a leftover token
    // would instead resume it with success, and quickly.
    void testExtraWakesCollapse()
    {
        thread_pool pool(1);
        async_waker waker;
        bool first_ok = false;
        bool second_canceled = false;

        waker.wake();
        waker.wake();
        waker.wake();

        auto t = [](async_waker& waker,
            bool& ok1, bool& canceled2) -> task<> {
            auto [ec1] = co_await waker.wait();  // consumes the one token
            ok1 = !ec1;
            auto [ec2] = co_await waker.wait();  // no token left
            canceled2 = (ec2 == cond::canceled);
        };

        std::stop_source src;
        run_async(pool.get_executor(), src.get_token())(
            t(waker, first_ok, second_canceled));

        // Give the pool time to reach the second wait, then stop
        // it so join() returns.
        std::thread th([&src] {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(50));
            src.request_stop();
        });
        pool.join();
        th.join();
        BOOST_TEST(first_ok);
        BOOST_TEST(second_canceled);
    }

    // wake() racing request_stop: exactly one side must win;
    // if cancel wins, the token is re-latched, not lost.
    void testWakeCancelRace()
    {
        for(int i = 0; i < 100; ++i)
        {
            thread_pool pool(1);
            async_waker waker;
            std::stop_source src;
            bool resumed = false;
            bool canceled = false;

            auto t = [](async_waker& waker, bool& r, bool& c) -> task<> {
                auto [ec] = co_await waker.wait();
                if(ec == cond::canceled)
                    c = true;
                else
                    r = true;
            };
            run_async(pool.get_executor(), src.get_token())(t(waker, resumed, canceled));

            std::thread t1([&waker] { waker.wake(); });
            std::thread t2([&src] { src.request_stop(); });
            t1.join();
            t2.join();
            pool.join();

            BOOST_TEST(resumed != canceled);
            if(canceled)
            {
                // Cancel claimed the waiter, so the wake must
                // have latched instead of vanishing: a fresh wait
                // completes immediately with no further wake.
                thread_pool pool2(1);
                bool token_latched = false;
                auto probe = [](async_waker& waker,
                    bool& out) -> task<> {
                    auto [ec] = co_await waker.wait();
                    out = !ec;
                };
                run_async(pool2.get_executor())(probe(waker, token_latched));
                pool2.join();
                BOOST_TEST(token_latched);
            }
        }
    }

    void run()
    {
        testLatchedTokenBeforeWait();
        testWakeAfterWaitResumes();
        testCrossThreadWake();
        testStopCancelsWait();
        testAlreadyStoppedCompletesCanceled();
        testReuseAfterCompletion();
        testExtraWakesCollapse();
        testWakeCancelRace();
    }
};

TEST_SUITE(async_waker_test, "boost.capy.ex.async_waker");

} // namespace capy
} // namespace boost
