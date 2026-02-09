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

#include <boost/capy/concept/io_awaitable.hpp>
#include <boost/capy/error.hpp>
#include <boost/capy/ex/executor_ref.hpp>
#include <boost/capy/io_result.hpp>
#include <boost/capy/task.hpp>

#include <queue>
#include <stop_token>
#include <vector>

#include "test_helpers.hpp"

namespace boost {
namespace capy {

static_assert(IoAwaitable<async_event::wait_awaiter>);

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
        std::queue<coro> q;
        queuing_executor ex(q);

        bool resumed = false;
        auto t = [](async_event& ev, bool& r) -> task<void> {
            auto [ec] = co_await ev.wait();
            BOOST_TEST(!ec);
            r = true;
        }(event, resumed);

        auto h = t.handle();
        t.release();
        h.promise().set_executor(ex);
        h.promise().set_stop_token({});

        h.resume();
        BOOST_TEST(!resumed);

        event.set();

        while(!q.empty())
        {
            q.front().resume();
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
        std::queue<coro> q;
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
        h1.promise().set_executor(ex);
        h1.promise().set_stop_token({});
        h2.promise().set_executor(ex);
        h2.promise().set_stop_token({});
        h3.promise().set_executor(ex);
        h3.promise().set_stop_token({});

        h1.resume();
        h2.resume();
        h3.resume();
        BOOST_TEST_EQ(count, 0);

        event.set();

        while(!q.empty())
        {
            q.front().resume();
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
        h.promise().set_executor(ex);
        h.promise().set_stop_token({});
        h.resume();

        BOOST_TEST(done);
        h.destroy();
    }

    void
    testSetClearSetSequence()
    {
        async_event event;
        std::queue<coro> q;
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
        h1.promise().set_executor(ex);
        h1.promise().set_stop_token({});
        h1.resume();
        BOOST_TEST_EQ(count, 0);

        event.set();
        while(!q.empty())
        {
            q.front().resume();
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
        h2.promise().set_executor(ex);
        h2.promise().set_stop_token({});
        h2.resume();
        BOOST_TEST_EQ(count, 1);

        event.set();
        while(!q.empty())
        {
            q.front().resume();
            q.pop();
        }
        BOOST_TEST_EQ(count, 2);
        h2.destroy();
    }

    void
    testResumeOrder()
    {
        async_event event;
        std::queue<coro> q;
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
        h1.promise().set_executor(ex);
        h1.promise().set_stop_token({});
        h2.promise().set_executor(ex);
        h2.promise().set_stop_token({});
        h3.promise().set_executor(ex);
        h3.promise().set_stop_token({});

        h1.resume();
        h2.resume();
        h3.resume();

        event.set();
        while(!q.empty())
        {
            q.front().resume();
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
        std::queue<coro> q;
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
        h.promise().set_executor(ex);
        h.promise().set_stop_token(ss.get_token());

        h.resume();
        BOOST_TEST(!done);

        ss.request_stop();

        while(!q.empty())
        {
            q.front().resume();
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
        std::queue<coro> q;
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
        h1.promise().set_executor(ex);
        h1.promise().set_stop_token(ss.get_token());

        auto h2 = w2.handle();
        w2.release();
        h2.promise().set_executor(ex);
        h2.promise().set_stop_token({});

        h1.resume();
        h2.resume();
        BOOST_TEST(!w1_done);
        BOOST_TEST(!w2_done);

        // Cancel w1
        ss.request_stop();
        while(!q.empty())
        {
            q.front().resume();
            q.pop();
        }
        BOOST_TEST(w1_done);
        BOOST_TEST(w1_ec == make_error_code(error::canceled));
        BOOST_TEST(!w2_done);

        // Set event -> w2 wakes
        event.set();
        while(!q.empty())
        {
            q.front().resume();
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
        std::queue<coro> q;
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
        h.promise().set_executor(ex);
        h.promise().set_stop_token(ss.get_token());

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
        h.promise().set_executor(ex);
        h.promise().set_stop_token(ss.get_token());
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
        std::queue<coro> q;
        queuing_executor ex(q);

        auto t = [](async_event& ev) -> task<void> {
            auto [ec] = co_await ev.wait();
            (void)ec;
        }(event);

        auto h = t.handle();
        t.release();
        h.promise().set_executor(ex);
        h.promise().set_stop_token({});
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
        std::queue<coro> q;
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
        h.promise().set_executor(ex);
        h.promise().set_stop_token(ss.get_token());
        h.resume();
        BOOST_TEST(!done);

        // set() first -- claims the waiter
        event.set();

        // Then request stop -- callback loses the race
        ss.request_stop();

        while(!q.empty())
        {
            q.front().resume();
            q.pop();
        }

        BOOST_TEST(done);
        BOOST_TEST(!out_ec);

        h.destroy();
    }

    void
    run()
    {
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
    }
};

TEST_SUITE(
    async_event_test,
    "boost.capy.async_event");

} // capy
} // boost
