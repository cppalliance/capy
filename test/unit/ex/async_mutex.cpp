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

#include <boost/capy/concept/io_awaitable.hpp>
#include <boost/capy/error.hpp>
#include <boost/capy/ex/executor_ref.hpp>
#include <boost/capy/ex/io_env.hpp>
#include <boost/capy/io_result.hpp>
#include <boost/capy/task.hpp>

#include <queue>
#include <stop_token>

#include "test_helpers.hpp"

namespace boost {
namespace capy {

static_assert(IoAwaitable<async_mutex::lock_awaiter>);
static_assert(IoAwaitable<async_mutex::lock_guard_awaiter>);

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
        io_env env;
        env.executor = executor_ref(ex);
        h.promise().set_environment(env);
        h.resume();

        BOOST_TEST(done);
        BOOST_TEST(!cm.is_locked());
        h.destroy();
    }

    void
    testContendedLock()
    {
        async_mutex cm;
        std::queue<coro> q;
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
        io_env env;
        env.executor = executor_ref(ex);
        h1.promise().set_environment(env);

        auto h2 = second.handle();
        second.release();
        h2.promise().set_environment(env);

        h1.resume();
        BOOST_TEST(first_done);
        BOOST_TEST(cm.is_locked());

        h2.resume();
        BOOST_TEST(!second_done);

        cm.unlock();
        BOOST_TEST(!second_done);

        BOOST_TEST(!q.empty());
        q.front().resume();
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
        std::queue<coro> q;
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
        io_env env1;
        env1.executor = executor_ref(ex);
        h1.promise().set_environment(env1);

        auto h2 = waiter.handle();
        waiter.release();
        io_env env2;
        env2.executor = executor_ref(ex);
        env2.stop_token = ss.get_token();
        h2.promise().set_environment(env2);

        h1.resume();
        BOOST_TEST(holder_done);

        h2.resume();
        BOOST_TEST(!waiter_done);

        ss.request_stop();

        BOOST_TEST(!q.empty());
        q.front().resume();
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
        std::queue<coro> q;
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
        io_env env1;
        env1.executor = executor_ref(ex);
        h1.promise().set_environment(env1);

        auto h2 = waiter.handle();
        waiter.release();
        io_env env2;
        env2.executor = executor_ref(ex);
        env2.stop_token = ss.get_token();
        h2.promise().set_environment(env2);

        h1.resume();
        BOOST_TEST(holder_done);

        h2.resume();
        BOOST_TEST(!waiter_done);

        ss.request_stop();

        BOOST_TEST(!q.empty());
        q.front().resume();
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
        std::queue<coro> q;
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
        io_env env;
        env.executor = executor_ref(ex);
        h1.promise().set_environment(env);

        auto h2 = waiter.handle();
        waiter.release();
        h2.promise().set_environment(env);

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
        std::queue<coro> q;
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
        io_env env_h;
        env_h.executor = executor_ref(ex);
        hh.promise().set_environment(env_h);

        auto h1 = w1.handle();
        w1.release();
        io_env env1;
        env1.executor = executor_ref(ex);
        env1.stop_token = ss1.get_token();
        h1.promise().set_environment(env1);

        auto h2 = w2.handle();
        w2.release();
        io_env env2;
        env2.executor = executor_ref(ex);
        env2.stop_token = ss2.get_token();
        h2.promise().set_environment(env2);

        auto h3 = w3.handle();
        w3.release();
        io_env env3;
        env3.executor = executor_ref(ex);
        h3.promise().set_environment(env3);

        hh.resume();
        BOOST_TEST(holder_done);

        h1.resume();
        h2.resume();
        h3.resume();

        ss1.request_stop();
        ss2.request_stop();

        while(!q.empty())
        {
            q.front().resume();
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
            q.front().resume();
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
    testPreSignaledToken()
    {
        async_mutex cm;
        std::queue<coro> q;
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
        io_env env1;
        env1.executor = executor_ref(ex);
        h1.promise().set_environment(env1);

        auto h2 = waiter.handle();
        waiter.release();
        io_env env2;
        env2.executor = executor_ref(ex);
        env2.stop_token = ss.get_token();
        h2.promise().set_environment(env2);

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
        io_env env;
        env.executor = executor_ref(ex);
        env.stop_token = ss.get_token();
        h.promise().set_environment(env);
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
        io_env env;
        env.executor = executor_ref(ex);
        h.promise().set_environment(env);
        h.resume();

        BOOST_TEST(done);
        h.destroy();
    }

    void
    testPreSignaledScopedLock()
    {
        async_mutex cm;
        std::queue<coro> q;
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
        io_env env1;
        env1.executor = executor_ref(ex);
        h1.promise().set_environment(env1);

        auto h2 = waiter.handle();
        waiter.release();
        io_env env2;
        env2.executor = executor_ref(ex);
        env2.stop_token = ss.get_token();
        h2.promise().set_environment(env2);

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
        std::queue<coro> q;
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
        io_env env;
        env.executor = executor_ref(ex);
        hh.promise().set_environment(env);

        auto h1 = w1.handle();
        w1.release();
        h1.promise().set_environment(env);

        auto h2 = w2.handle();
        w2.release();
        h2.promise().set_environment(env);

        auto h3 = w3.handle();
        w3.release();
        h3.promise().set_environment(env);

        hh.resume();
        h1.resume();
        h2.resume();
        h3.resume();

        cm.unlock();
        while(!q.empty())
        {
            q.front().resume();
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
        std::queue<coro> q;
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
        io_env env_h;
        env_h.executor = executor_ref(ex);
        hh.promise().set_environment(env_h);

        auto h1 = w1.handle();
        w1.release();
        io_env env1;
        env1.executor = executor_ref(ex);
        h1.promise().set_environment(env1);

        auto h2 = w2.handle();
        w2.release();
        io_env env2;
        env2.executor = executor_ref(ex);
        env2.stop_token = ss.get_token();
        h2.promise().set_environment(env2);

        auto h3 = w3.handle();
        w3.release();
        io_env env3;
        env3.executor = executor_ref(ex);
        h3.promise().set_environment(env3);

        hh.resume();
        h1.resume();
        h2.resume();
        h3.resume();

        ss.request_stop();
        while(!q.empty())
        {
            q.front().resume();
            q.pop();
        }
        BOOST_TEST(w2_done);
        BOOST_TEST(w2_ec == make_error_code(error::canceled));

        cm.unlock();
        while(!q.empty())
        {
            q.front().resume();
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
        std::queue<coro> q;
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
        io_env env;
        env.executor = executor_ref(ex);
        hh.promise().set_environment(env);

        auto h1 = w1.handle();
        w1.release();
        h1.promise().set_environment(env);

        auto h2 = w2.handle();
        w2.release();
        h2.promise().set_environment(env);

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
        io_env env;
        env.executor = executor_ref(ex);
        h.promise().set_environment(env);
        h.resume();

        BOOST_TEST(done);
        BOOST_TEST(!cm.is_locked());
        h.destroy();
    }

    void
    run()
    {
        testAcquireUnlocked();
        testContendedLock();
        testCancellation();
        testScopedLockCancellation();
        testDestroyWhileSuspended();
        testUnlockSkipsCanceled();
        testPreSignaledToken();
        testPreSignaledTokenUncontended();
        testScopedLockSuccess();
        testPreSignaledScopedLock();
        testFIFOOrder();
        testCancelMiddleWaiter();
        testDestroyMultipleSuspended();
        testLockGuardMove();
    }
};

TEST_SUITE(
    async_mutex_test,
    "boost.capy.ex.async_mutex");

} // capy
} // boost
