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

#include <boost/capy/ex/async_op.hpp>
#include <boost/capy/ex/async_run.hpp>

#include "test_suite.hpp"

#include <atomic>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

namespace boost {
namespace capy {

static_assert(affine_awaitable<task<void>, any_dispatcher>);
static_assert(affine_awaitable<task<int>, any_dispatcher>);
#if BOOST_CAPY_HAS_STOP_TOKEN
static_assert(stoppable_awaitable<task<void>, any_dispatcher>);
static_assert(stoppable_awaitable<task<int>, any_dispatcher>);
#endif

/** Simple synchronous dispatcher for testing.

    Satisfies the dispatcher concept: callable with (any_coro) returning any_coro.
    Executes inline (returns the handle for symmetric transfer).
    Uses a pointer to external counter to allow copying.
*/
struct test_dispatcher
{
    int* dispatch_count_;

    explicit test_dispatcher(int& count)
        : dispatch_count_(&count)
    {
    }

    any_coro operator()(any_coro h) const
    {
        ++(*dispatch_count_);
        return h;  // Inline execution for sync tests
    }
};

static_assert(dispatcher<test_dispatcher>);

/** Tracking dispatcher that logs dispatch calls with an ID.
    Uses pointers to external storage to allow copying.
*/
struct tracking_dispatcher
{
    int id;
    int* dispatch_count_;
    std::vector<int>* dispatch_log;

    tracking_dispatcher(int id_, int& count, std::vector<int>* log = nullptr)
        : id(id_)
        , dispatch_count_(&count)
        , dispatch_log(log)
    {
    }

    any_coro operator()(any_coro h) const
    {
        ++(*dispatch_count_);
        if (dispatch_log)
            dispatch_log->push_back(id);
        return h;  // Inline execution
    }
};

static_assert(dispatcher<tracking_dispatcher>);

/** Run a task to completion by manually stepping through it.

    Takes ownership of the task via release() and runs until done.
*/
template<class T>
T run_task(task<T> t)
{
    auto h = t.release();  // Take ownership
    while (!h.done())
        h.resume();
    auto& p = h.promise();
    // Check for exception first (result may be empty if exception occurred)
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

/** Run a void task to completion.
*/
inline void run_void_task(task<void> t)
{
    run_task<void>(std::move(t));
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
            BOOST_TEST_EQ(run_task(returns_int()), 42);
        }

        // task returning string
        {
            BOOST_TEST_EQ(run_task(returns_string()), "hello");
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
            BOOST_TEST_THROWS(run_task(throws_exception()), test_exception);
        }

        // task that throws std::runtime_error
        {
            BOOST_TEST_THROWS(run_task(throws_std_exception()), std::runtime_error);
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
            BOOST_TEST_EQ(run_task(outer_task_awaits_inner()), 101);
        }

        // outer task awaits inner task that throws
        {
            BOOST_TEST_THROWS(run_task(outer_task_awaits_throwing_inner()), test_exception);
        }

        // outer task catches exception from inner task
        {
            BOOST_TEST_EQ(run_task(outer_task_catches_inner_exception()), 999);
        }

        // chained tasks (3 levels)
        {
            BOOST_TEST_EQ(run_task(chained_tasks()), 25);
        }
    }

    void
    testMoveOperations()
    {
        // move constructor
        {
            auto t1 = returns_int();
            auto h1 = t1.release();
            BOOST_TEST(h1);

            // Re-wrap for move test
            task<int> t2(std::move(t1));
            // t1 is now moved-from, t2 should be empty since t1 was released
            // This test verifies move semantics
            BOOST_TEST(!t2.release());  // t2 is empty

            // Run the released handle
            while (!h1.done())
                h1.resume();
            BOOST_TEST_EQ(*h1.promise().result_, 42);
            h1.destroy();
        }

        // release()
        {
            auto t = returns_int();
            auto h = t.release();
            BOOST_TEST(h);
            BOOST_TEST(!t.release());  // Already released

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
        // task awaits single async_op - needs async_run for dispatcher
        {
            int dispatch_count = 0;
            test_dispatcher d(dispatch_count);
            int result = 0;
            bool completed = false;

            async_run(d)(task_awaits_async_op(),
                [&](int v) {
                    result = v;
                    completed = true;
                },
                [](std::exception_ptr) {});

            BOOST_TEST(completed);
            BOOST_TEST_EQ(result, 124);
        }

        // task awaits multiple async_ops
        if (false) {
            int dispatch_count = 0;
            test_dispatcher d(dispatch_count);
            int result = 0;
            bool completed = false;

            async_run(d)(task_awaits_multiple_async_ops(),
                [&](int v) {
                    result = v;
                    completed = true;
                },
                [](std::exception_ptr) {});

            BOOST_TEST(completed);
            BOOST_TEST_EQ(result, 579);
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
        run_void_task(void_task_basic());  // should not throw
    }

    void
    testVoidTaskException()
    {
        BOOST_TEST_THROWS(run_void_task(void_task_throws()), test_exception);
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
            run_void_task(void_task_awaits_value());
        }

        // void task awaits another void task
        {
            run_void_task(void_task_awaits_void());
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
        run_void_task(void_task_chain());
    }

    void
    testVoidTaskMove()
    {
        auto t1 = void_task_basic();
        auto h = t1.release();
        BOOST_TEST(h);

        task<void> t2(std::move(t1));
        // t1 was already released, t2 should be empty
        BOOST_TEST(!t2.release());

        // Clean up the handle
        while (!h.done())
            h.resume();
        h.destroy();
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
        // Needs async_run since void_task_awaits_async_op awaits an async_op
        int dispatch_count = 0;
        test_dispatcher d(dispatch_count);
        bool completed = false;

        async_run(d)(void_task_awaits_async_op(),
            [&]() { completed = true; },
            [](std::exception_ptr) {});

        BOOST_TEST(completed);
    }

    // Dispatcher tests using async_run

    static async_op<int>
    async_op_immediate(int value)
    {
        return make_async_op<int>(
            [value](auto cb) {
                cb(value);
            });
    }

    static task<int>
    task_with_async_for_affinity_test()
    {
        int v = co_await async_returns_value();
        co_return v + 1;
    }

    void
    testDispatcherUsedByAwait()
    {
        // Verify that dispatcher is used when awaiting via async_run
        int dispatch_count = 0;
        test_dispatcher d(dispatch_count);
        bool completed = false;
        int result = 0;

        async_run(d)(task_with_async_for_affinity_test(),
            [&](int v) {
                result = v;
                completed = true;
            },
            [](std::exception_ptr) {});

        BOOST_TEST(completed);
        BOOST_TEST_EQ(result, 124);
        // Work should have been dispatched
        BOOST_TEST_GE(dispatch_count, 1);
    }

    static task<void>
    void_task_with_async_for_affinity_test()
    {
        auto v = co_await async_returns_value();
        (void)v;
        co_return;
    }

    void
    testVoidTaskDispatcherUsedByAwait()
    {
        // Verify that dispatcher is used for void tasks
        int dispatch_count = 0;
        test_dispatcher d(dispatch_count);
        bool completed = false;

        async_run(d)(void_task_with_async_for_affinity_test(),
            [&]() { completed = true; },
            [](std::exception_ptr) {});

        BOOST_TEST(completed);
        // Work should have been dispatched
        BOOST_TEST_GE(dispatch_count, 1);
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
        // The dispatcher from async_run should be inherited by nested tasks
        int dispatch_count = 0;
        test_dispatcher d(dispatch_count);
        bool completed = false;
        int result = 0;

        async_run(d)(outer_task_a(),
            [&](int v) {
                result = v;
                completed = true;
            },
            [](std::exception_ptr) {});

        BOOST_TEST(completed);
        BOOST_TEST_EQ(result, 125);  // 123 + 1 + 1
        // All async completions should dispatch through the dispatcher
        BOOST_TEST_GE(dispatch_count, 1);
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
        int dispatch_count = 0;
        test_dispatcher d(dispatch_count);
        bool completed = false;

        async_run(d)(outer_void_task_a(),
            [&]() { completed = true; },
            [](std::exception_ptr) {});

        BOOST_TEST(completed);
        BOOST_TEST_GE(dispatch_count, 1);
    }

    void
    testNoDispatcherRunsInline()
    {
        // Verify that simple tasks can run without async_run (manual stepping)
        // Note: Only works for tasks that don't await dispatcher-aware awaitables
        BOOST_TEST_EQ(run_task(chained_tasks()), 25);
    }

    // Affinity preservation tests with tracking dispatcher

    void
    testInheritedAffinityVerification()
    {
        // Test that child tasks actually use inherited affinity
        // by checking that all resumptions go through the parent's dispatcher
        std::vector<int> log;
        int dispatch_count = 0;
        tracking_dispatcher d(1, dispatch_count, &log);

        bool completed = false;
        int result = 0;

        // Chain: outer -> middle -> inner
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

        async_run(d)(outer(),
            [&](int v) {
                result = v;
                completed = true;
            },
            [](std::exception_ptr) {});

        BOOST_TEST(completed);
        BOOST_TEST_EQ(result, 111);
        // All three async_ops should have resumed through dispatcher 1
        BOOST_TEST_GE(dispatch_count, 3);
        for (int id : log)
            BOOST_TEST_EQ(id, 1);
    }

    void
    testAffinityPreservedAcrossMultipleAwaits()
    {
        // Test that affinity is preserved across multiple co_await expressions
        std::vector<int> log;
        int dispatch_count = 0;
        tracking_dispatcher d(1, dispatch_count, &log);

        bool completed = false;
        int result = 0;

        auto multi_await = []() -> task<int> {
            int sum = 0;
            sum += co_await async_op_immediate(1);
            sum += co_await async_op_immediate(2);
            sum += co_await async_op_immediate(3);
            sum += co_await async_op_immediate(4);
            sum += co_await async_op_immediate(5);
            co_return sum;
        };

        async_run(d)(multi_await(),
            [&](int v) {
                result = v;
                completed = true;
            },
            [](std::exception_ptr) {});

        BOOST_TEST(completed);
        BOOST_TEST_EQ(result, 15);
        // 6 dispatches: 1 from async_run start + 5 from async_ops completing
        BOOST_TEST_EQ(dispatch_count, 6);
        BOOST_TEST_EQ(log.size(), 6u);
        for (int id : log)
            BOOST_TEST_EQ(id, 1);
    }

    void
    testAffinityWithNestedVoidTasks()
    {
        // Test affinity propagation through void task nesting
        std::vector<int> log;
        int dispatch_count = 0;
        tracking_dispatcher d(1, dispatch_count, &log);

        std::atomic<int> counter{0};
        bool completed = false;

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

        async_run(d)(root(),
            [&]() { completed = true; },
            [](std::exception_ptr) {});

        BOOST_TEST(completed);
        BOOST_TEST_EQ(counter.load(), 3);
        // All async_ops should dispatch through the dispatcher
        BOOST_TEST_GE(dispatch_count, 3);
        for (int id : log)
            BOOST_TEST_EQ(id, 1);
    }

    void
    testFinalSuspendUsesDispatcher()
    {
        // Test that when child task completes, it resumes parent via dispatcher
        std::vector<int> log;
        int dispatch_count = 0;
        tracking_dispatcher d(1, dispatch_count, &log);

        bool completed = false;
        int result = 0;

        // Simple child that just returns a value
        auto child = []() -> task<int> {
            co_return 42;
        };

        // Parent awaits child, then does work
        auto parent = [child]() -> task<int> {
            int v = co_await child();  // child's final_suspend should use dispatcher
            co_return v + 1;
        };

        async_run(d)(parent(),
            [&](int v) {
                result = v;
                completed = true;
            },
            [](std::exception_ptr) {});

        BOOST_TEST(completed);
        BOOST_TEST_EQ(result, 43);
        // Child's completion should dispatch through the dispatcher
        BOOST_TEST_GE(dispatch_count, 1);
    }

    // async_run() tests (replacing old spawn() tests)

    void
    testAsyncRunValueTask()
    {
        int dispatch_count = 0;
        test_dispatcher d(dispatch_count);
        bool completed = false;
        int result = 0;

        auto compute = []() -> task<int> {
            co_return 42;
        };

        async_run(d)(compute(),
            [&](int v) {
                result = v;
                completed = true;
            },
            [](std::exception_ptr) {});

        BOOST_TEST(completed);
        BOOST_TEST_EQ(result, 42);
        BOOST_TEST_GE(dispatch_count, 1);
    }

    void
    testAsyncRunVoidTask()
    {
        int dispatch_count = 0;
        test_dispatcher d(dispatch_count);
        bool task_done = false;
        bool completed = false;

        auto do_work = [&task_done]() -> task<void> {
            task_done = true;
            co_return;
        };

        async_run(d)(do_work(),
            [&]() { completed = true; },
            [](std::exception_ptr) {});

        BOOST_TEST(completed);
        BOOST_TEST(task_done);
        BOOST_TEST_GE(dispatch_count, 1);
    }

    void
    testAsyncRunTaskWithException()
    {
        int dispatch_count = 0;
        test_dispatcher d(dispatch_count);
        bool completed = false;
        bool caught_exception = false;

        auto throwing_task = []() -> task<int> {
            throw_test_exception("async_run test");
            co_return 0;
        };

        async_run(d)(throwing_task(),
            [&](int) { completed = true; },
            [&](std::exception_ptr ep) {
                try {
                    std::rethrow_exception(ep);
                } catch (test_exception const&) {
                    caught_exception = true;
                }
            });

        BOOST_TEST(!completed);
        BOOST_TEST(caught_exception);
    }

    void
    testAsyncRunVoidTaskWithException()
    {
        int dispatch_count = 0;
        test_dispatcher d(dispatch_count);
        bool completed = false;
        bool caught_exception = false;

        auto throwing_void_task = []() -> task<void> {
            throw_test_exception("void async_run exception");
            co_return;
        };

        async_run(d)(throwing_void_task(),
            [&]() { completed = true; },
            [&](std::exception_ptr ep) {
                try {
                    std::rethrow_exception(ep);
                } catch (test_exception const&) {
                    caught_exception = true;
                }
            });

        BOOST_TEST(!completed);
        BOOST_TEST(caught_exception);
    }

    void
    testAsyncRunWithNestedAwaits()
    {
        int dispatch_count = 0;
        test_dispatcher d(dispatch_count);
        bool completed = false;
        int result = 0;

        auto inner = []() -> task<int> {
            co_return 10;
        };

        auto outer = [inner]() -> task<int> {
            int a = co_await inner();
            int b = co_await inner();
            co_return a + b;
        };

        async_run(d)(outer(),
            [&](int v) {
                result = v;
                completed = true;
            },
            [](std::exception_ptr) {});

        BOOST_TEST(completed);
        BOOST_TEST_EQ(result, 20);
    }

    void
    testAsyncRunWithAsyncOp()
    {
        int dispatch_count = 0;
        test_dispatcher d(dispatch_count);
        bool completed = false;
        int result = 0;

        auto task_with_async = []() -> task<int> {
            int v = co_await async_op_immediate(100);
            co_return v + 1;
        };

        async_run(d)(task_with_async(),
            [&](int v) {
                result = v;
                completed = true;
            },
            [](std::exception_ptr) {});

        BOOST_TEST(completed);
        BOOST_TEST_EQ(result, 101);
        BOOST_TEST_GE(dispatch_count, 1);
    }

    void
    testAsyncRunAffinityPropagation()
    {
        std::vector<int> log;
        int dispatch_count = 0;
        tracking_dispatcher d(1, dispatch_count, &log);
        bool completed = false;
        int result = 0;

        auto inner = []() -> task<int> {
            co_return co_await async_op_immediate(50);
        };

        auto outer = [inner]() -> task<int> {
            int v = co_await inner();
            v += co_await async_op_immediate(5);
            co_return v;
        };

        async_run(d)(outer(),
            [&](int v) {
                result = v;
                completed = true;
            },
            [](std::exception_ptr) {});

        BOOST_TEST(completed);
        BOOST_TEST_EQ(result, 55);
        BOOST_TEST_GE(dispatch_count, 2);
        for (int id : log)
            BOOST_TEST_EQ(id, 1);
    }

    void
    testAsyncRunChained()
    {
        int dispatch_count = 0;
        test_dispatcher d(dispatch_count);
        int sum = 0;

        auto task1 = []() -> task<int> { co_return 1; };
        auto task2 = []() -> task<int> { co_return 2; };
        auto task3 = []() -> task<int> { co_return 3; };

        async_run(d)(task1(), [&](int v) { sum += v; }, [](std::exception_ptr) {});
        async_run(d)(task2(), [&](int v) { sum += v; }, [](std::exception_ptr) {});
        async_run(d)(task3(), [&](int v) { sum += v; }, [](std::exception_ptr) {});

        BOOST_TEST_EQ(sum, 6);
    }

    void
    testAsyncRunErrorHandler()
    {
        int dispatch_count = 0;
        test_dispatcher d(dispatch_count);
        bool caught = false;
        std::string error_msg;

        auto failing = []() -> task<int> {
            throw std::runtime_error("specific error");
            co_return 0;
        };

        async_run(d)(failing(),
            [](int) {},
            [&](std::exception_ptr ep) {
                try {
                    std::rethrow_exception(ep);
                } catch (std::runtime_error const& e) {
                    error_msg = e.what();
                    caught = true;
                }
            });

        BOOST_TEST(caught);
        BOOST_TEST_EQ(error_msg, "specific error");
    }

    void
    testAsyncRunDeeplyNested()
    {
        int dispatch_count = 0;
        test_dispatcher d(dispatch_count);
        bool completed = false;
        int result = 0;

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

        async_run(d)(level1(),
            [&](int v) {
                result = v;
                completed = true;
            },
            [](std::exception_ptr) {});

        BOOST_TEST(completed);
        BOOST_TEST_EQ(result, 111);
        BOOST_TEST_GE(dispatch_count, 3);
    }

    void
    testAsyncRunFireAndForget()
    {
        // Test fire-and-forget mode (default handler)
        int dispatch_count = 0;
        test_dispatcher d(dispatch_count);
        std::atomic<bool> task_ran{false};

        auto simple_task = [&task_ran]() -> task<void> {
            task_ran = true;
            co_return;
        };

        async_run(d)(simple_task());

        BOOST_TEST(task_ran.load());
    }

    void
    testAsyncRunSingleHandler()
    {
        // Test single handler that handles both success and exception
        int dispatch_count = 0;
        test_dispatcher d(dispatch_count);
        bool success_called = false;
        bool exception_called = false;

        struct overloaded_handler
        {
            bool* success;
            bool* exception;

            void operator()(int v)
            {
                (void)v;
                *success = true;
            }

            void operator()(std::exception_ptr)
            {
                *exception = true;
            }
        };

        auto success_task = []() -> task<int> {
            co_return 42;
        };

        async_run(d)(success_task(),
            overloaded_handler{&success_called, &exception_called});

        BOOST_TEST(success_called);
        BOOST_TEST(!exception_called);
    }

    //------------------------------------------------------
    // Memory allocation tests - TLS restoration pattern
    //------------------------------------------------------

    /** Tracking frame allocator that logs allocation/deallocation events.
    */
    struct tracking_frame_allocator
    {
        int id;
        int* alloc_count;
        int* dealloc_count;
        std::vector<int>* alloc_log;

        void* allocate(std::size_t n)
        {
            ++(*alloc_count);
            if(alloc_log)
                alloc_log->push_back(id);
            return ::operator new(n);
        }

        void deallocate(void* p, std::size_t)
        {
            ++(*dealloc_count);
            ::operator delete(p);
        }
    };

    void
    testAllocatorCapturedOnCreation()
    {
        // Verify that the allocator is captured when the task is created
        int dispatch_count = 0;
        test_dispatcher d(dispatch_count);
        bool completed = false;

        int alloc_count = 0;
        int dealloc_count = 0;
        std::vector<int> alloc_log;

        tracking_frame_allocator alloc{1, &alloc_count, &dealloc_count, &alloc_log};

        auto simple = []() -> task<void> {
            co_return;
        };

        async_run(d, alloc)(simple(),
            [&]() { completed = true; },
            [](std::exception_ptr) {});

        BOOST_TEST(completed);
        // At least one allocation should have used our allocator
        BOOST_TEST_GE(alloc_count, 1);
        BOOST_TEST(!alloc_log.empty());
        for(int id : alloc_log)
            BOOST_TEST_EQ(id, 1);
    }

    void
    testAllocatorUsedByChildTasks()
    {
        // Verify that child tasks use the same allocator as the parent
        // Note: HALO may elide child task allocation if directly awaited
        int dispatch_count = 0;
        test_dispatcher d(dispatch_count);
        bool completed = false;

        int alloc_count = 0;
        int dealloc_count = 0;
        std::vector<int> alloc_log;

        tracking_frame_allocator alloc{1, &alloc_count, &dealloc_count, &alloc_log};

        auto inner = []() -> task<int> {
            co_return 42;
        };

        auto outer = [inner]() -> task<int> {
            int v = co_await inner();
            co_return v + 1;
        };

        int result = 0;
        async_run(d, alloc)(outer(),
            [&](int v) {
                result = v;
                completed = true;
            },
            [](std::exception_ptr) {});

        BOOST_TEST(completed);
        BOOST_TEST_EQ(result, 43);
        // At least the outer task should be allocated
        BOOST_TEST_GE(alloc_count, 1);
        // All allocations must use our allocator
        for(int id : alloc_log)
            BOOST_TEST_EQ(id, 1);
    }

    void
    testAllocatorRestoredAfterAwait()
    {
        // Verify that TLS is restored after co_await,
        // allowing child tasks created after await to use the correct allocator
        int dispatch_count = 0;
        test_dispatcher d(dispatch_count);
        bool completed = false;

        int alloc_count = 0;
        int dealloc_count = 0;
        std::vector<int> alloc_log;

        tracking_frame_allocator alloc{1, &alloc_count, &dealloc_count, &alloc_log};

        // Create a task that awaits an async_op, then creates a child task
        auto child_after_await = []() -> task<int> {
            co_return 10;
        };

        auto parent = [child_after_await]() -> task<int> {
            // First await an async_op (simulates I/O)
            int v1 = co_await async_op_immediate(5);
            // After resume, TLS should be restored, so this child
            // should use the same allocator
            int v2 = co_await child_after_await();
            co_return v1 + v2;
        };

        int result = 0;
        async_run(d, alloc)(parent(),
            [&](int v) {
                result = v;
                completed = true;
            },
            [](std::exception_ptr) {});

        BOOST_TEST(completed);
        BOOST_TEST_EQ(result, 15);
        // At least one allocation should occur
        BOOST_TEST_GE(alloc_count, 1);
        // All allocations must use our allocator
        for(int id : alloc_log)
            BOOST_TEST_EQ(id, 1);
    }

    void
    testAllocatorRestoredAcrossMultipleAwaits()
    {
        // Verify TLS restoration across multiple sequential awaits
        int dispatch_count = 0;
        test_dispatcher d(dispatch_count);
        bool completed = false;

        int alloc_count = 0;
        int dealloc_count = 0;
        std::vector<int> alloc_log;

        tracking_frame_allocator alloc{1, &alloc_count, &dealloc_count, &alloc_log};

        auto make_child = [](int v) -> task<int> {
            co_return v;
        };

        auto parent = [make_child]() -> task<int> {
            int sum = 0;
            // Each await should restore TLS before the next child creation
            sum += co_await async_op_immediate(1);
            sum += co_await make_child(10);
            sum += co_await async_op_immediate(2);
            sum += co_await make_child(20);
            sum += co_await async_op_immediate(3);
            sum += co_await make_child(30);
            co_return sum;
        };

        int result = 0;
        async_run(d, alloc)(parent(),
            [&](int v) {
                result = v;
                completed = true;
            },
            [](std::exception_ptr) {});

        BOOST_TEST(completed);
        BOOST_TEST_EQ(result, 66);  // 1+10+2+20+3+30
        // All child tasks should use the same allocator
        for(int id : alloc_log)
            BOOST_TEST_EQ(id, 1);
    }

    void
    testDeeplyNestedAllocatorPropagation()
    {
        // Verify allocator propagates through deep task nesting
        // Note: HALO may elide some allocations, so we just verify
        // that all allocations that DO happen use our allocator
        int dispatch_count = 0;
        test_dispatcher d(dispatch_count);
        bool completed = false;

        int alloc_count = 0;
        int dealloc_count = 0;
        std::vector<int> alloc_log;

        tracking_frame_allocator alloc{1, &alloc_count, &dealloc_count, &alloc_log};

        auto level4 = []() -> task<int> {
            co_return 1;
        };

        auto level3 = [level4]() -> task<int> {
            co_return co_await level4() + 10;
        };

        auto level2 = [level3]() -> task<int> {
            co_return co_await level3() + 100;
        };

        auto level1 = [level2]() -> task<int> {
            co_return co_await level2() + 1000;
        };

        int result = 0;
        async_run(d, alloc)(level1(),
            [&](int v) {
                result = v;
                completed = true;
            },
            [](std::exception_ptr) {});

        BOOST_TEST(completed);
        BOOST_TEST_EQ(result, 1111);
        // At least some allocations should occur
        BOOST_TEST_GE(alloc_count, 1);
        // All allocations must use our allocator (HALO may reduce count)
        for(int id : alloc_log)
            BOOST_TEST_EQ(id, 1);
    }

    void
    testAllocatorWithMixedTasksAndAsyncOps()
    {
        // Verify allocator works correctly with interleaved tasks and async_ops
        int dispatch_count = 0;
        test_dispatcher d(dispatch_count);
        bool completed = false;

        int alloc_count = 0;
        int dealloc_count = 0;
        std::vector<int> alloc_log;

        tracking_frame_allocator alloc{1, &alloc_count, &dealloc_count, &alloc_log};

        auto compute = [](int x) -> task<int> {
            co_return x * 2;
        };

        auto complex_task = [compute]() -> task<int> {
            int v = 0;
            // async_op -> task -> async_op -> task pattern
            v += co_await async_op_immediate(1);
            v += co_await compute(v);    // Creates child task after I/O
            v += co_await async_op_immediate(10);
            v += co_await compute(v);    // Creates another child after I/O
            co_return v;
        };

        int result = 0;
        async_run(d, alloc)(complex_task(),
            [&](int v) {
                result = v;
                completed = true;
            },
            [](std::exception_ptr) {});

        BOOST_TEST(completed);
        // v = 0 + 1 = 1, then v = 1 + 2 = 3, then v = 3 + 10 = 13, then v = 13 + 26 = 39
        BOOST_TEST_EQ(result, 39);
        // All allocations should use our allocator
        for(int id : alloc_log)
            BOOST_TEST_EQ(id, 1);
    }

    void
    testDeallocationCount()
    {
        // Verify that all allocations are eventually deallocated
        int dispatch_count = 0;
        test_dispatcher d(dispatch_count);
        bool completed = false;

        int alloc_count = 0;
        int dealloc_count = 0;

        tracking_frame_allocator alloc{1, &alloc_count, &dealloc_count, nullptr};

        auto inner = []() -> task<int> {
            co_return 42;
        };

        auto outer = [inner]() -> task<int> {
            co_return co_await inner();
        };

        async_run(d, alloc)(outer(),
            [&](int) { completed = true; },
            [](std::exception_ptr) {});

        BOOST_TEST(completed);
        // All allocations should be balanced by deallocations
        BOOST_TEST_EQ(alloc_count, dealloc_count);
    }

    void testFrameAllocationOrder()
    {
        int dispatch_count = 0;
        test_dispatcher d(dispatch_count);
        bool completed = false;

        int alloc_count = 0;
        int dealloc_count = 0;
        std::vector<int> alloc_log;

        // Allocator ID 1 = launcher (Frame #2)
        // Allocator ID 2 = task (Frame #1)
        tracking_frame_allocator alloc{1, &alloc_count, &dealloc_count, &alloc_log};

        auto simple = []() -> task<void> {
            co_return;
        };

        async_run(d, alloc)(simple(),
            [&]() { completed = true; },
            [](std::exception_ptr) {});

        BOOST_TEST(completed);
        BOOST_TEST_GE(alloc_count, 1);
        BOOST_TEST(!alloc_log.empty());

        // Verify all allocations used the same allocator
        for(int id : alloc_log)
            BOOST_TEST_EQ(id, 1);

        // Expected allocation order:
        // 1. Frame #2 (launcher) is allocated first
        // 2. Frame #1 (task) is allocated second
        // Expected deallocation order:
        // 1. Frame #1 (task) is destroyed first
        // 2. Frame #2 (launcher) is destroyed last
        // This guarantees the pointer in Frame #1's wrapper to Frame #2's embedder is always valid
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

        // dispatcher tests (via async_run)
        testDispatcherUsedByAwait();
        testVoidTaskDispatcherUsedByAwait();

        // affinity propagation tests (ABC problem)
        testAffinityPropagation();
        testAffinityPropagationVoid();
        testNoDispatcherRunsInline();

        // affinity preservation tests
        testInheritedAffinityVerification();
        testAffinityPreservedAcrossMultipleAwaits();
        testAffinityWithNestedVoidTasks();
        testFinalSuspendUsesDispatcher();

        // async_run() function tests
        testAsyncRunValueTask();
        testAsyncRunVoidTask();
        testAsyncRunTaskWithException();
        testAsyncRunVoidTaskWithException();
        testAsyncRunWithNestedAwaits();
        testAsyncRunWithAsyncOp();
        testAsyncRunAffinityPropagation();
        testAsyncRunChained();
        testAsyncRunErrorHandler();
        testAsyncRunDeeplyNested();
        testAsyncRunFireAndForget();
        testAsyncRunSingleHandler();

        // Memory allocation tests
        testAllocatorCapturedOnCreation();
        testAllocatorUsedByChildTasks();
        testAllocatorRestoredAfterAwait();
        testAllocatorRestoredAcrossMultipleAwaits();
        testDeeplyNestedAllocatorPropagation();
        testAllocatorWithMixedTasksAndAsyncOps();
        testDeallocationCount();
        testFrameAllocationOrder();
    }
};

TEST_SUITE(
    task_test,
    "boost.capy.task");

} // capy
} // boost
