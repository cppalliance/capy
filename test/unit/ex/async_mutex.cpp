//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Test that header file is self-contained.
#include <boost/capy/ex/async_mutex.hpp>

#include <boost/capy/concept/context_awaitable.hpp>
#include <boost/capy/error.hpp>
#include <boost/capy/ex/abandon.hpp>
#include <boost/capy/ex/executor_ref.hpp>
#include <boost/capy/ex/await_context.hpp>
#include <boost/capy/results.hpp>
#include <boost/capy/ex/run_async.hpp>
#include <boost/capy/ex/strand.hpp>
#include <boost/capy/task.hpp>

#include <memory>
#include <queue>
#include <stop_token>
#include <tuple>

// GCC gives false positive -Wmaybe-uninitialized on structured bindings
// via the tuple protocol inside coroutine frames.
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif

#include "test_helpers.hpp"

namespace boost {
namespace capy {

static_assert(ContextAwaitable<async_mutex::lock_awaiter>);
static_assert(ContextAwaitable<async_mutex::lock_guard_awaiter>);

struct async_mutex_test
{
    void
    testAcquireUnlocked()
    {
        async_mutex cm;
        int dispatch_count = 0;
        test_executor ex(dispatch_count);

        bool done = false;
        auto t = [](async_mutex& cm, bool& done) -> task<void> {
            auto [ec] = co_await cm.lock();
            BOOST_TEST(!ec);
            BOOST_TEST(cm.is_locked());
            done = true;
            cm.unlock();
        }(cm, done);

        auto h = t.handle();
        t.release();
        await_context env;
        env.executor = executor_ref(ex);
        h.promise().set_environment(&env);
        h.resume();

        BOOST_TEST(done);
        BOOST_TEST(!cm.is_locked());
        h.destroy();
    }

    void
    testContendedLock()
    {
        async_mutex cm;
        std::queue<continuation> q;
        queuing_executor ex(q);

        bool first_done = false;
        bool second_done = false;

        auto first = [](async_mutex& cm, bool& done) -> task<void> {
            auto [ec] = co_await cm.lock();
            BOOST_TEST(!ec);
            done = true;
        }(cm, first_done);

        auto second = [](async_mutex& cm, bool& done) -> task<void> {
            auto [ec] = co_await cm.lock();
            BOOST_TEST(!ec);
            done = true;
            cm.unlock();
        }(cm, second_done);

        auto h1 = first.handle();
        first.release();
        await_context env;
        env.executor = executor_ref(ex);
        h1.promise().set_environment(&env);

        auto h2 = second.handle();
        second.release();
        h2.promise().set_environment(&env);

        h1.resume();
        BOOST_TEST(first_done);
        BOOST_TEST(cm.is_locked());

        h2.resume();
        BOOST_TEST(!second_done);

        cm.unlock();
        BOOST_TEST(!second_done);

        BOOST_TEST(!q.empty());
        q.front().h.resume();
        q.pop();
        BOOST_TEST(second_done);
        BOOST_TEST(!cm.is_locked());

        h1.destroy();
        h2.destroy();
    }

    void
    testCancellation()
    {
        async_mutex cm;
        std::queue<continuation> q;
        queuing_executor ex(q);
        std::stop_source ss;

        bool holder_done = false;
        bool waiter_done = false;
        std::error_code waiter_ec;

        auto holder = [](async_mutex& cm, bool& done) -> task<void> {
            auto [ec] = co_await cm.lock();
            BOOST_TEST(!ec);
            done = true;
        }(cm, holder_done);

        auto waiter = [](async_mutex& cm,
            std::error_code& out_ec, bool& done) -> task<void> {
            auto [ec] = co_await cm.lock();
            out_ec = ec;
            done = true;
        }(cm, waiter_ec, waiter_done);

        auto h1 = holder.handle();
        holder.release();
        await_context env1;
        env1.executor = executor_ref(ex);
        h1.promise().set_environment(&env1);

        auto h2 = waiter.handle();
        waiter.release();
        await_context env2;
        env2.executor = executor_ref(ex);
        env2.stop_token = ss.get_token();
        h2.promise().set_environment(&env2);

        h1.resume();
        BOOST_TEST(holder_done);

        h2.resume();
        BOOST_TEST(!waiter_done);

        ss.request_stop();

        BOOST_TEST(!q.empty());
        q.front().h.resume();
        q.pop();

        BOOST_TEST(waiter_done);
        BOOST_TEST(waiter_ec == make_error_code(error::canceled));
        BOOST_TEST(cm.is_locked());

        cm.unlock();
        BOOST_TEST(!cm.is_locked());

        h1.destroy();
        h2.destroy();
    }

    void
    testScopedLockCancellation()
    {
        async_mutex cm;
        std::queue<continuation> q;
        queuing_executor ex(q);
        std::stop_source ss;

        bool holder_done = false;
        bool waiter_done = false;
        std::error_code waiter_ec;

        auto holder = [](async_mutex& cm, bool& done) -> task<void> {
            auto [ec] = co_await cm.lock();
            BOOST_TEST(!ec);
            done = true;
        }(cm, holder_done);

        auto waiter = [](async_mutex& cm,
            std::error_code& out_ec, bool& done) -> task<void> {
            auto [ec, guard] = co_await cm.scoped_lock();
            out_ec = ec;
            done = true;
        }(cm, waiter_ec, waiter_done);

        auto h1 = holder.handle();
        holder.release();
        await_context env1;
        env1.executor = executor_ref(ex);
        h1.promise().set_environment(&env1);

        auto h2 = waiter.handle();
        waiter.release();
        await_context env2;
        env2.executor = executor_ref(ex);
        env2.stop_token = ss.get_token();
        h2.promise().set_environment(&env2);

        h1.resume();
        BOOST_TEST(holder_done);

        h2.resume();
        BOOST_TEST(!waiter_done);

        ss.request_stop();

        BOOST_TEST(!q.empty());
        q.front().h.resume();
        q.pop();

        BOOST_TEST(waiter_done);
        BOOST_TEST(waiter_ec == make_error_code(error::canceled));
        BOOST_TEST(cm.is_locked());

        cm.unlock();
        BOOST_TEST(!cm.is_locked());

        h1.destroy();
        h2.destroy();
    }

    void
    testDestroyWhileSuspended()
    {
        async_mutex cm;
        std::queue<continuation> q;
        queuing_executor ex(q);

        bool holder_done = false;

        auto holder = [](async_mutex& cm, bool& done) -> task<void> {
            auto [ec] = co_await cm.lock();
            BOOST_TEST(!ec);
            done = true;
        }(cm, holder_done);

        auto waiter = [](async_mutex& cm) -> task<void> {
            auto [ec] = co_await cm.lock();
            (void)ec;
        }(cm);

        auto h1 = holder.handle();
        holder.release();
        await_context env;
        env.executor = executor_ref(ex);
        h1.promise().set_environment(&env);

        auto h2 = waiter.handle();
        waiter.release();
        h2.promise().set_environment(&env);

        h1.resume();
        BOOST_TEST(holder_done);

        h2.resume();

        h2.destroy();

        cm.unlock();
        BOOST_TEST(!cm.is_locked());

        h1.destroy();
    }

    void
    testUnlockSkipsCanceled()
    {
        async_mutex cm;
        std::queue<continuation> q;
        queuing_executor ex(q);
        std::stop_source ss1;
        std::stop_source ss2;

        bool holder_done = false;
        bool w1_done = false, w2_done = false, w3_done = false;
        std::error_code w1_ec, w2_ec, w3_ec;

        auto holder = [](async_mutex& cm, bool& done) -> task<void> {
            auto [ec] = co_await cm.lock();
            done = true;
            (void)ec;
        }(cm, holder_done);

        auto w1 = [](async_mutex& cm,
            std::error_code& out_ec, bool& done) -> task<void> {
            auto [ec] = co_await cm.lock();
            out_ec = ec;
            done = true;
        }(cm, w1_ec, w1_done);

        auto w2 = [](async_mutex& cm,
            std::error_code& out_ec, bool& done) -> task<void> {
            auto [ec] = co_await cm.lock();
            out_ec = ec;
            done = true;
        }(cm, w2_ec, w2_done);

        auto w3 = [](async_mutex& cm,
            std::error_code& out_ec, bool& done) -> task<void> {
            auto [ec] = co_await cm.lock();
            out_ec = ec;
            done = true;
            cm.unlock();
        }(cm, w3_ec, w3_done);

        auto hh = holder.handle();
        holder.release();
        await_context env_h;
        env_h.executor = executor_ref(ex);
        hh.promise().set_environment(&env_h);

        auto h1 = w1.handle();
        w1.release();
        await_context env1;
        env1.executor = executor_ref(ex);
        env1.stop_token = ss1.get_token();
        h1.promise().set_environment(&env1);

        auto h2 = w2.handle();
        w2.release();
        await_context env2;
        env2.executor = executor_ref(ex);
        env2.stop_token = ss2.get_token();
        h2.promise().set_environment(&env2);

        auto h3 = w3.handle();
        w3.release();
        await_context env3;
        env3.executor = executor_ref(ex);
        h3.promise().set_environment(&env3);

        hh.resume();
        BOOST_TEST(holder_done);

        h1.resume();
        h2.resume();
        h3.resume();

        ss1.request_stop();
        ss2.request_stop();

        while(!q.empty())
        {
            q.front().h.resume();
            q.pop();
        }

        BOOST_TEST(w1_done);
        BOOST_TEST(w2_done);
        BOOST_TEST(w1_ec == make_error_code(error::canceled));
        BOOST_TEST(w2_ec == make_error_code(error::canceled));
        BOOST_TEST(!w3_done);

        cm.unlock();

        while(!q.empty())
        {
            q.front().h.resume();
            q.pop();
        }

        BOOST_TEST(w3_done);
        BOOST_TEST(!w3_ec);
        BOOST_TEST(!cm.is_locked());

        hh.destroy();
        h1.destroy();
        h2.destroy();
        h3.destroy();
    }

    void
    testUnlockSkipsClaimedWaiterStillQueued()
    {
        // unlock() runs while a stop-claimed waiter is still on the
        // queue (its posted resume has not run yet). unlock() pops it,
        // sees it already claimed, and loops to the next waiter. The
        // claimed waiter's later await_resume() then also unlinks it;
        // remove() must be idempotent so this second unlink is a no-op.
        // The queuing executor makes the interleaving deterministic.
        async_mutex cm;
        std::queue<continuation> q;
        queuing_executor ex(q);
        std::stop_source ss1;

        bool holder_done = false;
        bool w1_done = false, w2_done = false;
        std::error_code w1_ec, w2_ec;

        auto holder = [](async_mutex& cm, bool& done) -> task<void> {
            auto [ec] = co_await cm.lock();
            done = true;
            (void)ec;
        }(cm, holder_done);

        auto w1 = [](async_mutex& cm,
            std::error_code& out_ec, bool& done) -> task<void> {
            auto [ec] = co_await cm.lock();
            out_ec = ec;
            done = true;
        }(cm, w1_ec, w1_done);

        auto w2 = [](async_mutex& cm,
            std::error_code& out_ec, bool& done) -> task<void> {
            auto [ec] = co_await cm.lock();
            out_ec = ec;
            done = true;
        }(cm, w2_ec, w2_done);

        auto hh = holder.handle();
        holder.release();
        await_context env_h;
        env_h.executor = executor_ref(ex);
        hh.promise().set_environment(&env_h);

        auto h1 = w1.handle();
        w1.release();
        await_context env1;
        env1.executor = executor_ref(ex);
        env1.stop_token = ss1.get_token();
        h1.promise().set_environment(&env1);

        auto h2 = w2.handle();
        w2.release();
        await_context env2;
        env2.executor = executor_ref(ex);
        h2.promise().set_environment(&env2);

        hh.resume();
        BOOST_TEST(holder_done);

        h1.resume();  // w1 queues (front)
        h2.resume();  // w2 queues

        // Cancel w1: it is claimed and its resume posted, but it is
        // still in the wait queue.
        ss1.request_stop();

        // unlock() pops the claimed w1, skips it, and hands the lock to
        // w2.
        cm.unlock();

        while(!q.empty())
        {
            q.front().h.resume();
            q.pop();
        }

        BOOST_TEST(w1_done);
        BOOST_TEST(w1_ec == make_error_code(error::canceled));
        BOOST_TEST(w2_done);
        BOOST_TEST(!w2_ec);  // w2 acquired the lock
        BOOST_TEST(cm.is_locked());

        cm.unlock();

        hh.destroy();
        h1.destroy();
        h2.destroy();
    }

    void
    testPreSignaledToken()
    {
        async_mutex cm;
        std::queue<continuation> q;
        queuing_executor ex(q);
        std::stop_source ss;
        ss.request_stop();

        bool holder_done = false;
        bool waiter_done = false;
        std::error_code waiter_ec;

        auto holder = [](async_mutex& cm, bool& done) -> task<void> {
            auto [ec] = co_await cm.lock();
            BOOST_TEST(!ec);
            done = true;
        }(cm, holder_done);

        auto waiter = [](async_mutex& cm,
            std::error_code& out_ec, bool& done) -> task<void> {
            auto [ec] = co_await cm.lock();
            out_ec = ec;
            done = true;
        }(cm, waiter_ec, waiter_done);

        auto h1 = holder.handle();
        holder.release();
        await_context env1;
        env1.executor = executor_ref(ex);
        h1.promise().set_environment(&env1);

        auto h2 = waiter.handle();
        waiter.release();
        await_context env2;
        env2.executor = executor_ref(ex);
        env2.stop_token = ss.get_token();
        h2.promise().set_environment(&env2);

        h1.resume();
        BOOST_TEST(holder_done);

        h2.resume();
        BOOST_TEST(waiter_done);
        BOOST_TEST(q.empty());
        BOOST_TEST(waiter_ec == make_error_code(error::canceled));

        BOOST_TEST(cm.is_locked());
        cm.unlock();
        BOOST_TEST(!cm.is_locked());

        h1.destroy();
        h2.destroy();
    }

    void
    testPreSignaledTokenUncontended()
    {
        async_mutex cm;
        int dispatch_count = 0;
        test_executor ex(dispatch_count);
        std::stop_source ss;
        ss.request_stop();

        bool done = false;
        auto t = [](async_mutex& cm, bool& done) -> task<void> {
            auto [ec] = co_await cm.lock();
            BOOST_TEST(!ec);
            done = true;
            cm.unlock();
        }(cm, done);

        auto h = t.handle();
        t.release();
        await_context env;
        env.executor = executor_ref(ex);
        env.stop_token = ss.get_token();
        h.promise().set_environment(&env);
        h.resume();

        BOOST_TEST(done);
        BOOST_TEST(!cm.is_locked());
        h.destroy();
    }

    void
    testScopedLockSuccess()
    {
        async_mutex cm;
        int dispatch_count = 0;
        test_executor ex(dispatch_count);

        bool done = false;
        auto t = [](async_mutex& cm, bool& done) -> task<void> {
            {
                auto [ec, guard] = co_await cm.scoped_lock();
                BOOST_TEST(!ec);
                BOOST_TEST(cm.is_locked());
            }
            BOOST_TEST(!cm.is_locked());
            done = true;
        }(cm, done);

        auto h = t.handle();
        t.release();
        await_context env;
        env.executor = executor_ref(ex);
        h.promise().set_environment(&env);
        h.resume();

        BOOST_TEST(done);
        h.destroy();
    }

    void
    testPreSignaledScopedLock()
    {
        async_mutex cm;
        std::queue<continuation> q;
        queuing_executor ex(q);
        std::stop_source ss;
        ss.request_stop();

        bool holder_done = false;
        bool waiter_done = false;
        std::error_code waiter_ec;

        auto holder = [](async_mutex& cm, bool& done) -> task<void> {
            auto [ec] = co_await cm.lock();
            done = true;
            (void)ec;
        }(cm, holder_done);

        auto waiter = [](async_mutex& cm,
            std::error_code& out_ec, bool& done) -> task<void> {
            auto [ec, guard] = co_await cm.scoped_lock();
            out_ec = ec;
            done = true;
        }(cm, waiter_ec, waiter_done);

        auto h1 = holder.handle();
        holder.release();
        await_context env1;
        env1.executor = executor_ref(ex);
        h1.promise().set_environment(&env1);

        auto h2 = waiter.handle();
        waiter.release();
        await_context env2;
        env2.executor = executor_ref(ex);
        env2.stop_token = ss.get_token();
        h2.promise().set_environment(&env2);

        h1.resume();
        BOOST_TEST(holder_done);

        h2.resume();
        BOOST_TEST(waiter_done);
        BOOST_TEST(q.empty());
        BOOST_TEST(waiter_ec == make_error_code(error::canceled));

        cm.unlock();
        BOOST_TEST(!cm.is_locked());

        h1.destroy();
        h2.destroy();
    }

    void
    testFIFOOrder()
    {
        async_mutex cm;
        std::queue<continuation> q;
        queuing_executor ex(q);

        int order = 0;
        int w1_order = 0, w2_order = 0, w3_order = 0;

        auto holder = [](async_mutex& cm) -> task<void> {
            auto [ec] = co_await cm.lock();
            (void)ec;
        }(cm);

        auto w1 = [](async_mutex& cm, int& ord, int& out) -> task<void> {
            auto [ec] = co_await cm.lock();
            (void)ec;
            out = ++ord;
            cm.unlock();
        }(cm, order, w1_order);

        auto w2 = [](async_mutex& cm, int& ord, int& out) -> task<void> {
            auto [ec] = co_await cm.lock();
            (void)ec;
            out = ++ord;
            cm.unlock();
        }(cm, order, w2_order);

        auto w3 = [](async_mutex& cm, int& ord, int& out) -> task<void> {
            auto [ec] = co_await cm.lock();
            (void)ec;
            out = ++ord;
            cm.unlock();
        }(cm, order, w3_order);

        auto hh = holder.handle();
        holder.release();
        await_context env;
        env.executor = executor_ref(ex);
        hh.promise().set_environment(&env);

        auto h1 = w1.handle();
        w1.release();
        h1.promise().set_environment(&env);

        auto h2 = w2.handle();
        w2.release();
        h2.promise().set_environment(&env);

        auto h3 = w3.handle();
        w3.release();
        h3.promise().set_environment(&env);

        hh.resume();
        h1.resume();
        h2.resume();
        h3.resume();

        cm.unlock();
        while(!q.empty())
        {
            q.front().h.resume();
            q.pop();
        }

        BOOST_TEST_EQ(w1_order, 1);
        BOOST_TEST_EQ(w2_order, 2);
        BOOST_TEST_EQ(w3_order, 3);
        BOOST_TEST(!cm.is_locked());

        hh.destroy();
        h1.destroy();
        h2.destroy();
        h3.destroy();
    }

    void
    testCancelMiddleWaiter()
    {
        async_mutex cm;
        std::queue<continuation> q;
        queuing_executor ex(q);
        std::stop_source ss;

        int order = 0;
        int w1_order = 0, w3_order = 0;
        std::error_code w2_ec;
        bool w2_done = false;

        auto holder = [](async_mutex& cm) -> task<void> {
            auto [ec] = co_await cm.lock();
            (void)ec;
        }(cm);

        auto w1 = [](async_mutex& cm, int& ord, int& out) -> task<void> {
            auto [ec] = co_await cm.lock();
            (void)ec;
            out = ++ord;
            cm.unlock();
        }(cm, order, w1_order);

        auto w2 = [](async_mutex& cm,
            std::error_code& out_ec, bool& done) -> task<void> {
            auto [ec] = co_await cm.lock();
            out_ec = ec;
            done = true;
        }(cm, w2_ec, w2_done);

        auto w3 = [](async_mutex& cm, int& ord, int& out) -> task<void> {
            auto [ec] = co_await cm.lock();
            (void)ec;
            out = ++ord;
            cm.unlock();
        }(cm, order, w3_order);

        auto hh = holder.handle();
        holder.release();
        await_context env_h;
        env_h.executor = executor_ref(ex);
        hh.promise().set_environment(&env_h);

        auto h1 = w1.handle();
        w1.release();
        await_context env1;
        env1.executor = executor_ref(ex);
        h1.promise().set_environment(&env1);

        auto h2 = w2.handle();
        w2.release();
        await_context env2;
        env2.executor = executor_ref(ex);
        env2.stop_token = ss.get_token();
        h2.promise().set_environment(&env2);

        auto h3 = w3.handle();
        w3.release();
        await_context env3;
        env3.executor = executor_ref(ex);
        h3.promise().set_environment(&env3);

        hh.resume();
        h1.resume();
        h2.resume();
        h3.resume();

        ss.request_stop();
        while(!q.empty())
        {
            q.front().h.resume();
            q.pop();
        }
        BOOST_TEST(w2_done);
        BOOST_TEST(w2_ec == make_error_code(error::canceled));

        cm.unlock();
        while(!q.empty())
        {
            q.front().h.resume();
            q.pop();
        }

        BOOST_TEST_EQ(w1_order, 1);
        BOOST_TEST_EQ(w3_order, 2);
        BOOST_TEST(!cm.is_locked());

        hh.destroy();
        h1.destroy();
        h2.destroy();
        h3.destroy();
    }

    void
    testDestroyMultipleSuspended()
    {
        async_mutex cm;
        std::queue<continuation> q;
        queuing_executor ex(q);

        auto holder = [](async_mutex& cm) -> task<void> {
            auto [ec] = co_await cm.lock();
            (void)ec;
        }(cm);

        auto w1 = [](async_mutex& cm) -> task<void> {
            auto [ec] = co_await cm.lock();
            (void)ec;
        }(cm);

        auto w2 = [](async_mutex& cm) -> task<void> {
            auto [ec] = co_await cm.lock();
            (void)ec;
        }(cm);

        auto hh = holder.handle();
        holder.release();
        await_context env;
        env.executor = executor_ref(ex);
        hh.promise().set_environment(&env);

        auto h1 = w1.handle();
        w1.release();
        h1.promise().set_environment(&env);

        auto h2 = w2.handle();
        w2.release();
        h2.promise().set_environment(&env);

        hh.resume();
        h1.resume();
        h2.resume();

        h1.destroy();
        h2.destroy();

        cm.unlock();
        BOOST_TEST(!cm.is_locked());

        hh.destroy();
    }

    void
    testLockGuardMove()
    {
        async_mutex cm;
        int dispatch_count = 0;
        test_executor ex(dispatch_count);

        bool done = false;
        auto t = [](async_mutex& cm, bool& done) -> task<void> {
            auto [ec, guard] = co_await cm.scoped_lock();
            BOOST_TEST(!ec);
            BOOST_TEST(cm.is_locked());

            async_mutex::lock_guard moved(std::move(guard));

            BOOST_TEST(cm.is_locked());
            done = true;
        }(cm, done);

        auto h = t.handle();
        t.release();
        await_context env;
        env.executor = executor_ref(ex);
        h.promise().set_environment(&env);
        h.resume();

        BOOST_TEST(done);
        BOOST_TEST(!cm.is_locked());
        h.destroy();
    }

    void
    testUnlockResumesWaiterInline()
    {
        // Regression: unlock() must use post() so the waiter
        // is resumed even with an inline executor.
        async_mutex cm;
        int dispatch_count = 0;
        test_executor ex(dispatch_count);

        bool first_done = false;
        bool second_done = false;

        auto first = [](async_mutex& cm, bool& done) -> task<void> {
            auto [ec] = co_await cm.lock();
            BOOST_TEST(!ec);
            done = true;
        }(cm, first_done);

        auto second = [](async_mutex& cm, bool& done) -> task<void> {
            auto [ec] = co_await cm.lock();
            BOOST_TEST(!ec);
            done = true;
            cm.unlock();
        }(cm, second_done);

        auto h1 = first.handle();
        first.release();
        await_context env;
        env.executor = executor_ref(ex);
        h1.promise().set_environment(&env);

        auto h2 = second.handle();
        second.release();
        h2.promise().set_environment(&env);

        h1.resume();
        BOOST_TEST(first_done);
        BOOST_TEST(cm.is_locked());

        h2.resume();
        BOOST_TEST(!second_done);

        cm.unlock();

        BOOST_TEST(second_done);
        BOOST_TEST(!cm.is_locked());

        h1.destroy();
        h2.destroy();
    }

    void
    testCancellationWithInlineExecutor()
    {
        // Regression: cancel_fn must use post() so the waiter
        // is resumed even with an inline executor.
        async_mutex cm;
        int dispatch_count = 0;
        test_executor ex(dispatch_count);
        std::stop_source ss;

        bool holder_done = false;
        bool waiter_done = false;
        std::error_code waiter_ec;

        auto holder = [](async_mutex& cm, bool& done) -> task<void> {
            auto [ec] = co_await cm.lock();
            BOOST_TEST(!ec);
            done = true;
        }(cm, holder_done);

        auto waiter = [](async_mutex& cm,
            std::error_code& out_ec, bool& done) -> task<void> {
            auto [ec] = co_await cm.lock();
            out_ec = ec;
            done = true;
        }(cm, waiter_ec, waiter_done);

        auto h1 = holder.handle();
        holder.release();
        await_context env1;
        env1.executor = executor_ref(ex);
        h1.promise().set_environment(&env1);

        auto h2 = waiter.handle();
        waiter.release();
        await_context env2;
        env2.executor = executor_ref(ex);
        env2.stop_token = ss.get_token();
        h2.promise().set_environment(&env2);

        h1.resume();
        BOOST_TEST(holder_done);

        h2.resume();
        BOOST_TEST(!waiter_done);

        ss.request_stop();

        BOOST_TEST(waiter_done);
        BOOST_TEST(waiter_ec == make_error_code(error::canceled));
        BOOST_TEST(cm.is_locked());

        cm.unlock();
        BOOST_TEST(!cm.is_locked());

        h1.destroy();
        h2.destroy();
    }

    // A destroyed mutex can no longer grant the lock, so its waiters
    // resume with an error instead of disappearing.
    void testDestroyMutexCancelsWaiters()
    {
        int down = 0;
        std::error_code ec;
        auto m = std::make_unique<async_mutex>();
        BOOST_TEST(m->lock().await_ready()); // held by the test
        auto waiter = [](async_mutex& mx, std::error_code& out,
            destroy_count_t) -> task<void>
        {
            auto [e] = co_await mx.lock();
            out = e;
        };
        std::queue<continuation> q;
        run_async(queuing_executor(q))(waiter(*m, ec, destroy_count_t(down)));
        pump(q);
        m.reset();
        BOOST_TEST_EQ(down, 0);
        BOOST_TEST_EQ(q.size(), 1u);
        pump(q);
        BOOST_TEST(ec == make_error_code(error::canceled));
        BOOST_TEST_EQ(down, 1);
    }

    // A frame owning the mutex is torn down while it holds the lock:
    // its guard hands the lock to a waiter, then the mutex dies, then
    // the context's teardown destroys that waiter before it resumes.
    void testOwnerTornDownWhileHoldingLock()
    {
        int down = 0;
        parked slot;
        std::queue<continuation> q;
        queuing_executor ex(q);
        int resumed = 0;
        auto waiter = [](async_mutex& mx, int& resumed, destroy_count_t)
            -> task<void>
        {
            std::ignore = co_await mx.lock();
            ++resumed;
        };
        auto leaf = [](parked* s) -> task<void>
        {
            co_await park_awaitable{s};
        };
        auto owner = [&]() -> task<void>
        {
            async_mutex m;
            auto [ec, guard] = co_await m.scoped_lock();
            run_async(ex)(waiter(m, resumed, destroy_count_t(down)));
            run_async(ex)(waiter(m, resumed, destroy_count_t(down)));
            co_await leaf(&slot);
        };
        run_async(ex, std::allocator<std::byte>{})(owner());
        pump(q);
        BOOST_TEST(slot);
        capy::abandon(slot);
        while(!q.empty())
        {
            capy::abandon(q.front());
            q.pop();
        }
        BOOST_TEST_EQ(down, 2);
        BOOST_TEST_EQ(resumed, 0);
    }

    // The granted waiter's frame owns the mutex, so the post that
    // destroys it destroys the mutex while unlock() is still running.
    void testUnlockWhenPostDestroysOwner()
    {
        std::queue<continuation> q;
        auto ctx = std::make_unique<closable_test_context>();
        queuing_executor qe(q);
        qe.ctx_ = ctx.get();
        strand<queuing_executor> s{qe};
        async_mutex* mp = nullptr;
        int down = 0;
        auto waiter = [](async_mutex*& out, destroy_count_t) -> task<void>
        {
            async_mutex m;
            out = &m;
            std::ignore = co_await m.lock();
            std::ignore = co_await m.lock();
        };
        run_async(s, std::allocator<std::byte>{})(
            waiter(mp, destroy_count_t(down)));
        pump(q);
        BOOST_TEST(mp);
        ctx->shutdown();
        mp->unlock();
        BOOST_TEST_EQ(down, 1);
    }

    // Each granted waiter destroyed on the spot by its closed strand
    // hands the lock on; that must not nest once per waiter.
    void testHandoffChainDoesNotRecurse()
    {
        constexpr int count = 100000;
        std::queue<continuation> q;
        auto ctx = std::make_unique<closable_test_context>();
        queuing_executor qe(q);
        qe.ctx_ = ctx.get();
        strand<queuing_executor> s{qe};
        async_mutex m;
        BOOST_TEST(m.lock().await_ready());
        int down = 0;
        auto waiter = [](async_mutex& mx, destroy_count_t) -> task<void>
        {
            std::ignore = co_await mx.lock();
            mx.unlock();
        };
        for(int i = 0; i < count; ++i)
            run_async(s)(waiter(m, destroy_count_t(down)));
        pump(q);
        ctx->shutdown();
        m.unlock();
        BOOST_TEST_EQ(down, count);
        BOOST_TEST(!m.is_locked());
    }

    // A waiter the destroyed mutex posted is still destroyed when its
    // execution context is torn down instead of run.
    void testCanceledWaiterDestroyedAtTeardown()
    {
        int down = 0;
        auto m = std::make_unique<async_mutex>();
        BOOST_TEST(m->lock().await_ready());
        auto waiter = [](async_mutex& mx, destroy_count_t) -> task<void>
        {
            std::ignore = co_await mx.lock();
        };
        std::queue<continuation> q;
        run_async(queuing_executor(q))(waiter(*m, destroy_count_t(down)));
        pump(q);
        m.reset();
        BOOST_TEST_EQ(q.size(), 1u);
        capy::abandon(q.front());
        q.pop();
        pump(q);
        BOOST_TEST_EQ(down, 1);
    }

    // The lock handed to a waiter that is destroyed before it resumes
    // passes on instead of staying held.
    void testHandoffToDestroyedWaiterUnlocks()
    {
        async_mutex m;
        BOOST_TEST(m.lock().await_ready());
        std::queue<continuation> q;
        auto waiter = [](async_mutex& mx) -> task<void>
        {
            std::ignore = co_await mx.lock();
            mx.unlock();
        };
        run_async(queuing_executor(q))(waiter(m));
        pump(q);                 // waiter queued on the mutex
        m.unlock();              // hands the lock over, posts the waiter
        BOOST_TEST_EQ(q.size(), 1u);
        capy::abandon(q.front()); // torn down before it resumes
        q.pop();
        pump(q);
        BOOST_TEST(!m.is_locked());
    }

    // A waiter claimed by its stop request belongs to the executor's
    // queue: discarding it after the mutex is gone touches nothing.
    void testStopClaimedWaiterOutlivesMutex()
    {
        auto p = std::make_unique<async_mutex>();
        std::queue<continuation> q;
        std::stop_source ss;
        // Held by the test, not a coroutine: nothing unlocks it later,
        // and unlocking first would pop the claimed waiter before the
        // destructor could see it.
        BOOST_TEST(p->lock().await_ready());
        int down = 0;
        auto waiter = [](async_mutex& x, destroy_count_t) -> task<void>
        {
            std::ignore = co_await x.lock();
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

    void
    run()
    {
        testStopClaimedWaiterOutlivesMutex();
        testDestroyMutexCancelsWaiters();
        testCanceledWaiterDestroyedAtTeardown();
        testHandoffChainDoesNotRecurse();
        testUnlockWhenPostDestroysOwner();
        testOwnerTornDownWhileHoldingLock();
        testHandoffToDestroyedWaiterUnlocks();
        testAcquireUnlocked();
        testContendedLock();
        testCancellation();
        testScopedLockCancellation();
        testDestroyWhileSuspended();
        testUnlockSkipsCanceled();
        testUnlockSkipsClaimedWaiterStillQueued();
        testPreSignaledToken();
        testPreSignaledTokenUncontended();
        testScopedLockSuccess();
        testPreSignaledScopedLock();
        testFIFOOrder();
        testCancelMiddleWaiter();
        testDestroyMultipleSuspended();
        testLockGuardMove();
        testUnlockResumesWaiterInline();
        testCancellationWithInlineExecutor();
    }
};

TEST_SUITE(
    async_mutex_test,
    "boost.capy.ex.async_mutex");

} // capy
} // boost
