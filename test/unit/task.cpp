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

#include <boost/capy/async_op.hpp>
#include <boost/capy/executor.hpp>

#include "test_suite.hpp"

#include <atomic>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <vector>

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

[[noreturn]] inline void
throw_test_exception(char const* msg)
{
    throw test_exception(msg);
}

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
            auto& result = h.promise().result_;
            BOOST_TEST(result.has_value());
            BOOST_TEST_EQ(*result, 42);

            h.destroy();
        }
    }

    static async_op<int>
    async_returns_value()
    {
        return make_async_op<int>(
            [](auto cb) {
                cb(123);
            });
    }

    static async_op<int>
    async_with_delayed_completion()
    {
        return make_async_op<int>(
            [](auto cb) {
                cb(456);
            });
    }

    static task<int>
    task_awaits_async_op()
    {
        int v = co_await async_returns_value();
        co_return v + 1;
    }

    static task<int>
    task_awaits_multiple_async_ops()
    {
        int v1 = co_await async_returns_value();
        int v2 = co_await async_with_delayed_completion();
        co_return v1 + v2;
    }

    void
    testTaskAwaitsAsyncResult()
    {
        // task awaits single async_op
        {
            auto t = task_awaits_async_op();
            BOOST_TEST_EQ(run_task(t), 124);
        }

        // task awaits multiple async_ops
        {
            auto t = task_awaits_multiple_async_ops();
            BOOST_TEST_EQ(run_task(t), 579);
        }
    }

    void
    testAwaitReady()
    {
        auto t = returns_int();
        BOOST_TEST(!t.await_ready());
    }

    // task<void> tests

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
        auto t = void_task_basic();
        while (!t.handle().done())
            t.handle().resume();
        t.await_resume(); // should not throw
    }

    void
    testVoidTaskException()
    {
        auto t = void_task_throws();
        while (!t.handle().done())
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
            auto t = void_task_awaits_value();
            while (!t.handle().done())
                t.handle().resume();
            t.await_resume();
        }

        // void task awaits another void task
        {
            auto t = void_task_awaits_void();
            while (!t.handle().done())
                t.handle().resume();
            t.await_resume();
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
        auto t = void_task_chain();
        while (!t.handle().done())
            t.handle().resume();
        t.await_resume();
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
    void_task_awaits_async_op()
    {
        int v = co_await async_returns_value();
        (void)v;
        co_return;
    }

    void
    testVoidTaskAwaitsAsyncResult()
    {
        auto t = void_task_awaits_async_op();
        while (!t.handle().done())
            t.handle().resume();
        t.await_resume();
    }

    // executor affinity tests

    void
    testExecutorDefault()
    {
        // task<T> executor defaults to empty (no affinity)
        {
            auto t = returns_int();
            auto& p = t.handle().promise();
            BOOST_TEST(!p.get_executor());
        }

        // task<void> executor defaults to empty (no affinity)
        {
            auto t = void_task_basic();
            auto& p = t.handle().promise();
            BOOST_TEST(!p.get_executor());
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

        auto t = void_task_with_async_for_affinity_test();
        t.on(ex);

        while (!t.handle().done())
            t.handle().resume();
        t.await_resume();

        // Work should have been posted through the executor
        BOOST_TEST_GE(ctx.submit_count.load(), 1);
    }

    // on() method tests

    void
    testOnSetsExecutor()
    {
        // Verify on() sets the executor for task<T>
        sync_executor ctx;
        executor ex(ctx);

        auto t = task_with_async_for_affinity_test();
        t.on(ex);

        // Executor should now be set
        BOOST_TEST(static_cast<bool>(t.handle().promise().get_executor()));
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

        while (!t.handle().done())
            t.handle().resume();
        t.await_resume();

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
        while (!t.handle().done())
            t.handle().resume();
        t.await_resume();

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

    // Affinity propagation tests

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

        while (!t.handle().done())
            t.handle().resume();
        t.await_resume();

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

    // Affinity preservation tests

    /** Executor that tracks submissions with an ID.
    */
    struct tracking_executor
    {
        friend struct executor::access;

        int id;
        std::atomic<int> submit_count{0};
        mutable std::vector<int>* submission_log;

        explicit
        tracking_executor(int id_, std::vector<int>* log)
            : id(id_)
            , submission_log(log)
        {
        }

    private:
        struct header
        {
            std::size_t size;
        };

        void*
        allocate(std::size_t size, std::size_t /*align*/)
        {
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
            if (submission_log)
                submission_log->push_back(id);
            w->invoke();
            w->~work();
            deallocate(w, 0, 0);
        }
    };

    static async_op<int>
    async_op_immediate(int value)
    {
        return make_async_op<int>(
            [value](auto cb) {
                cb(value);
            });
    }

    void
    testInheritedAffinityVerification()
    {
        // Test that child tasks actually use inherited affinity
        // by checking that all resumptions go through the parent's executor
        std::vector<int> log;
        tracking_executor ctx(1, &log);
        executor ex(ctx);

        // Chain: outer -> middle -> inner, only outer has .on()
        auto inner = []() -> task<int> {
            co_return co_await async_op_immediate(100);
        };

        auto middle = [inner]() -> task<int> {
            int v = co_await inner();
            co_return v + co_await async_op_immediate(10);
        };

        auto outer = [middle]() -> task<int> {
            int v = co_await middle();
            co_return v + co_await async_op_immediate(1);
        };

        auto t = outer();
        t.on(ex);

        BOOST_TEST_EQ(run_task(t), 111);
        // All three async_ops should have resumed through executor 1
        BOOST_TEST_GE(ctx.submit_count.load(), 3);
        for (int id : log)
            BOOST_TEST_EQ(id, 1);
    }

    void
    testCrossExecutorAsyncOp()
    {
        // Test: async_op "completes" but task resumes on its affinity executor
        // This verifies the dispatcher is correctly used for resumption
        std::vector<int> log;
        tracking_executor ctx1(1, &log);
        tracking_executor ctx2(2, &log);
        executor ex1(ctx1);
        executor ex2(ctx2);

        // Create a task with affinity to ex1
        auto task_with_affinity = []() -> task<int> {
            // This async_op completes inline (simulating completion on "other" context)
            int v = co_await async_op_immediate(42);
            co_return v;
        };

        auto t = task_with_affinity();
        t.on(ex1);

        BOOST_TEST_EQ(run_task(t), 42);
        // Resumption should go through ex1, not ex2
        BOOST_TEST_GE(ctx1.submit_count.load(), 1);
        BOOST_TEST_EQ(ctx2.submit_count.load(), 0);
        // All logged submissions should be to executor 1
        for (int id : log)
            BOOST_TEST_EQ(id, 1);
    }

    void
    testMixedAffinityChain()
    {
        // Test: outer has ex1, inner explicitly has ex2
        // Verify each task uses its own affinity
        std::vector<int> outer_log;
        std::vector<int> inner_log;
        tracking_executor ctx1(1, &outer_log);
        tracking_executor ctx2(2, &inner_log);
        executor ex1(ctx1);
        executor ex2(ctx2);

        // Inner task with explicit affinity to ex2
        auto make_inner = [ex2]() -> task<int> {
            auto inner = []() -> task<int> {
                co_return co_await async_op_immediate(100);
            };
            return inner().on(ex2);
        };

        // Outer task with affinity to ex1
        auto outer = [make_inner]() -> task<int> {
            int v = co_await make_inner();
            // This await should use ex1 (outer's affinity)
            v += co_await async_op_immediate(1);
            co_return v;
        };

        auto t = outer();
        t.on(ex1);

        BOOST_TEST_EQ(run_task(t), 101);
        // Inner's async should use ex2
        BOOST_TEST_GE(ctx2.submit_count.load(), 1);
        // Outer's async should use ex1
        BOOST_TEST_GE(ctx1.submit_count.load(), 1);
    }

    void
    testAffinityPreservedAcrossMultipleAwaits()
    {
        // Test that affinity is preserved across multiple co_await expressions
        std::vector<int> log;
        tracking_executor ctx(1, &log);
        executor ex(ctx);

        auto multi_await = []() -> task<int> {
            int sum = 0;
            sum += co_await async_op_immediate(1);
            sum += co_await async_op_immediate(2);
            sum += co_await async_op_immediate(3);
            sum += co_await async_op_immediate(4);
            sum += co_await async_op_immediate(5);
            co_return sum;
        };

        auto t = multi_await();
        t.on(ex);

        BOOST_TEST_EQ(run_task(t), 15);
        // All 5 awaits should use the same executor
        BOOST_TEST_EQ(ctx.submit_count.load(), 5);
        BOOST_TEST_EQ(log.size(), 5u);
        for (int id : log)
            BOOST_TEST_EQ(id, 1);
    }

    void
    testAffinityWithNestedVoidTasks()
    {
        // Test affinity propagation through void task nesting
        std::vector<int> log;
        tracking_executor ctx(1, &log);
        executor ex(ctx);

        std::atomic<int> counter{0};

        auto leaf = [&counter]() -> task<void> {
            co_await async_op_immediate(0);
            ++counter;
            co_return;
        };

        auto branch = [leaf, &counter]() -> task<void> {
            co_await leaf();
            co_await async_op_immediate(0);
            ++counter;
            co_return;
        };

        auto root = [branch, &counter]() -> task<void> {
            co_await branch();
            co_await async_op_immediate(0);
            ++counter;
            co_return;
        };

        auto t = root();
        t.on(ex);

        while (!t.handle().done())
            t.handle().resume();
        t.await_resume();

        BOOST_TEST_EQ(counter.load(), 3);
        // All async_ops should dispatch through executor
        BOOST_TEST_GE(ctx.submit_count.load(), 3);
        for (int id : log)
            BOOST_TEST_EQ(id, 1);
    }

    void
    testFinalSuspendUsesDispatcher()
    {
        // Test that when child task completes, it resumes parent via dispatcher
        std::vector<int> log;
        tracking_executor ctx(1, &log);
        executor ex(ctx);

        // Simple child that just returns a value
        auto child = []() -> task<int> {
            co_return 42;
        };

        // Parent awaits child, then does work
        auto parent = [child]() -> task<int> {
            int v = co_await child();  // child's final_suspend should use dispatcher
            co_return v + 1;
        };

        auto t = parent();
        t.on(ex);

        BOOST_TEST_EQ(run_task(t), 43);
        // Child's completion should dispatch through executor
        BOOST_TEST_GE(ctx.submit_count.load(), 1);
    }

    // spawn() tests

    void
    testSpawnValueTask()
    {
        sync_executor ctx;
        executor ex(ctx);
        std::optional<system::result<int, std::exception_ptr>> received;

        auto compute = []() -> task<int> {
            co_return 42;
        };

        spawn(ex, compute(), [&](auto result) {
            received = result;
        });

        BOOST_TEST(received.has_value());
        BOOST_TEST(received->has_value());
        BOOST_TEST_EQ(*(*received), 42);
        BOOST_TEST_GE(ctx.submit_count.load(), 1);
    }

    void
    testSpawnVoidTask()
    {
        sync_executor ctx;
        executor ex(ctx);
        bool task_done = false;
        std::optional<system::result<void, std::exception_ptr>> received;

        auto do_work = [&task_done]() -> task<void> {
            task_done = true;
            co_return;
        };

        spawn(ex, do_work(), [&](auto result) {
            received = result;
        });

        BOOST_TEST(received.has_value());
        BOOST_TEST(received->has_value());
        BOOST_TEST(task_done);
        BOOST_TEST_GE(ctx.submit_count.load(), 1);
    }

    void
    testSpawnTaskWithException()
    {
        sync_executor ctx;
        executor ex(ctx);
        std::optional<system::result<int, std::exception_ptr>> received;

        auto throwing_task = []() -> task<int> {
            throw_test_exception("spawn test");
            co_return 0;
        };

        spawn(ex, throwing_task(), [&](auto result) {
            received = result;
        });

        BOOST_TEST(received.has_value());
        BOOST_TEST(received->has_error());
        bool caught = false;
        try { std::rethrow_exception(received->error()); }
        catch (test_exception const&) { caught = true; }
        BOOST_TEST(caught);
    }

    void
    testSpawnVoidTaskWithException()
    {
        sync_executor ctx;
        executor ex(ctx);
        std::optional<system::result<void, std::exception_ptr>> received;

        auto throwing_void_task = []() -> task<void> {
            throw_test_exception("void spawn exception");
            co_return;
        };

        spawn(ex, throwing_void_task(), [&](auto result) {
            received = result;
        });

        BOOST_TEST(received.has_value());
        BOOST_TEST(received->has_error());
        bool caught = false;
        try { std::rethrow_exception(received->error()); }
        catch (test_exception const&) { caught = true; }
        BOOST_TEST(caught);
    }

    void
    testSpawnWithNestedAwaits()
    {
        sync_executor ctx;
        executor ex(ctx);
        std::optional<system::result<int, std::exception_ptr>> received;

        auto inner = []() -> task<int> {
            co_return 10;
        };

        auto outer = [inner]() -> task<int> {
            int a = co_await inner();
            int b = co_await inner();
            co_return a + b;
        };

        spawn(ex, outer(), [&](auto result) {
            received = result;
        });

        BOOST_TEST(received.has_value());
        BOOST_TEST(received->has_value());
        BOOST_TEST_EQ(*(*received), 20);
    }

    void
    testSpawnWithAsyncOp()
    {
        sync_executor ctx;
        executor ex(ctx);
        std::optional<system::result<int, std::exception_ptr>> received;

        auto task_with_async = []() -> task<int> {
            int v = co_await async_op_immediate(100);
            co_return v + 1;
        };

        spawn(ex, task_with_async(), [&](auto result) {
            received = result;
        });

        BOOST_TEST(received.has_value());
        BOOST_TEST(received->has_value());
        BOOST_TEST_EQ(*(*received), 101);
        BOOST_TEST_GE(ctx.submit_count.load(), 1);
    }

    void
    testSpawnAffinityPropagation()
    {
        std::vector<int> log;
        tracking_executor ctx(1, &log);
        executor ex(ctx);
        std::optional<system::result<int, std::exception_ptr>> received;

        auto inner = []() -> task<int> {
            co_return co_await async_op_immediate(50);
        };

        auto outer = [inner]() -> task<int> {
            int v = co_await inner();
            v += co_await async_op_immediate(5);
            co_return v;
        };

        spawn(ex, outer(), [&](auto result) {
            received = result;
        });

        BOOST_TEST(received.has_value());
        BOOST_TEST(received->has_value());
        BOOST_TEST_EQ(*(*received), 55);
        BOOST_TEST_GE(ctx.submit_count.load(), 2);
        for (int id : log)
            BOOST_TEST_EQ(id, 1);
    }

    void
    testSpawnChained()
    {
        sync_executor ctx;
        executor ex(ctx);
        int sum = 0;

        auto task1 = []() -> task<int> { co_return 1; };
        auto task2 = []() -> task<int> { co_return 2; };
        auto task3 = []() -> task<int> { co_return 3; };

        spawn(ex, task1(), [&](auto r) { if (r) sum += *r; });
        spawn(ex, task2(), [&](auto r) { if (r) sum += *r; });
        spawn(ex, task3(), [&](auto r) { if (r) sum += *r; });

        BOOST_TEST_EQ(sum, 6);
    }

    void
    testSpawnResultErrorAccess()
    {
        sync_executor ctx;
        executor ex(ctx);
        std::optional<system::result<int, std::exception_ptr>> received;

        auto failing = []() -> task<int> {
            throw std::runtime_error("specific error");
            co_return 0;
        };

        spawn(ex, failing(), [&](auto result) {
            received = result;
        });

        BOOST_TEST(received.has_value());
        BOOST_TEST(!received->has_value());
        BOOST_TEST(received->has_error());
        BOOST_TEST(received->error() != nullptr);

        bool caught = false;
        try
        {
            std::rethrow_exception(received->error());
        }
        catch (std::runtime_error const& e)
        {
            BOOST_TEST(std::string(e.what()) == "specific error");
            caught = true;
        }
        BOOST_TEST(caught);
    }

    void
    testSpawnDeeplyNested()
    {
        sync_executor ctx;
        executor ex(ctx);
        std::optional<system::result<int, std::exception_ptr>> received;

        auto level3 = []() -> task<int> {
            co_return co_await async_op_immediate(1);
        };

        auto level2 = [level3]() -> task<int> {
            int v = co_await level3();
            co_return v + co_await async_op_immediate(10);
        };

        auto level1 = [level2]() -> task<int> {
            int v = co_await level2();
            co_return v + co_await async_op_immediate(100);
        };

        spawn(ex, level1(), [&](auto result) {
            received = result;
        });

        BOOST_TEST(received.has_value());
        BOOST_TEST(received->has_value());
        BOOST_TEST_EQ(*(*received), 111);
        BOOST_TEST_GE(ctx.submit_count.load(), 3);
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

        // affinity preservation tests
        testInheritedAffinityVerification();
        testCrossExecutorAsyncOp();
        testMixedAffinityChain();
        testAffinityPreservedAcrossMultipleAwaits();
        testAffinityWithNestedVoidTasks();
        testFinalSuspendUsesDispatcher();

        // spawn() function tests
        testSpawnValueTask();
        testSpawnVoidTask();
        testSpawnTaskWithException();
        testSpawnVoidTaskWithException();
        testSpawnWithNestedAwaits();
        testSpawnWithAsyncOp();
        testSpawnAffinityPropagation();
        testSpawnChained();
        testSpawnResultErrorAccess();
        testSpawnDeeplyNested();
        testGccUninitialized();
    }

    // GCC 12+ -Wmaybe-uninitialized false positive tests
    // https://github.com/boostorg/variant2/issues/XXX
    // These attempt to reproduce the warning without coroutines.
    void
    testGccUninitialized()
    {
        using result_void = system::result<void, std::exception_ptr>;
        using result_string = system::result<std::string, std::exception_ptr>;

        // Test 1: Simple copy construction
        {
            result_void r1;
            result_void r2(r1);
            (void)r2;
        }

        // Test 2: Copy assignment
        {
            result_void r1;
            result_void r2;
            r2 = r1;
            (void)r2;
        }

        // Test 3: std::optional assignment (matches spawn pattern)
        {
            std::optional<result_void> opt;
            opt = result_void{};
            (void)opt;
        }

        // Test 4: Pass to function via copy
        {
            auto fn = [](result_void r) { (void)r; };
            fn(result_void{});
        }

        // Test 5: Lambda capture + optional (closest to spawn)
        {
            auto fn = [](result_void r) {
                std::optional<result_void> opt;
                opt = r;
                return opt.has_value();
            };
            (void)fn(result_void{});
        }

        // Test 6: Non-void result with string (triggers string warning)
        {
            result_string r1;
            result_string r2(r1);
            (void)r2;
        }

        // Test 7: Assign exception to result holding value
        {
            result_string r1{"hello"};
            r1 = std::make_exception_ptr(std::runtime_error("test"));
            (void)r1;
        }

        // Test 8: Optional with string result
        {
            std::optional<result_string> opt;
            opt = result_string{};
            (void)opt;
        }

#ifdef BOOST_CAPY_HAS_CORO
        // Minimal fire-and-forget coroutine for testing
        struct fire_and_forget
        {
            struct promise_type
            {
                fire_and_forget get_return_object() { return {}; }
                std::suspend_never initial_suspend() noexcept { return {}; }
                std::suspend_never final_suspend() noexcept { return {}; }
                void return_void() {}
                void unhandled_exception() { std::terminate(); }
            };
        };

        // Test 9: Coroutine returning result (mimics spawn)
        {
            auto coro = []() -> fire_and_forget {
                result_void r{};
                (void)r;
                co_return;
            };
            coro();
        }

        // Test 10: Coroutine with handler call (closest to actual spawn)
        {
            std::optional<result_void> received;
            auto handler = [&](result_void r) {
                received = r;
            };
            auto coro = [&]() -> fire_and_forget {
                handler(result_void{});
                co_return;
            };
            coro();
            (void)received;
        }

        // Test 11: Coroutine with try/catch like spawn
        {
            std::optional<result_void> received;
            auto handler = [&](result_void r) {
                received = r;
            };
            auto coro = [&]() -> fire_and_forget {
                try
                {
                    handler(result_void{});
                }
                catch (...)
                {
                    handler(result_void{std::current_exception()});
                }
                co_return;
            };
            coro();
            (void)received;
        }

        // Test 12: Coroutine with string result
        {
            std::optional<result_string> received;
            auto handler = [&](result_string r) {
                received = r;
            };
            auto coro = [&]() -> fire_and_forget {
                try
                {
                    handler(result_string{"test"});
                }
                catch (...)
                {
                    handler(result_string{std::current_exception()});
                }
                co_return;
            };
            coro();
            (void)received;
        }
#endif
    }
};

TEST_SUITE(
    task_test,
    "boost.capy.task");

} // capy
} // boost

#endif
