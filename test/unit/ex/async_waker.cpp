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
#include <boost/capy/ex/abandon.hpp>
#include <boost/capy/ex/run_async.hpp>
#include <boost/capy/ex/thread_pool.hpp>
#include <boost/capy/task.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <queue>
#include <stop_token>
#include <thread>
#include <tuple>

#include "test_helpers.hpp"

namespace boost {
namespace capy {

namespace {

// One thread that resumes what is posted to it, and reports when it
// has nothing left to run.
class worker
{
    std::mutex m_;
    std::condition_variable cv_;
    std::deque<continuation> q_;
    bool running_ = false;
    bool stop_ = false;
    std::thread t_;

    void loop()
    {
        std::unique_lock<std::mutex> lk(m_);
        for(;;)
        {
            cv_.wait(lk, [&]{ return stop_ || !q_.empty(); });
            if(q_.empty())
                return;
            auto c = q_.front();
            q_.pop_front();
            running_ = true;
            lk.unlock();
            c.h.resume();
            lk.lock();
            running_ = false;
        }
    }

public:
    ~worker()
    {
        {
            std::lock_guard<std::mutex> lk(m_);
            stop_ = true;
        }
        cv_.notify_one();
        t_.join();
    }

    worker()
        : t_([this]{ loop(); })
    {
    }

    bool idle()
    {
        std::lock_guard<std::mutex> lk(m_);
        return q_.empty() && !running_;
    }

    void push(continuation const& c)
    {
        {
            std::lock_guard<std::mutex> lk(m_);
            q_.push_back(c);
        }
        cv_.notify_one();
    }
};

struct worker_executor
{
    worker* w_;

    bool operator==(worker_executor const& o) const noexcept
    {
        return w_ == o.w_;
    }
    test_io_context& context() const noexcept
    {
        return default_test_io_context();
    }
    void on_work_started() const noexcept {}
    void on_work_finished() const noexcept {}
    std::coroutine_handle<> dispatch(continuation& c) const
    {
        w_->push(c);
        return std::noop_coroutine();
    }
    void post(continuation& c) const
    {
        w_->push(c);
    }
};

} // namespace

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

    // A waiting frame destroyed without resuming leaves the waker
    // empty, so a later wake latches a token for the next wait.
    void testDestroyWaitingFrame()
    {
        async_waker waker;
        std::queue<continuation> q;
        queuing_executor ex(q);
        int down = 0;
        auto t = [](async_waker& wk, destroy_count_t) -> task<void>
        {
            std::ignore = co_await wk.wait();
        }(waker, destroy_count_t(down));
        auto h = t.handle();
        t.release();
        io_env env;
        env.executor = executor_ref(ex);
        h.promise().set_environment(&env);
        h.resume();
        BOOST_TEST_EQ(down, 0);
        h.destroy();
        BOOST_TEST_EQ(down, 1);

        waker.wake();
        BOOST_TEST(q.empty());
        std::error_code ec = make_error_code(error::canceled);
        auto next = [](async_waker& wk, std::error_code& out) -> task<void>
        {
            auto [e] = co_await wk.wait();
            out = e;
        };
        run_async(ex)(next(waker, ec));
        pump(q);
        BOOST_TEST(!ec);
    }

    // A destroyed waker can no longer wake its waiter, so the waiter
    // resumes with an error instead of disappearing.
    void testDestroyWakerCancelsWaiter()
    {
        int down = 0;
        std::error_code ec;
        auto w = std::make_unique<async_waker>();
        auto waiter = [](async_waker& wk, std::error_code& out,
            destroy_count_t) -> task<void>
        {
            auto [e] = co_await wk.wait();
            out = e;
        };
        std::queue<continuation> q;
        run_async(queuing_executor(q))(waiter(*w, ec, destroy_count_t(down)));
        pump(q);
        w.reset();
        BOOST_TEST_EQ(down, 0);
        BOOST_TEST_EQ(q.size(), 1u);
        pump(q);
        BOOST_TEST(ec == make_error_code(error::canceled));
        BOOST_TEST_EQ(down, 1);
    }

    // A stop requested while the waiter is suspending is never lost:
    // either the stop callback or the waiter's own re-check cancels it.
    void testStopRacesWait()
    {
        thread_pool pool(1);
        async_waker w;
        std::atomic<int> done{0};
        auto waiter = [](async_waker& x, std::atomic<int>& d,
            std::atomic<bool>& en) -> task<void>
        {
            en.store(true);
            std::ignore = co_await x.wait();
            d.fetch_add(1);
        };
        for(int i = 0; i < 2000; ++i)
        {
            std::stop_source ss;
            std::atomic<bool> entered{false};
            run_async(pool.get_executor(), ss.get_token(),
                std::allocator<std::byte>{})(waiter(w, done, entered));
            while(!entered.load())
                std::this_thread::yield();
            ss.request_stop();
            // Only a watchdog: a lost cancel never completes.
            auto const limit = std::chrono::steady_clock::now() +
                std::chrono::seconds(30);
            while(done.load() != i + 1 &&
                std::chrono::steady_clock::now() < limit)
                std::this_thread::yield();
            if(!BOOST_TEST_EQ(done.load(), i + 1))
                break;
        }
        pool.join();
    }

    // A woken waiter torn down before it resumes, then the waker.
    void testWokenWaiterDestroyedThenWakerDestroyed()
    {
        int down = 0;
        auto p = std::make_unique<async_waker>();
        std::queue<continuation> q;
        auto waiter = [](async_waker& x, destroy_count_t) -> task<void>
        {
            std::ignore = co_await x.wait();
        };
        run_async(queuing_executor(q), std::allocator<std::byte>{})(
            waiter(*p, destroy_count_t(down)));
        pump(q);
        p->wake();
        BOOST_TEST_EQ(q.size(), 1u);
        capy::abandon(q.front());
        q.pop();
        pump(q);
        p.reset();
        BOOST_TEST_EQ(down, 1);
    }

    // The waker may die on the waking thread right after wake(), while
    // the waiter resumes on its own thread.
    void testWakerDestroyedWhileWokenWaiterResumes()
    {
        worker wk;
        worker_executor ex{&wk};
        for(int i = 0; i < 300; ++i)
        {
            auto w = std::make_unique<async_waker>();
            std::atomic<bool> entered{false};
            std::atomic<bool> done{false};
            auto waiter = [](async_waker& x, std::atomic<bool>& en,
                std::atomic<bool>& d) -> task<void>
            {
                en.store(true);
                std::ignore = co_await x.wait();
                d.store(true);
            };
            run_async(ex, std::allocator<std::byte>{})(
                waiter(*w, entered, done));
            // Entered and then idle: suspended on the waker.
            while(!entered.load() || !wk.idle())
                std::this_thread::yield();
            w->wake();
            w.reset();
            // Only a watchdog: a waiter that is never resumed hangs.
            auto const limit = std::chrono::steady_clock::now() +
                std::chrono::seconds(30);
            while(!done.load() &&
                std::chrono::steady_clock::now() < limit)
                std::this_thread::yield();
            if(!BOOST_TEST(done.load()))
                break;
        }
    }

    // A stop requested after the waker that woke the waiter is gone
    // finds no callback reaching into it.
    void testStopAfterWakeAndWakerDestroyed()
    {
        std::error_code ec = make_error_code(error::canceled);
        auto w = std::make_unique<async_waker>();
        std::stop_source ss;
        auto waiter = [](async_waker& wk, std::error_code& out) -> task<void>
        {
            auto [e] = co_await wk.wait();
            out = e;
        };
        std::queue<continuation> q;
        run_async(queuing_executor(q), ss.get_token(),
            std::allocator<std::byte>{})(waiter(*w, ec));
        pump(q);
        w->wake();
        BOOST_TEST_EQ(q.size(), 1u);
        w.reset();
        ss.request_stop();
        BOOST_TEST_EQ(q.size(), 1u);
        pump(q);
        BOOST_TEST(!ec);
    }

    // The waiter's stop callback reaches into the waker, so a stop
    // requested after the waker is gone must find it deregistered.
    void testStopAfterWakerDestroyed()
    {
        std::error_code ec;
        auto w = std::make_unique<async_waker>();
        std::stop_source ss;
        auto waiter = [](async_waker& wk, std::error_code& out) -> task<void>
        {
            auto [e] = co_await wk.wait();
            out = e;
        };
        std::queue<continuation> q;
        run_async(queuing_executor(q), ss.get_token())(waiter(*w, ec));
        pump(q);
        w.reset();
        ss.request_stop();
        BOOST_TEST_EQ(q.size(), 1u);
        pump(q);
        BOOST_TEST(ec == make_error_code(error::canceled));
    }

    // A waiter claimed by its stop request belongs to the executor's
    // queue: discarding it after the waker is gone touches nothing.
    void testStopClaimedWaiterOutlivesWaker()
    {
        auto p = std::make_unique<async_waker>();
        std::queue<continuation> q;
        std::stop_source ss;
        int down = 0;
        auto waiter = [](async_waker& x, destroy_count_t) -> task<void>
        {
            std::ignore = co_await x.wait();
        };
        run_async(queuing_executor(q), ss.get_token())(
            waiter(*p, destroy_count_t(down)));
        pump(q);
        ss.request_stop();
        BOOST_TEST_EQ(q.size(), 1u);
        p.reset();
        capy::abandon(q.front());
        q.pop();
        pump(q);
        BOOST_TEST_EQ(down, 1);
    }

    // Same for a waiter claimed by wake().
    void testWakeClaimedWaiterOutlivesWaker()
    {
        auto p = std::make_unique<async_waker>();
        std::queue<continuation> q;
        int down = 0;
        auto waiter = [](async_waker& x, destroy_count_t) -> task<void>
        {
            std::ignore = co_await x.wait();
        };
        run_async(queuing_executor(q))(
            waiter(*p, destroy_count_t(down)));
        pump(q);
        p->wake();
        BOOST_TEST_EQ(q.size(), 1u);
        p.reset();
        capy::abandon(q.front());
        q.pop();
        pump(q);
        BOOST_TEST_EQ(down, 1);
    }

    void run()
    {
        testStopClaimedWaiterOutlivesWaker();
        testWakeClaimedWaiterOutlivesWaker();
        testDestroyWakerCancelsWaiter();
        testDestroyWaitingFrame();
        testStopAfterWakerDestroyed();
        testStopAfterWakeAndWakerDestroyed();
        testWokenWaiterDestroyedThenWakerDestroyed();
        testStopRacesWait();
        testWakerDestroyedWhileWokenWaiterResumes();
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
