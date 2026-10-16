//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Test that header file is self-contained.
#include <boost/capy/ex/async_event.hpp>

#include <boost/capy/concept/context_awaitable.hpp>
#include <boost/capy/error.hpp>
#include <boost/capy/ex/abandon.hpp>
#include <boost/capy/ex/strand.hpp>
#include <boost/capy/ex/executor_ref.hpp>
#include <boost/capy/ex/await_context.hpp>
#include <boost/capy/ex/run_async.hpp>
#include <boost/capy/ex/thread_pool.hpp>
#include <boost/capy/results.hpp>
#include <boost/capy/task.hpp>
#include <boost/capy/when_all.hpp>

#include <cstddef>
#include <memory>
#include <queue>
#include <semaphore>
#include <stop_token>
#include <tuple>
#include <vector>

#include "test_helpers.hpp"

namespace boost {
namespace capy {

static_assert(ContextAwaitable<async_event::wait_awaiter>);

struct async_event_test
{
    void
    testInitialState()
    {
        async_event event;
        BOOST_TEST(!event.is_set());
    }

    void
    testSetAndClear()
    {
        async_event event;
        BOOST_TEST(!event.is_set());
        event.set();
        BOOST_TEST(event.is_set());
        event.clear();
        BOOST_TEST(!event.is_set());
        event.set();
        BOOST_TEST(event.is_set());
    }

    void
    testWaitOnSetEvent()
    {
        // await_ready returns true when event is set
        async_event event;
        event.set();
        auto awaiter = event.wait();
        BOOST_TEST(awaiter.await_ready());

        // await_ready returns false when event is not set
        async_event event2;
        auto awaiter2 = event2.wait();
        BOOST_TEST(!awaiter2.await_ready());
    }

    void
    testSingleWaiter()
    {
        async_event event;
        std::queue<continuation> q;
        queuing_executor ex(q);

        bool resumed = false;
        auto t = [](async_event& ev, bool& r) -> task<void> {
            auto [ec] = co_await ev.wait();
            BOOST_TEST(!ec);
            r = true;
        }(event, resumed);

        auto h = t.handle();
        t.release();
        await_context env;
        env.executor = executor_ref(ex);
        h.promise().set_environment(&env);

        h.resume();
        BOOST_TEST(!resumed);

        event.set();

        while(!q.empty())
        {
            q.front().h.resume();
            q.pop();
        }

        BOOST_TEST(resumed);
        BOOST_TEST(h.done());
        h.destroy();
    }

    void
    testMultipleWaiters()
    {
        async_event event;
        std::queue<continuation> q;
        queuing_executor ex(q);

        int count = 0;

        auto make_waiter = [](async_event& ev,
            int& c) -> task<void> {
            auto [ec] = co_await ev.wait();
            BOOST_TEST(!ec);
            ++c;
        };

        auto t1 = make_waiter(event, count);
        auto t2 = make_waiter(event, count);
        auto t3 = make_waiter(event, count);

        auto h1 = t1.handle();
        auto h2 = t2.handle();
        auto h3 = t3.handle();
        t1.release();
        t2.release();
        t3.release();

        await_context env;
        env.executor = executor_ref(ex);
        h1.promise().set_environment(&env);
        h2.promise().set_environment(&env);
        h3.promise().set_environment(&env);

        h1.resume();
        h2.resume();
        h3.resume();
        BOOST_TEST_EQ(count, 0);

        event.set();

        while(!q.empty())
        {
            q.front().h.resume();
            q.pop();
        }

        BOOST_TEST_EQ(count, 3);

        h1.destroy();
        h2.destroy();
        h3.destroy();
    }

    void
    testWaitAfterSet()
    {
        // Wait on an already-set event completes immediately
        async_event event;
        int dispatch_count = 0;
        test_executor ex(dispatch_count);
        event.set();

        bool done = false;
        auto t = [](async_event& ev, bool& d) -> task<void> {
            auto [ec] = co_await ev.wait();
            BOOST_TEST(!ec);
            d = true;
        }(event, done);

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
    testSetClearSetSequence()
    {
        async_event event;
        std::queue<continuation> q;
        queuing_executor ex(q);

        int count = 0;

        // First waiter
        auto t1 = [](async_event& ev, int& c) -> task<void> {
            auto [ec] = co_await ev.wait();
            BOOST_TEST(!ec);
            ++c;
        }(event, count);

        auto h1 = t1.handle();
        t1.release();
        await_context env;
        env.executor = executor_ref(ex);
        h1.promise().set_environment(&env);
        h1.resume();
        BOOST_TEST_EQ(count, 0);

        event.set();
        while(!q.empty())
        {
            q.front().h.resume();
            q.pop();
        }
        BOOST_TEST_EQ(count, 1);
        h1.destroy();

        // Clear and add second waiter
        event.clear();

        auto t2 = [](async_event& ev, int& c) -> task<void> {
            auto [ec] = co_await ev.wait();
            BOOST_TEST(!ec);
            ++c;
        }(event, count);

        auto h2 = t2.handle();
        t2.release();
        h2.promise().set_environment(&env);
        h2.resume();
        BOOST_TEST_EQ(count, 1);

        event.set();
        while(!q.empty())
        {
            q.front().h.resume();
            q.pop();
        }
        BOOST_TEST_EQ(count, 2);
        h2.destroy();
    }

    void
    testResumeOrder()
    {
        async_event event;
        std::queue<continuation> q;
        queuing_executor ex(q);

        std::vector<int> order;

        auto make_waiter = [](async_event& ev,
            std::vector<int>& ord, int id) -> task<void> {
            auto [ec] = co_await ev.wait();
            (void)ec;
            ord.push_back(id);
        };

        auto t1 = make_waiter(event, order, 1);
        auto t2 = make_waiter(event, order, 2);
        auto t3 = make_waiter(event, order, 3);

        auto h1 = t1.handle();
        auto h2 = t2.handle();
        auto h3 = t3.handle();
        t1.release();
        t2.release();
        t3.release();

        await_context env;
        env.executor = executor_ref(ex);
        h1.promise().set_environment(&env);
        h2.promise().set_environment(&env);
        h3.promise().set_environment(&env);

        h1.resume();
        h2.resume();
        h3.resume();

        event.set();
        while(!q.empty())
        {
            q.front().h.resume();
            q.pop();
        }

        BOOST_TEST_EQ(order.size(), 3u);
        BOOST_TEST_EQ(order[0], 1);
        BOOST_TEST_EQ(order[1], 2);
        BOOST_TEST_EQ(order[2], 3);

        h1.destroy();
        h2.destroy();
        h3.destroy();
    }

    void
    testNoWaitersSet()
    {
        async_event event;
        event.set();
        BOOST_TEST(event.is_set());
    }

    void
    testCancellation()
    {
        // Stop token cancels a waiting coroutine
        async_event event;
        std::queue<continuation> q;
        queuing_executor ex(q);
        std::stop_source ss;

        bool done = false;
        std::error_code out_ec;

        auto t = [](async_event& ev,
            std::error_code& oec, bool& d) -> task<void> {
            auto [ec] = co_await ev.wait();
            oec = ec;
            d = true;
        }(event, out_ec, done);

        auto h = t.handle();
        t.release();
        await_context env;
        env.executor = executor_ref(ex);
        env.stop_token = ss.get_token();
        h.promise().set_environment(&env);

        h.resume();
        BOOST_TEST(!done);

        ss.request_stop();

        while(!q.empty())
        {
            q.front().h.resume();
            q.pop();
        }

        BOOST_TEST(done);
        BOOST_TEST(out_ec == make_error_code(error::canceled));
        BOOST_TEST(!event.is_set());

        h.destroy();
    }

    void
    testCancelOneOfMultiple()
    {
        // Cancel one waiter, set() wakes the rest
        async_event event;
        std::queue<continuation> q;
        queuing_executor ex(q);
        std::stop_source ss;

        bool w1_done = false, w2_done = false;
        std::error_code w1_ec, w2_ec;

        auto w1 = [](async_event& ev,
            std::error_code& oec, bool& d) -> task<void> {
            auto [ec] = co_await ev.wait();
            oec = ec;
            d = true;
        }(event, w1_ec, w1_done);

        auto w2 = [](async_event& ev,
            std::error_code& oec, bool& d) -> task<void> {
            auto [ec] = co_await ev.wait();
            oec = ec;
            d = true;
        }(event, w2_ec, w2_done);

        auto h1 = w1.handle();
        w1.release();
        await_context env1;
        env1.executor = executor_ref(ex);
        env1.stop_token = ss.get_token();
        h1.promise().set_environment(&env1);

        auto h2 = w2.handle();
        w2.release();
        await_context env2;
        env2.executor = executor_ref(ex);
        h2.promise().set_environment(&env2);

        h1.resume();
        h2.resume();
        BOOST_TEST(!w1_done);
        BOOST_TEST(!w2_done);

        // Cancel w1
        ss.request_stop();
        while(!q.empty())
        {
            q.front().h.resume();
            q.pop();
        }
        BOOST_TEST(w1_done);
        BOOST_TEST(w1_ec == make_error_code(error::canceled));
        BOOST_TEST(!w2_done);

        // Set event -> w2 wakes
        event.set();
        while(!q.empty())
        {
            q.front().h.resume();
            q.pop();
        }
        BOOST_TEST(w2_done);
        BOOST_TEST(!w2_ec);

        h1.destroy();
        h2.destroy();
    }

    void
    testPreSignaledToken()
    {
        // Pre-signaled token on contended event
        async_event event;
        std::queue<continuation> q;
        queuing_executor ex(q);
        std::stop_source ss;
        ss.request_stop();

        bool done = false;
        std::error_code out_ec;

        auto t = [](async_event& ev,
            std::error_code& oec, bool& d) -> task<void> {
            auto [ec] = co_await ev.wait();
            oec = ec;
            d = true;
        }(event, out_ec, done);

        auto h = t.handle();
        t.release();
        await_context env;
        env.executor = executor_ref(ex);
        env.stop_token = ss.get_token();
        h.promise().set_environment(&env);

        h.resume();
        BOOST_TEST(done);
        BOOST_TEST(q.empty());
        BOOST_TEST(out_ec == make_error_code(error::canceled));

        h.destroy();
    }

    void
    testPreSignaledTokenOnSetEvent()
    {
        // Pre-signaled token on already-set event:
        // fast path acquires, no cancellation
        async_event event;
        int dispatch_count = 0;
        test_executor ex(dispatch_count);
        std::stop_source ss;
        ss.request_stop();
        event.set();

        bool done = false;
        auto t = [](async_event& ev, bool& d) -> task<void> {
            auto [ec] = co_await ev.wait();
            BOOST_TEST(!ec);
            d = true;
        }(event, done);

        auto h = t.handle();
        t.release();
        await_context env;
        env.executor = executor_ref(ex);
        env.stop_token = ss.get_token();
        h.promise().set_environment(&env);
        h.resume();

        BOOST_TEST(done);
        h.destroy();
    }

    void
    testDestroyWhileSuspended()
    {
        // Destroying a coroutine while suspended properly
        // unlinks the awaiter
        async_event event;
        std::queue<continuation> q;
        queuing_executor ex(q);

        auto t = [](async_event& ev) -> task<void> {
            auto [ec] = co_await ev.wait();
            (void)ec;
        }(event);

        auto h = t.handle();
        t.release();
        await_context env;
        env.executor = executor_ref(ex);
        h.promise().set_environment(&env);
        h.resume();

        // Destroy without resuming (shutdown)
        h.destroy();

        // set() with empty list is safe
        event.set();
        BOOST_TEST(event.is_set());
    }

    void
    testSetThenCancelNoDouble()
    {
        // set() claims the waiter first, stop callback
        // loses the race -- no double resume
        async_event event;
        std::queue<continuation> q;
        queuing_executor ex(q);
        std::stop_source ss;

        bool done = false;
        std::error_code out_ec;

        auto t = [](async_event& ev,
            std::error_code& oec, bool& d) -> task<void> {
            auto [ec] = co_await ev.wait();
            oec = ec;
            d = true;
        }(event, out_ec, done);

        auto h = t.handle();
        t.release();
        await_context env;
        env.executor = executor_ref(ex);
        env.stop_token = ss.get_token();
        h.promise().set_environment(&env);
        h.resume();
        BOOST_TEST(!done);

        // set() first -- claims the waiter
        event.set();

        // Then request stop -- callback loses the race
        ss.request_stop();

        while(!q.empty())
        {
            q.front().h.resume();
            q.pop();
        }

        BOOST_TEST(done);
        BOOST_TEST(!out_ec);

        h.destroy();
    }

    void
    testSetWithInlineExecutor()
    {
        // Regression: set() must use post() so the waiter
        // is resumed even with an inline executor.
        async_event event;
        int dispatch_count = 0;
        test_executor ex(dispatch_count);

        bool resumed = false;
        auto t = [](async_event& ev, bool& r) -> task<void> {
            auto [ec] = co_await ev.wait();
            BOOST_TEST(!ec);
            r = true;
        }(event, resumed);

        auto h = t.handle();
        t.release();
        await_context env;
        env.executor = executor_ref(ex);
        h.promise().set_environment(&env);

        h.resume();
        BOOST_TEST(!resumed);

        event.set();

        BOOST_TEST(resumed);
        BOOST_TEST(h.done());
        h.destroy();
    }

    void
    testMultipleWaitersInlineExecutor()
    {
        // Regression: set() must resume all waiters via post()
        // even with an inline executor.
        async_event event;
        int dispatch_count = 0;
        test_executor ex(dispatch_count);

        int count = 0;

        auto make_waiter = [](async_event& ev,
            int& c) -> task<void> {
            auto [ec] = co_await ev.wait();
            BOOST_TEST(!ec);
            ++c;
        };

        auto t1 = make_waiter(event, count);
        auto t2 = make_waiter(event, count);
        auto t3 = make_waiter(event, count);

        auto h1 = t1.handle();
        auto h2 = t2.handle();
        auto h3 = t3.handle();
        t1.release();
        t2.release();
        t3.release();

        await_context env;
        env.executor = executor_ref(ex);
        h1.promise().set_environment(&env);
        h2.promise().set_environment(&env);
        h3.promise().set_environment(&env);

        h1.resume();
        h2.resume();
        h3.resume();
        BOOST_TEST_EQ(count, 0);

        event.set();

        BOOST_TEST_EQ(count, 3);

        h1.destroy();
        h2.destroy();
        h3.destroy();
    }

    void
    testCancellationWithInlineExecutor()
    {
        // Regression: cancel_fn must use post() so the waiter
        // is resumed even with an inline executor.
        async_event event;
        int dispatch_count = 0;
        test_executor ex(dispatch_count);
        std::stop_source ss;

        bool done = false;
        std::error_code out_ec;

        auto t = [](async_event& ev,
            std::error_code& oec, bool& d) -> task<void> {
            auto [ec] = co_await ev.wait();
            oec = ec;
            d = true;
        }(event, out_ec, done);

        auto h = t.handle();
        t.release();
        await_context env;
        env.executor = executor_ref(ex);
        env.stop_token = ss.get_token();
        h.promise().set_environment(&env);

        h.resume();
        BOOST_TEST(!done);

        ss.request_stop();

        BOOST_TEST(done);
        BOOST_TEST(out_ec == make_error_code(error::canceled));
        BOOST_TEST(!event.is_set());

        h.destroy();
    }

    static task<results<>>
    set_event_task(async_event& evt)
    {
        evt.set();
        co_return results<>{};
    }

    static task<void>
    when_all_set_event_main(bool& finished)
    {
        async_event evt;
        (void) co_await when_all(evt.wait(), set_event_task(evt));
        finished = true;
    }

    static task<void>
    simple_task(bool& finished)
    {
        finished = true;
        co_return;
    }

    void
    testRunAsyncThreadPool()
    {
        thread_pool pool(1);
        std::binary_semaphore done(0);
        bool finished = false;

        run_async(pool.get_executor(),
            [&]() { done.release(); },
            [&](std::exception_ptr) { done.release(); }
        )(simple_task(finished));

        done.acquire();
        BOOST_TEST(finished);
    }

    void
    testWhenAllSetEvent()
    {
        thread_pool pool(1);
        std::binary_semaphore done(0);
        bool finished = false;

        run_async(pool.get_executor(),
            [&]() { done.release(); },
            [&](std::exception_ptr) { done.release(); }
        )(when_all_set_event_main(finished));

        done.acquire();
        BOOST_TEST(finished);
    }

    // A destroyed event can no longer be set, so its waiters resume
    // with an error instead of disappearing.
    void testDestroyEventCancelsWaiters()
    {
        int down = 0;
        std::error_code ec;
        auto ev = std::make_unique<async_event>();
        auto waiter = [](async_event& e, std::error_code& out,
            destroy_count_t) -> task<void>
        {
            auto [r] = co_await e.wait();
            out = r;
        };
        std::queue<continuation> q;
        run_async(queuing_executor(q))(waiter(*ev, ec, destroy_count_t(down)));
        pump(q);
        ev.reset();
        BOOST_TEST_EQ(down, 0);
        BOOST_TEST_EQ(q.size(), 1u);
        pump(q);
        BOOST_TEST(ec == make_error_code(error::canceled));
        BOOST_TEST_EQ(down, 1);
    }

    // An event owned by a frame that an outer teardown destroys posts
    // every waiter; each then resumes with an error, or is destroyed
    // if its execution context is torn down too.
    void testCascadeCancelsManyWaiters()
    {
        constexpr int count = 64;
        int inner_down = 0, outer_down = 0, server_down = 0;
        int canceled = 0;
        parked slot;
        std::queue<continuation> q;
        queuing_executor ex(q);
        auto inner = [&](async_event& e, destroy_count_t) -> task<void>
        {
            auto [r] = co_await e.wait();
            if(r == make_error_code(error::canceled))
                ++canceled;
        };
        auto outer = [&](async_event& e, destroy_count_t) -> task<void>
        {
            co_await inner(e, destroy_count_t(inner_down));
        };
        auto leaf = [](parked* s) -> task<void>
        {
            co_await park_awaitable{s};
        };
        auto server = [&](destroy_count_t) -> task<void>
        {
            async_event ev;
            for(int i = 0; i < count; ++i)
                run_async(ex)(outer(ev, destroy_count_t(outer_down)));
            co_await leaf(&slot);
        };
        run_async(ex)(server(destroy_count_t(server_down)));
        pump(q);
        BOOST_TEST(slot);

        capy::abandon(slot);
        // Run only the posted unwind; the canceled waiters it posts
        // stay queued.
        BOOST_TEST_EQ(q.size(), 1u);
        auto unwind = q.front();
        q.pop();
        unwind.h.resume();
        BOOST_TEST_EQ(server_down, 1);
        BOOST_TEST_EQ(inner_down, 0);
        BOOST_TEST_EQ(q.size(), static_cast<std::size_t>(count));

        // Half resume, half are torn down with their context.
        for(int i = 0; i < count / 2; ++i)
        {
            auto c = q.front();
            q.pop();
            c.h.resume();
        }
        while(!q.empty())
        {
            capy::abandon(q.front());
            q.pop();
        }
        BOOST_TEST_EQ(canceled, count / 2);
        BOOST_TEST_EQ(inner_down, count);
        BOOST_TEST_EQ(outer_down, count);
    }

    // Posting a waiter can destroy it on the spot (here, a strand that
    // can no longer run it); set() must not touch an event that the
    // destroyed frame owned.
    void testSetWhenPostDestroysOwner()
    {
        std::queue<continuation> q;
        auto ctx = std::make_unique<closable_test_context>();
        queuing_executor qe(q);
        qe.ctx_ = ctx.get();
        strand<queuing_executor> s{qe};
        async_event* ev = nullptr;
        int down = 0;
        auto waiter = [](async_event*& out, destroy_count_t) -> task<void>
        {
            async_event e;
            out = &e;
            std::ignore = co_await e.wait();
        };
        run_async(s, std::allocator<std::byte>{})(
            waiter(ev, destroy_count_t(down)));
        pump(q);
        BOOST_TEST(ev);
        ctx->shutdown();
        ev->set();
        BOOST_TEST_EQ(down, 1);
    }

    // The same, with set() called from a destructor inside a teardown.
    void testSetFromTeardownWhenPostDestroysOwner()
    {
        std::queue<continuation> q;
        auto ctx = std::make_unique<closable_test_context>();
        queuing_executor qe(q);
        qe.ctx_ = ctx.get();
        strand<queuing_executor> s{qe};
        async_event* ev = nullptr;
        int down = 0;
        auto waiter = [](async_event*& out, destroy_count_t) -> task<void>
        {
            async_event e;
            out = &e;
            std::ignore = co_await e.wait();
        };
        run_async(s, std::allocator<std::byte>{})(
            waiter(ev, destroy_count_t(down)));
        pump(q);

        struct set_on_exit
        {
            async_event** e;
            ~set_on_exit() { (*e)->set(); }
        };
        parked slot;
        std::queue<continuation> q2;
        auto notifier = [](async_event** e,
            parked* sl) -> task<void>
        {
            set_on_exit g{e};
            co_await park_awaitable{sl};
        };
        run_async(queuing_executor(q2), std::allocator<std::byte>{})(
            notifier(&ev, &slot));
        pump(q2);
        BOOST_TEST(slot);
        ctx->shutdown();
        capy::abandon(slot);
        pump(q2);
        BOOST_TEST_EQ(down, 1);
    }

    // A waiter claimed by its stop request belongs to the executor's
    // queue: discarding it after the event is gone touches nothing.
    void testStopClaimedWaiterOutlivesEvent()
    {
        auto p = std::make_unique<async_event>();
        std::queue<continuation> q;
        std::stop_source ss;
        int down = 0;
        auto waiter = [](async_event& x, destroy_count_t) -> task<void>
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

    void
    run()
    {
        testStopClaimedWaiterOutlivesEvent();
        testSetWhenPostDestroysOwner();
        testSetFromTeardownWhenPostDestroysOwner();
        testDestroyEventCancelsWaiters();
        testCascadeCancelsManyWaiters();
        testInitialState();
        testSetAndClear();
        testWaitOnSetEvent();
        testSingleWaiter();
        testMultipleWaiters();
        testWaitAfterSet();
        testSetClearSetSequence();
        testResumeOrder();
        testNoWaitersSet();
        testCancellation();
        testCancelOneOfMultiple();
        testPreSignaledToken();
        testPreSignaledTokenOnSetEvent();
        testDestroyWhileSuspended();
        testSetThenCancelNoDouble();
        testSetWithInlineExecutor();
        testMultipleWaitersInlineExecutor();
        testCancellationWithInlineExecutor();
        testRunAsyncThreadPool();
        testWhenAllSetEvent();
    }
};

TEST_SUITE(
    async_event_test,
    "boost.capy.async_event");

} // capy
} // boost
