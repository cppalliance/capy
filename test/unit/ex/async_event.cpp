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
#include <boost/capy/ex/executor_ref.hpp>
#include <boost/capy/task.hpp>

#include "test/unit/test_helpers.hpp"

#include <queue>
#include <vector>

namespace boost {
namespace capy {

static_assert(IoAwaitable<async_event::wait_awaiter>);

namespace {

/** Run a task to completion by manually stepping through it.
*/
template<class T>
T run_task(task<T> t)
{
    auto h = t.handle();
    t.release();
    while (!h.done())
        h.resume();
    auto& p = h.promise();
    if (p.ep_)
    {
        auto ep = p.ep_;
        h.destroy();
        std::rethrow_exception(ep);
    }
    if constexpr (!std::is_void_v<T>)
    {
        auto result = std::move(*p.result_);
        h.destroy();
        return result;
    }
    else
    {
        h.destroy();
    }
}

} // namespace

struct async_event_test
{
    void
    testInitialState()
    {
        // Event is initially not set
        {
            async_event event;
            BOOST_TEST(!event.is_set());
        }
    }

    void
    testSetAndClear()
    {
        // set() sets the flag
        {
            async_event event;
            BOOST_TEST(!event.is_set());
            event.set();
            BOOST_TEST(event.is_set());
        }

        // clear() clears the flag
        {
            async_event event;
            event.set();
            BOOST_TEST(event.is_set());
            event.clear();
            BOOST_TEST(!event.is_set());
        }

        // Multiple set() calls are idempotent
        {
            async_event event;
            event.set();
            event.set();
            event.set();
            BOOST_TEST(event.is_set());
        }

        // Multiple clear() calls are idempotent
        {
            async_event event;
            event.clear();
            event.clear();
            BOOST_TEST(!event.is_set());
        }

        // Set/clear cycle
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
    }

    void
    testWaitOnSetEvent()
    {
        // wait() on already-set event returns immediately
        {
            async_event event;
            event.set();

            auto awaiter = event.wait();
            BOOST_TEST(awaiter.await_ready());
        }

        // wait() on unset event does not complete immediately
        {
            async_event event;

            auto awaiter = event.wait();
            BOOST_TEST(!awaiter.await_ready());
        }
    }

    void
    testSingleWaiter()
    {
        // Single waiter is resumed when event is set
        {
            async_event event;
            bool resumed = false;

            auto waiter = [&]() -> task<void> {
                co_await event.wait();
                resumed = true;
            };

            auto t = waiter();
            auto h = t.handle();
            t.release();

            // Start the task (it will suspend on wait())
            h.resume();
            BOOST_TEST(!resumed);
            BOOST_TEST(!h.done());

            // Set the event - waiter should be resumed
            event.set();
            BOOST_TEST(resumed);
            BOOST_TEST(h.done());

            h.destroy();
        }
    }

    void
    testMultipleWaiters()
    {
        // Multiple waiters are all resumed when event is set
        {
            async_event event;
            int resumed_count = 0;

            auto waiter = [&]() -> task<void> {
                co_await event.wait();
                ++resumed_count;
            };

            auto t1 = waiter();
            auto t2 = waiter();
            auto t3 = waiter();

            auto h1 = t1.handle();
            auto h2 = t2.handle();
            auto h3 = t3.handle();

            t1.release();
            t2.release();
            t3.release();

            // Start all tasks (they will suspend on wait())
            h1.resume();
            h2.resume();
            h3.resume();

            BOOST_TEST_EQ(resumed_count, 0);
            BOOST_TEST(!h1.done());
            BOOST_TEST(!h2.done());
            BOOST_TEST(!h3.done());

            // Set the event - all waiters should be resumed
            event.set();

            BOOST_TEST_EQ(resumed_count, 3);
            BOOST_TEST(h1.done());
            BOOST_TEST(h2.done());
            BOOST_TEST(h3.done());

            h1.destroy();
            h2.destroy();
            h3.destroy();
        }
    }

    void
    testWaitAfterSet()
    {
        // Waiting after event is set returns immediately
        {
            async_event event;
            event.set();

            bool completed = false;
            auto waiter = [&]() -> task<void> {
                co_await event.wait();
                completed = true;
            };

            run_task<void>(waiter());
            BOOST_TEST(completed);
        }
    }

    void
    testClearWhileWaiting()
    {
        // Waiters added before set() are still resumed even if
        // clear() was called earlier
        {
            async_event event;
            bool resumed = false;

            auto waiter = [&]() -> task<void> {
                co_await event.wait();
                resumed = true;
            };

            auto t = waiter();
            auto h = t.handle();
            t.release();

            h.resume();
            BOOST_TEST(!resumed);

            // Clear (no-op since not set)
            event.clear();
            BOOST_TEST(!resumed);

            // Set
            event.set();
            BOOST_TEST(resumed);

            h.destroy();
        }
    }

    void
    testSetClearSetSequence()
    {
        // Set/clear/set sequence with waiters
        {
            async_event event;
            int wait_count = 0;

            auto waiter = [&]() -> task<void> {
                co_await event.wait();
                ++wait_count;
            };

            // First waiter
            auto t1 = waiter();
            auto h1 = t1.handle();
            t1.release();
            h1.resume();

            BOOST_TEST_EQ(wait_count, 0);

            // Set - first waiter resumes
            event.set();
            BOOST_TEST_EQ(wait_count, 1);
            BOOST_TEST(h1.done());
            h1.destroy();

            // Clear
            event.clear();

            // Second waiter - should block
            auto t2 = waiter();
            auto h2 = t2.handle();
            t2.release();
            h2.resume();

            BOOST_TEST_EQ(wait_count, 1);
            BOOST_TEST(!h2.done());

            // Set again - second waiter resumes
            event.set();
            BOOST_TEST_EQ(wait_count, 2);
            BOOST_TEST(h2.done());
            h2.destroy();
        }
    }

    void
    testResumeOrder()
    {
        // Waiters are resumed in FIFO order
        {
            async_event event;
            std::vector<int> order;

            auto waiter = [&](int id) -> task<void> {
                co_await event.wait();
                order.push_back(id);
            };

            auto t1 = waiter(1);
            auto t2 = waiter(2);
            auto t3 = waiter(3);

            auto h1 = t1.handle();
            auto h2 = t2.handle();
            auto h3 = t3.handle();

            t1.release();
            t2.release();
            t3.release();

            // Start in order 1, 2, 3
            h1.resume();
            h2.resume();
            h3.resume();

            event.set();

            // Should be resumed in same order
            BOOST_TEST_EQ(order.size(), 3u);
            BOOST_TEST_EQ(order[0], 1);
            BOOST_TEST_EQ(order[1], 2);
            BOOST_TEST_EQ(order[2], 3);

            h1.destroy();
            h2.destroy();
            h3.destroy();
        }
    }

    void
    testWithExecutor()
    {
        // IoAwaitable protocol with executor
        {
            async_event event;
            std::queue<coro> queue;
            queuing_executor ex(queue);

            bool resumed = false;
            auto waiter = [&]() -> task<void> {
                co_await event.wait();
                resumed = true;
            };

            auto t = waiter();
            auto h = t.handle();
            t.release();

            // Start via IoAwaitable protocol
            auto awaiter = event.wait();
            BOOST_TEST(!awaiter.await_ready());

            // Suspend with executor
            auto result = awaiter.await_suspend(h, ex, std::stop_token{});
            BOOST_TEST(result == std::noop_coroutine());
            BOOST_TEST(!resumed);

            // Set the event - should dispatch through executor
            event.set();
            BOOST_TEST(!resumed);  // Not resumed yet, queued
            BOOST_TEST_EQ(queue.size(), 1u);

            // Process the queue
            while (!queue.empty())
            {
                auto coro = queue.front();
                queue.pop();
                coro.resume();
            }

            h.destroy();
        }
    }

    void
    testMultipleWaitersWithExecutor()
    {
        // Multiple waiters with executor all get dispatched
        {
            async_event event;
            std::queue<coro> queue;
            queuing_executor ex(queue);

            // Set up waiters using await_suspend directly
            async_event::wait_awaiter a1 = event.wait();
            async_event::wait_awaiter a2 = event.wait();
            async_event::wait_awaiter a3 = event.wait();

            // Use noop_coroutine as dummy handles
            auto h = std::noop_coroutine();

            a1.await_suspend(h, ex, std::stop_token{});
            a2.await_suspend(h, ex, std::stop_token{});
            a3.await_suspend(h, ex, std::stop_token{});

            BOOST_TEST(queue.empty());

            // Set event - all should be dispatched
            event.set();
            BOOST_TEST_EQ(queue.size(), 3u);
        }
    }

    void
    testAwaitReadyFastPath()
    {
        // await_ready returns true when event is set (fast path)
        {
            async_event event;
            event.set();

            auto awaiter = event.wait();
            BOOST_TEST(awaiter.await_ready());
            // No suspension needed
        }

        // await_ready returns false when event is not set
        {
            async_event event;

            auto awaiter = event.wait();
            BOOST_TEST(!awaiter.await_ready());
        }
    }

    void
    testNoWaitersSet()
    {
        // set() with no waiters is safe
        {
            async_event event;
            event.set();  // Should not crash
            BOOST_TEST(event.is_set());
        }
    }

    void
    testSetMultipleTimes()
    {
        // Calling set() multiple times is safe
        {
            async_event event;
            bool resumed = false;

            auto waiter = [&]() -> task<void> {
                co_await event.wait();
                resumed = true;
            };

            auto t = waiter();
            auto h = t.handle();
            t.release();
            h.resume();

            BOOST_TEST(!resumed);

            event.set();
            BOOST_TEST(resumed);

            // Additional set() calls should be no-op
            event.set();
            event.set();
            BOOST_TEST(event.is_set());

            h.destroy();
        }
    }

    void
    testInterleavedWaiters()
    {
        // Interleaved wait/set operations
        {
            async_event event;
            std::vector<int> results;

            auto waiter = [&](int id) -> task<void> {
                co_await event.wait();
                results.push_back(id);
            };

            // Start waiter 1
            auto t1 = waiter(1);
            auto h1 = t1.handle();
            t1.release();
            h1.resume();

            // Set event - waiter 1 resumes
            event.set();
            BOOST_TEST_EQ(results.size(), 1u);
            BOOST_TEST_EQ(results[0], 1);

            // Waiter 2 should complete immediately (event still set)
            auto t2 = waiter(2);
            run_task<void>(std::move(t2));
            BOOST_TEST_EQ(results.size(), 2u);
            BOOST_TEST_EQ(results[1], 2);

            // Clear and add waiter 3
            event.clear();
            auto t3 = waiter(3);
            auto h3 = t3.handle();
            t3.release();
            h3.resume();

            // Waiter 3 should be blocked
            BOOST_TEST_EQ(results.size(), 2u);

            // Set again
            event.set();
            BOOST_TEST_EQ(results.size(), 3u);
            BOOST_TEST_EQ(results[2], 3);

            h1.destroy();
            h3.destroy();
        }
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
        testClearWhileWaiting();
        testSetClearSetSequence();
        testResumeOrder();
        testWithExecutor();
        testMultipleWaitersWithExecutor();
        testAwaitReadyFastPath();
        testNoWaitersSet();
        testSetMultipleTimes();
        testInterleavedWaiters();
    }
};

TEST_SUITE(
    async_event_test,
    "boost.capy.async_event");

} // capy
} // boost
