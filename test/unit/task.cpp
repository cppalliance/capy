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
    // executor affinity tests
    //----------------------------------------------------------

    void
    testExecutorDefault()
    {
        // task<T> executor defaults to empty (no affinity)
        {
            auto t = returns_int();
            auto& p = t.handle().promise();
            BOOST_TEST(!p.ex);
        }

        // task<void> executor defaults to empty (no affinity)
        {
            auto t = void_task_basic();
            auto& p = t.handle().promise();
            BOOST_TEST(!p.ex);
        }
    }

    static task<int>
    task_with_async_for_affinity_test()
    {
        int v = co_await async_returns_value();
        co_return v + 1;
    }

    void
    testExecutorUsedByAwait()
    {
        // Verify that executor is used when awaiting
        sync_executor ctx;
        executor ex(ctx);

        auto t = task_with_async_for_affinity_test();
        t.on(ex);

        BOOST_TEST_EQ(run_task(t), 124);
        // Work should have been posted through the executor
        BOOST_TEST_GE(ctx.submit_count.load(), 1);
    }

    static task<void>
    void_task_with_async_for_affinity_test()
    {
        auto v = co_await async_returns_value();
        (void)v;
        co_return;
    }

    void
    testVoidTaskExecutorUsedByAwait()
    {
        // Verify that executor is used for void tasks
        sync_executor ctx;
        executor ex(ctx);
        bool done = false;

        auto t = void_task_with_async_for_affinity_test();
        t.on(ex);

        t.handle().promise().on_done = [&done]{ done = true; };
        t.handle().resume();

        BOOST_TEST(done);
        // Work should have been posted through the executor
        BOOST_TEST_GE(ctx.submit_count.load(), 1);
    }

    //----------------------------------------------------------
    // on() method tests
    //----------------------------------------------------------

    void
    testOnSetsExecutor()
    {
        // Verify on() sets the executor for task<T>
        sync_executor ctx;
        executor ex(ctx);

        auto t = task_with_async_for_affinity_test();
        t.on(ex);

        // Executor should now be set
        BOOST_TEST(static_cast<bool>(t.handle().promise().ex));
        BOOST_TEST_EQ(run_task(t), 124);
        // Work should have been posted through the executor
        BOOST_TEST_GE(ctx.submit_count.load(), 1);
    }

    void
    testOnSetsExecutorVoid()
    {
        // Verify on() sets the executor for task<void>
        sync_executor ctx;
        executor ex(ctx);

        auto t = void_task_with_async_for_affinity_test();
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

    //----------------------------------------------------------
    // Affinity propagation tests (ABC problem)
    //----------------------------------------------------------

    static task<int>
    inner_task_c()
    {
        co_return co_await async_returns_value();
    }

    static task<int>
    middle_task_b()
    {
        int v = co_await inner_task_c();
        co_return v + 1;
    }

    static task<int>
    outer_task_a()
    {
        int v = co_await middle_task_b();
        co_return v + 1;
    }

    void
    testAffinityPropagation()
    {
        // Verify affinity propagates through task chain (ABC problem)
        // a has affinity, b and c should inherit it
        sync_executor ctx;
        executor ex(ctx);

        auto t = outer_task_a();
        t.on(ex);

        BOOST_TEST_EQ(run_task(t), 125);  // 123 + 1 + 1
        // All async completions should dispatch through executor
        BOOST_TEST_GE(ctx.submit_count.load(), 1);
    }

    static task<void>
    inner_void_task_c()
    {
        co_await async_returns_value();
        co_return;
    }

    static task<void>
    middle_void_task_b()
    {
        co_await inner_void_task_c();
        co_return;
    }

    static task<void>
    outer_void_task_a()
    {
        co_await middle_void_task_b();
        co_return;
    }

    void
    testAffinityPropagationVoid()
    {
        // Verify affinity propagates through void task chain
        sync_executor ctx;
        executor ex(ctx);

        auto t = outer_void_task_a();
        t.on(ex);

        bool done = false;
        t.handle().promise().on_done = [&done]{ done = true; };
        t.handle().resume();

        BOOST_TEST(done);
        BOOST_TEST_GE(ctx.submit_count.load(), 1);
    }

    void
    testExplicitAffinityOverridesInheritance()
    {
        // Verify explicit affinity via .on() is not overwritten
        sync_executor ctx1;
        sync_executor ctx2;
        executor ex1(ctx1);
        executor ex2(ctx2);

        // Create a task with explicit affinity
        auto make_inner = [ex2]() -> task<int> {
            return inner_task_c().on(ex2);  // explicit affinity to ex2
        };

        auto outer = [make_inner]() -> task<int> {
            int v = co_await make_inner();
            co_return v + 1;
        };

        auto t = outer();
        t.on(ex1);  // outer has affinity to ex1

        BOOST_TEST_EQ(run_task(t), 124);
        // Inner should use ex2, not inherit ex1
        BOOST_TEST_GE(ctx2.submit_count.load(), 1);
    }

    void
    testNoAffinityRunsInline()
    {
        // Verify that without affinity, no executor dispatch occurs
        auto t = outer_task_a();  // no .on() call

        BOOST_TEST_EQ(run_task(t), 125);
        // Task should complete without any executor
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

        // executor affinity tests
        testExecutorDefault();
        testExecutorUsedByAwait();
        testVoidTaskExecutorUsedByAwait();

        // on() method tests
        testOnSetsExecutor();
        testOnSetsExecutorVoid();
        testOnFluentSyntax();
        testOnFluentSyntaxVoid();
        testOnLvalueReturnsReference();

        // affinity propagation tests (ABC problem)
        testAffinityPropagation();
        testAffinityPropagationVoid();
        testExplicitAffinityOverridesInheritance();
        testNoAffinityRunsInline();
    }
};

TEST_SUITE(
    task_test,
    "boost.capy.task");

} // capy
} // boost

#endif
