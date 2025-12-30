//
// Copyright (c) 2025 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Test that header file is self-contained.
#include <boost/capy/task.hpp>

#ifdef BOOST_CAPY_HAS_CORO

#include <boost/capy/async_result.hpp>
#include <boost/capy/executor.hpp>

#include "test_suite.hpp"

#include <atomic>
#include <cstdlib>
#include <stdexcept>
#include <string>

namespace boost {
namespace capy {

/** Simple synchronous executor for testing.
*/
struct sync_executor
{
    friend struct executor::access;

    std::atomic<int> alloc_count{0};
    std::atomic<int> submit_count{0};

private:
    struct header
    {
        std::size_t size;
    };

    void*
    allocate(std::size_t size, std::size_t /*align*/)
    {
        ++alloc_count;
        std::size_t total = sizeof(header) + size;
        void* p = std::malloc(total);
        auto* h = new(p) header{total};
        return h + 1;
    }

    void
    deallocate(void* p, std::size_t /*size*/, std::size_t /*align*/)
    {
        auto* h = static_cast<header*>(p) - 1;
        std::free(h);
    }

    void
    submit(executor::work* w)
    {
        ++submit_count;
        w->invoke();
        w->~work();
        deallocate(w, 0, 0);
    }
};

template<class T>
T run_task(task<T>& t)
{
    while (!t.handle().done())
        t.handle().resume();
    return t.await_resume();
}

struct test_exception : std::runtime_error
{
    explicit test_exception(const char* msg)
        : std::runtime_error(msg)
    {
    }
};

struct task_test
{
    static task<int>
    returns_int()
    {
        co_return 42;
    }

    static task<std::string>
    returns_string()
    {
        co_return "hello";
    }

    void
    testReturnValue()
    {
        // task returning int
        {
            auto t = returns_int();
            BOOST_TEST_EQ(run_task(t), 42);
        }

        // task returning string
        {
            auto t = returns_string();
            BOOST_TEST_EQ(run_task(t), "hello");
        }
    }

    static task<int>
    throws_exception()
    {
        throw test_exception("test error");
        co_return 0;
    }

    static task<int>
    throws_std_exception()
    {
        throw std::runtime_error("runtime error");
        co_return 0;
    }

    void
    testException()
    {
        // task that throws custom exception
        {
            auto t = throws_exception();
            while (!t.handle().done())
                t.handle().resume();
            BOOST_TEST_THROWS(t.await_resume(), test_exception);
        }

        // task that throws std::runtime_error
        {
            auto t = throws_std_exception();
            while (!t.handle().done())
                t.handle().resume();
            BOOST_TEST_THROWS(t.await_resume(), std::runtime_error);
        }
    }

    static task<int>
    inner_task_value()
    {
        co_return 100;
    }

    static task<int>
    outer_task_awaits_inner()
    {
        int v = co_await inner_task_value();
        co_return v + 1;
    }

    static task<int>
    inner_task_throws()
    {
        throw test_exception("inner exception");
        co_return 0;
    }

    static task<int>
    outer_task_awaits_throwing_inner()
    {
        int v = co_await inner_task_throws();
        co_return v + 1;
    }

    static task<int>
    outer_task_catches_inner_exception()
    {
        try
        {
            (void)co_await inner_task_throws();
            co_return -1;
        }
        catch (test_exception const&)
        {
            co_return 999;
        }
    }

    static task<int>
    chained_tasks()
    {
        auto inner = []() -> task<int> {
            co_return 10;
        };

        auto middle = [&]() -> task<int> {
            int v = co_await inner();
            co_return v * 2;
        };

        int v = co_await middle();
        co_return v + 5;
    }

    void
    testTaskAwaitsTask()
    {
        // outer task awaits inner task with value
        {
            auto t = outer_task_awaits_inner();
            BOOST_TEST_EQ(run_task(t), 101);
        }

        // outer task awaits inner task that throws
        {
            auto t = outer_task_awaits_throwing_inner();
            while (!t.handle().done())
                t.handle().resume();
            BOOST_TEST_THROWS(t.await_resume(), test_exception);
        }

        // outer task catches exception from inner task
        {
            auto t = outer_task_catches_inner_exception();
            BOOST_TEST_EQ(run_task(t), 999);
        }

        // chained tasks (3 levels)
        {
            auto t = chained_tasks();
            BOOST_TEST_EQ(run_task(t), 25);
        }
    }

    void
    testMoveOperations()
    {
        // move constructor
        {
            auto t1 = returns_int();
            auto h = t1.handle();
            BOOST_TEST(h);

            task<int> t2(std::move(t1));
            BOOST_TEST(!t1.handle());
            BOOST_TEST(t2.handle() == h);

            BOOST_TEST_EQ(run_task(t2), 42);
        }

        // release()
        {
            auto t = returns_int();
            auto h = t.release();
            BOOST_TEST(h);
            BOOST_TEST(!t.handle());

            while (!h.done())
                h.resume();
            auto& result = h.promise().result;
            BOOST_TEST_EQ(result.index(), 1u);
            BOOST_TEST_EQ(std::get<1>(result), 42);

            h.destroy();
        }
    }

    static async_result<int>
    async_returns_value()
    {
        return make_async_result<int>(
            [](auto cb) {
                cb(123);
            });
    }

    static async_result<int>
    async_with_delayed_completion()
    {
        return make_async_result<int>(
            [](auto cb) {
                cb(456);
            });
    }

    static task<int>
    task_awaits_async_result()
    {
        int v = co_await async_returns_value();
        co_return v + 1;
    }

    static task<int>
    task_awaits_multiple_async_results()
    {
        int v1 = co_await async_returns_value();
        int v2 = co_await async_with_delayed_completion();
        co_return v1 + v2;
    }

    void
    testTaskAwaitsAsyncResult()
    {
        // task awaits single async_result
        {
            auto t = task_awaits_async_result();
            BOOST_TEST_EQ(run_task(t), 124);
        }

        // task awaits multiple async_results
        {
            auto t = task_awaits_multiple_async_results();
            BOOST_TEST_EQ(run_task(t), 579);
        }
    }

    void
    testAwaitReady()
    {
        auto t = returns_int();
        BOOST_TEST(!t.await_ready());
    }

    //----------------------------------------------------------
    // task<void> tests
    //----------------------------------------------------------

    static task<void>
    void_task_basic()
    {
        co_return;
    }

    static task<void>
    void_task_throws()
    {
        throw test_exception("void task exception");
        co_return;
    }

    void
    testVoidTaskBasic()
    {
        bool done = false;
        auto t = void_task_basic();
        t.handle().promise().on_done = [&done]{ done = true; };
        t.handle().resume();
        BOOST_TEST(done);
        t.await_resume(); // should not throw
    }

    void
    testVoidTaskException()
    {
        auto t = void_task_throws();
        t.handle().promise().on_done = []{ };
        t.handle().resume();
        BOOST_TEST_THROWS(t.await_resume(), test_exception);
    }

    static task<void>
    void_task_awaits_value()
    {
        int v = co_await returns_int();
        (void)v;
        co_return;
    }

    static task<void>
    void_task_awaits_void()
    {
        co_await void_task_basic();
        co_return;
    }

    void
    testVoidTaskAwaits()
    {
        // void task awaits value-returning task
        {
            bool done = false;
            auto t = void_task_awaits_value();
            t.handle().promise().on_done = [&done]{ done = true; };
            t.handle().resume();
            BOOST_TEST(done);
        }

        // void task awaits another void task
        {
            bool done = false;
            auto t = void_task_awaits_void();
            t.handle().promise().on_done = [&done]{ done = true; };
            t.handle().resume();
            BOOST_TEST(done);
        }
    }

    static task<void>
    void_task_chain_step()
    {
        co_return;
    }

    static task<void>
    void_task_chain()
    {
        co_await void_task_chain_step();
        co_await void_task_chain_step();
        co_await void_task_chain_step();
        co_return;
    }

    void
    testVoidTaskChain()
    {
        bool done = false;
        auto t = void_task_chain();
        t.handle().promise().on_done = [&done]{ done = true; };
        t.handle().resume();
        BOOST_TEST(done);
    }

    void
    testVoidTaskMove()
    {
        auto t1 = void_task_basic();
        auto h = t1.handle();
        BOOST_TEST(h);

        task<void> t2(std::move(t1));
        BOOST_TEST(!t1.handle());
        BOOST_TEST(t2.handle() == h);
    }

    static task<void>
    void_task_awaits_async_result()
    {
        int v = co_await async_returns_value();
        (void)v;
        co_return;
    }

    void
    testVoidTaskAwaitsAsyncResult()
    {
        bool done = false;
        auto t = void_task_awaits_async_result();
        t.handle().promise().on_done = [&done]{ done = true; };
        t.handle().resume();
        BOOST_TEST(done);
    }

    //----------------------------------------------------------
    // dispatcher tests
    //----------------------------------------------------------

    void
    testDispatcherDefault()
    {
        // task<T> dispatcher defaults to immediate (non-empty)
        {
            auto t = returns_int();
            auto& p = t.handle().promise();
            BOOST_TEST(static_cast<bool>(p.dispatcher));
        }

        // task<void> dispatcher defaults to immediate (non-empty)
        {
            auto t = void_task_basic();
            auto& p = t.handle().promise();
            BOOST_TEST(static_cast<bool>(p.dispatcher));
        }
    }

    static task<int>
    task_with_async_for_dispatcher_test()
    {
        int v = co_await async_returns_value();
        co_return v + 1;
    }

    void
    testDispatcherCalledByAwait()
    {
        // Verify that dispatcher is called when awaiting
        int dispatch_call_count = 0;

        auto t = task_with_async_for_dispatcher_test();
        auto& p = t.handle().promise();
        p.dispatcher = [&dispatch_call_count](std::function<void()> f) {
            ++dispatch_call_count;
            f();
        };

        BOOST_TEST_EQ(run_task(t), 124);
        // Dispatcher should have been called via make_affine
        BOOST_TEST_GE(dispatch_call_count, 1);
    }

    static task<void>
    void_task_with_async_for_dispatcher_test()
    {
        auto v = co_await async_returns_value();
        (void)v;
        co_return;
    }

    void
    testVoidTaskDispatcherCalledByAwait()
    {
        // Verify that dispatcher is called for void tasks
        int dispatch_call_count = 0;
        bool done = false;

        auto t = void_task_with_async_for_dispatcher_test();
        auto& p = t.handle().promise();
        p.dispatcher = [&dispatch_call_count](std::function<void()> f) {
            ++dispatch_call_count;
            f();
        };

        p.on_done = [&done]{ done = true; };
        t.handle().resume();

        BOOST_TEST(done);
        // Dispatcher should have been called via make_affine
        BOOST_TEST_GE(dispatch_call_count, 1);
    }

    //----------------------------------------------------------
    // on() executor affinity tests
    //----------------------------------------------------------

    void
    testOnSetsDispatcher()
    {
        // Verify on() sets the dispatcher for task<T>
        sync_executor ctx;
        executor ex(ctx);

        auto t = task_with_async_for_dispatcher_test();
        t.on(ex);

        // Dispatcher should now be set to post through executor
        BOOST_TEST(static_cast<bool>(t.handle().promise().dispatcher));
        BOOST_TEST_EQ(run_task(t), 124);
        // Work should have been posted through the executor
        BOOST_TEST_GE(ctx.submit_count.load(), 1);
    }

    void
    testOnSetsDispatcherVoid()
    {
        // Verify on() sets the dispatcher for task<void>
        sync_executor ctx;
        executor ex(ctx);

        auto t = void_task_with_async_for_dispatcher_test();
        t.on(ex);

        bool done = false;
        t.handle().promise().on_done = [&done]{ done = true; };
        t.handle().resume();

        BOOST_TEST(done);
        // Work should have been posted through the executor
        BOOST_TEST_GE(ctx.submit_count.load(), 1);
    }

    void
    testOnFluentSyntax()
    {
        // Verify fluent syntax works with rvalue
        sync_executor ctx;
        executor ex(ctx);

        auto make_task = []() -> task<int> {
            co_return co_await async_returns_value();
        };

        auto t = make_task().on(ex);
        BOOST_TEST_EQ(run_task(t), 123);
        BOOST_TEST_GE(ctx.submit_count.load(), 1);
    }

    void
    testOnFluentSyntaxVoid()
    {
        // Verify fluent syntax works with rvalue for void tasks
        sync_executor ctx;
        executor ex(ctx);

        auto make_task = []() -> task<void> {
            co_await async_returns_value();
            co_return;
        };

        auto t = make_task().on(ex);
        bool done = false;
        t.handle().promise().on_done = [&done]{ done = true; };
        t.handle().resume();

        BOOST_TEST(done);
        BOOST_TEST_GE(ctx.submit_count.load(), 1);
    }

    void
    testOnLvalueReturnsReference()
    {
        // Verify on() returns reference to same task for lvalue
        sync_executor ctx;
        executor ex(ctx);

        auto t = returns_int();
        auto& ref = t.on(ex);
        BOOST_TEST(&ref == &t);
    }

    void
    run()
    {
        testReturnValue();
        testException();
        testTaskAwaitsTask();
        testMoveOperations();
        testTaskAwaitsAsyncResult();
        testAwaitReady();

        // task<void> tests
        testVoidTaskBasic();
        testVoidTaskException();
        testVoidTaskAwaits();
        testVoidTaskChain();
        testVoidTaskMove();
        testVoidTaskAwaitsAsyncResult();

        // dispatcher tests
        testDispatcherDefault();
        testDispatcherCalledByAwait();
        testVoidTaskDispatcherCalledByAwait();

        // on() executor affinity tests
        testOnSetsDispatcher();
        testOnSetsDispatcherVoid();
        testOnFluentSyntax();
        testOnFluentSyntaxVoid();
        testOnLvalueReturnsReference();
    }
};

TEST_SUITE(
    task_test,
    "boost.capy.task");

} // capy
} // boost

#endif
