//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Test that header file is self-contained.
#include <boost/capy/task.hpp>

#include <boost/capy/ex/run_async.hpp>

#include "test_helpers.hpp"

#include <atomic>
#include <chrono>
#include <thread>
#include <queue>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

namespace boost {
namespace capy {

static_assert(IoAwaitableTask<task<void>>);
static_assert(IoAwaitableTask<task<int>>);
static_assert(IoLaunchableTask<task<void>>);
static_assert(IoLaunchableTask<task<int>>);

/** Tracking executor that logs dispatch calls with an ID.
    Uses pointers to external storage to allow copying.
*/
struct tracking_executor
{
    int id;
    int* dispatch_count_;
    std::vector<int>* dispatch_log;
    test_io_context* ctx_ = nullptr;

    tracking_executor(int id_, int& count, std::vector<int>* log = nullptr)
        : id(id_)
        , dispatch_count_(&count)
        , dispatch_log(log)
    {
    }

    bool operator==(tracking_executor const& other) const noexcept
    {
        return id == other.id && dispatch_count_ == other.dispatch_count_;
    }

    execution_context& context() const noexcept
    {
        return ctx_ ? *ctx_ : default_test_io_context();
    }

    void on_work_started() const noexcept {}
    void on_work_finished() const noexcept {}

    void dispatch(coro h) const
    {
        ++(*dispatch_count_);
        if (dispatch_log)
            dispatch_log->push_back(id);
        h.resume();
    }

    void post(coro h) const
    {
        h.resume();
    }
};

static_assert(Executor<tracking_executor>);

/** Run a task to completion by manually stepping through it.

    Takes ownership of the task via release() and runs until done.
*/
template<class T>
T run_task(task<T> t)
{
    auto h = t.handle();
    t.release();  // Take ownership
    while (!h.done())
        h.resume();
    auto& p = h.promise();
    // Check for exception first (result may be empty if exception occurred)
    if (auto ep = p.exception())
    {
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
            auto h1 = t1.handle();
            t1.release();
            BOOST_TEST(h1);

            // Re-wrap for move test
            task<int> t2(std::move(t1));
            // t1 is now moved-from, t2 should be empty since t1 was released
            // This test verifies move semantics
            BOOST_TEST(!t2.handle());  // t2 is empty

            // Run the released handle
            while (!h1.done())
                h1.resume();
            BOOST_TEST_EQ(*h1.promise().result_, 42);
            h1.destroy();
        }

        // release()
        {
            auto t = returns_int();
            auto h = t.handle();
            t.release();
            BOOST_TEST(h);
            BOOST_TEST(!t.handle());  // Already released

            while (!h.done())
                h.resume();
            auto& result = h.promise().result_;
            BOOST_TEST(result.has_value());
            BOOST_TEST_EQ(*result, 42);

            h.destroy();
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
        auto h = t1.handle();
        t1.release();
        BOOST_TEST(h);

        task<void> t2(std::move(t1));
        // t1 was already released, t2 should be empty
        BOOST_TEST(!t2.handle());

        // Clean up the handle
        while (!h.done())
            h.resume();
        h.destroy();
    }

    void
    testNoDispatcherRunsInline()
    {
        // Verify that simple tasks can run without run_async (manual stepping)
        // Note: Only works for tasks that don't await executor-aware awaitables
        BOOST_TEST_EQ(run_task(chained_tasks()), 25);
    }

    void
    testFinalSuspendUsesDispatcher()
    {
        // Test that when child task completes, it resumes parent via executor
        std::vector<int> log;
        int dispatch_count = 0;
        tracking_executor ex(1, dispatch_count, &log);

        bool completed = false;
        int result = 0;

        // Simple child that just returns a value
        auto child = []() -> task<int> {
            co_return 42;
        };

        // Parent awaits child, then does work
        auto parent = [child]() -> task<int> {
            int v = co_await child();  // child's final_suspend should use executor
            co_return v + 1;
        };

        run_async(ex,
            [&](int v) {
                result = v;
                completed = true;
            },
            [](std::exception_ptr) {})(parent());

        BOOST_TEST(completed);
        BOOST_TEST_EQ(result, 43);
        // Child's completion should dispatch through the executor
        BOOST_TEST_GE(dispatch_count, 1);
    }

    // run_async() tests (replacing old spawn() tests)

    void
    testAsyncRunValueTask()
    {
        int dispatch_count = 0;
        test_executor ex(dispatch_count);
        bool completed = false;
        int result = 0;

        auto compute = []() -> task<int> {
            co_return 42;
        };

        run_async(ex,
            [&](int v) {
                result = v;
                completed = true;
            },
            [](std::exception_ptr) {})(compute());

        BOOST_TEST(completed);
        BOOST_TEST_EQ(result, 42);
        BOOST_TEST_GE(dispatch_count, 1);
    }

    void
    testAsyncRunVoidTask()
    {
        int dispatch_count = 0;
        test_executor ex(dispatch_count);
        bool task_done = false;
        bool completed = false;

        auto do_work = [&task_done]() -> task<void> {
            task_done = true;
            co_return;
        };

        run_async(ex,
            [&]() { completed = true; },
            [](std::exception_ptr) {})(do_work());

        BOOST_TEST(completed);
        BOOST_TEST(task_done);
        BOOST_TEST_GE(dispatch_count, 1);
    }

    void
    testAsyncRunTaskWithException()
    {
        int dispatch_count = 0;
        test_executor ex(dispatch_count);
        bool completed = false;
        bool caught_exception = false;

        auto throwing_task = []() -> task<int> {
            throw_test_exception("run_async test");
            co_return 0;
        };

        run_async(ex,
            [&](int) { completed = true; },
            [&](std::exception_ptr ep) {
                try {
                    std::rethrow_exception(ep);
                } catch (test_exception const&) {
                    caught_exception = true;
                }
            })(throwing_task());

        BOOST_TEST(!completed);
        BOOST_TEST(caught_exception);
    }

    void
    testAsyncRunVoidTaskWithException()
    {
        int dispatch_count = 0;
        test_executor ex(dispatch_count);
        bool completed = false;
        bool caught_exception = false;

        auto throwing_void_task = []() -> task<void> {
            throw_test_exception("void run_async exception");
            co_return;
        };

        run_async(ex,
            [&]() { completed = true; },
            [&](std::exception_ptr ep) {
                try {
                    std::rethrow_exception(ep);
                } catch (test_exception const&) {
                    caught_exception = true;
                }
            })(throwing_void_task());

        BOOST_TEST(!completed);
        BOOST_TEST(caught_exception);
    }

    void
    testAsyncRunWithNestedAwaits()
    {
        int dispatch_count = 0;
        test_executor ex(dispatch_count);
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

        run_async(ex,
            [&](int v) {
                result = v;
                completed = true;
            },
            [](std::exception_ptr) {})(outer());

        BOOST_TEST(completed);
        BOOST_TEST_EQ(result, 20);
    }

    void
    testAsyncRunChained()
    {
        int dispatch_count = 0;
        test_executor ex(dispatch_count);
        int sum = 0;

        auto task1 = []() -> task<int> { co_return 1; };
        auto task2 = []() -> task<int> { co_return 2; };
        auto task3 = []() -> task<int> { co_return 3; };

        run_async(ex, [&](int v) { sum += v; }, [](std::exception_ptr) {})(task1());
        run_async(ex, [&](int v) { sum += v; }, [](std::exception_ptr) {})(task2());
        run_async(ex, [&](int v) { sum += v; }, [](std::exception_ptr) {})(task3());

        BOOST_TEST_EQ(sum, 6);
    }

    void
    testAsyncRunErrorHandler()
    {
        int dispatch_count = 0;
        test_executor ex(dispatch_count);
        bool caught = false;
        std::string error_msg;

        auto failing = []() -> task<int> {
            throw std::runtime_error("specific error");
            co_return 0;
        };

        run_async(ex,
            [](int) {},
            [&](std::exception_ptr ep) {
                try {
                    std::rethrow_exception(ep);
                } catch (std::runtime_error const& e) {
                    error_msg = e.what();
                    caught = true;
                }
            })(failing());

        BOOST_TEST(caught);
        BOOST_TEST_EQ(error_msg, "specific error");
    }

    void
    testAsyncRunFireAndForget()
    {
        // Test fire-and-forget mode (default handler)
        int dispatch_count = 0;
        test_executor ex(dispatch_count);
        std::atomic<bool> task_ran{false};

        auto simple_task = [&task_ran]() -> task<void> {
            task_ran = true;
            co_return;
        };

        run_async(ex)(simple_task());

        BOOST_TEST(task_ran.load());
    }

    void
    testAsyncRunSingleHandler()
    {
        // Test single handler that handles both success and exception
        int dispatch_count = 0;
        test_executor ex(dispatch_count);
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

        run_async(ex,
            overloaded_handler{&success_called, &exception_called})(success_task());

        BOOST_TEST(success_called);
        BOOST_TEST(!exception_called);
    }

    //------------------------------------------------------
    // Memory allocation tests - TLS restoration pattern
    //------------------------------------------------------

    /** Tracking frame allocator that logs allocation/deallocation events.
    */
    template<class T = std::byte>
    struct tracking_frame_allocator
    {
        using value_type = T;

        template<class U>
        struct rebind { using other = tracking_frame_allocator<U>; };

        int id;
        int* alloc_count;
        int* dealloc_count;
        std::vector<int>* alloc_log;

        tracking_frame_allocator(int id_, int* ac, int* dc, std::vector<int>* log)
            : id(id_), alloc_count(ac), dealloc_count(dc), alloc_log(log) {}

        template<class U>
        tracking_frame_allocator(const tracking_frame_allocator<U>& o)
            : id(o.id), alloc_count(o.alloc_count), dealloc_count(o.dealloc_count), alloc_log(o.alloc_log) {}

        T* allocate(std::size_t n)
        {
            ++(*alloc_count);
            if(alloc_log)
                alloc_log->push_back(id);
            return static_cast<T*>(::operator new(n * sizeof(T)));
        }

        void deallocate(T* p, std::size_t)
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
        test_executor ex(dispatch_count);
        bool completed = false;

        int alloc_count = 0;
        int dealloc_count = 0;
        std::vector<int> alloc_log;

        tracking_frame_allocator<> alloc{1, &alloc_count, &dealloc_count, &alloc_log};

        auto simple = []() -> task<void> {
            co_return;
        };

        run_async(ex, std::stop_token{}, alloc,
            [&]() { completed = true; },
            [](std::exception_ptr) {})(simple());

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
        test_executor ex(dispatch_count);
        bool completed = false;

        int alloc_count = 0;
        int dealloc_count = 0;
        std::vector<int> alloc_log;

        tracking_frame_allocator<> alloc{1, &alloc_count, &dealloc_count, &alloc_log};

        auto inner = []() -> task<int> {
            co_return 42;
        };

        auto outer = [inner]() -> task<int> {
            int v = co_await inner();
            co_return v + 1;
        };

        int result = 0;
        run_async(ex, std::stop_token{}, alloc,
            [&](int v) {
                result = v;
                completed = true;
            },
            [](std::exception_ptr) {})(outer());

        BOOST_TEST(completed);
        BOOST_TEST_EQ(result, 43);
        // At least the outer task should be allocated
        BOOST_TEST_GE(alloc_count, 1);
        // All allocations must use our allocator
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
        test_executor ex(dispatch_count);
        bool completed = false;

        int alloc_count = 0;
        int dealloc_count = 0;
        std::vector<int> alloc_log;

        tracking_frame_allocator<> alloc{1, &alloc_count, &dealloc_count, &alloc_log};

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
        run_async(ex, std::stop_token{}, alloc,
            [&](int v) {
                result = v;
                completed = true;
            },
            [](std::exception_ptr) {})(level1());

        BOOST_TEST(completed);
        BOOST_TEST_EQ(result, 1111);
        // At least some allocations should occur
        BOOST_TEST_GE(alloc_count, 1);
        // All allocations must use our allocator (HALO may reduce count)
        for(int id : alloc_log)
            BOOST_TEST_EQ(id, 1);
    }

    void
    testDeallocationCount()
    {
        // Verify that all allocations are eventually deallocated
        int dispatch_count = 0;
        test_executor ex(dispatch_count);
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

        run_async(ex, std::stop_token{}, alloc,
            [&](int) { completed = true; },
            [](std::exception_ptr) {})(outer());

        BOOST_TEST(completed);
        // All allocations should be balanced by deallocations
        BOOST_TEST_EQ(alloc_count, dealloc_count);
    }

    void testFrameAllocationOrder()
    {
        int dispatch_count = 0;
        test_executor ex(dispatch_count);
        bool completed = false;

        int alloc_count = 0;
        int dealloc_count = 0;
        std::vector<int> alloc_log;

        // Allocator ID 1 = launcher (Frame #2)
        // Allocator ID 2 = task (Frame #1)
        tracking_frame_allocator<> alloc{1, &alloc_count, &dealloc_count, &alloc_log};

        auto simple = []() -> task<void> {
            co_return;
        };

        run_async(ex, std::stop_token{}, alloc,
            [&]() { completed = true; },
            [](std::exception_ptr) {})(simple());

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

    //------------------------------------------------------
    // get_stop_token() tests
    //------------------------------------------------------

    static task<bool>
    task_checks_stop_token()
    {
        auto token = co_await this_coro::stop_token;
        co_return token.stop_requested();
    }

    static task<bool>
    task_checks_stop_possible()
    {
        auto token = co_await this_coro::stop_token;
        co_return token.stop_possible();
    }

    void
    testGetStopTokenBasic()
    {
        int dispatch_count = 0;
        test_executor ex(dispatch_count);
        bool stop_possible = true;

        run_async(ex,
            [&](bool v) { stop_possible = v; },
            [](std::exception_ptr) {})(task_checks_stop_possible());

        BOOST_TEST(!stop_possible);
    }

    void
    testGetStopTokenWithSource()
    {
        int dispatch_count = 0;
        test_executor ex(dispatch_count);
        std::stop_source source;
        bool stop_requested = true;

        auto outer = []() -> task<bool> {
            auto token = co_await this_coro::stop_token;
            co_return token.stop_requested();
        };

        run_async(ex,
            [&](bool v) { stop_requested = v; },
            [](std::exception_ptr) {})(outer());

        BOOST_TEST(!stop_requested);
    }

    static task<std::stop_token>
    inner_task_returns_token()
    {
        co_return co_await this_coro::stop_token;
    }

    static task<bool>
    outer_task_propagates_token()
    {
        auto outer_token = co_await this_coro::stop_token;
        auto inner_token = co_await inner_task_returns_token();
        co_return outer_token.stop_possible() == inner_token.stop_possible();
    }

    void
    testGetStopTokenPropagation()
    {
        int dispatch_count = 0;
        test_executor ex(dispatch_count);
        bool tokens_match = false;

        run_async(ex,
            [&](bool v) { tokens_match = v; },
            [](std::exception_ptr) {})(outer_task_propagates_token());

        BOOST_TEST(tokens_match);
    }

    static task<int>
    task_with_cancellation_check()
    {
        auto token = co_await this_coro::stop_token;
        int count = 0;

        for (int i = 0; i < 100; ++i)
        {
            if (token.stop_requested())
                co_return count;
            ++count;
        }

        co_return count;
    }

    void
    testGetStopTokenInLoop()
    {
        int dispatch_count = 0;
        test_executor ex(dispatch_count);
        int result = 0;

        run_async(ex,
            [&](int v) { result = v; },
            [](std::exception_ptr) {})(task_with_cancellation_check());

        BOOST_TEST_EQ(result, 100);
    }

    static task<bool>
    task_get_token_multiple_times()
    {
        auto t1 = co_await this_coro::stop_token;
        auto t2 = co_await this_coro::stop_token;
        auto t3 = co_await this_coro::stop_token;

        co_return t1.stop_possible() == t2.stop_possible() &&
                  t2.stop_possible() == t3.stop_possible();
    }

    void
    testGetStopTokenMultipleCalls()
    {
        int dispatch_count = 0;
        test_executor ex(dispatch_count);
        bool all_same = false;

        run_async(ex,
            [&](bool v) { all_same = v; },
            [](std::exception_ptr) {})(task_get_token_multiple_times());

        BOOST_TEST(all_same);
    }

    struct tear_down_t
    {
        bool *torn_down;
        tear_down_t(bool &torn_down) : torn_down(&torn_down) {}
        tear_down_t(tear_down_t && rhs) noexcept : torn_down(std::exchange(rhs.torn_down, nullptr)) {}
        ~tear_down_t()
        {
            if (torn_down)
                *torn_down = true;
        }
    };

    void testTearDown()
    {
        bool torn_down = false;


        auto l =[](tear_down_t ) -> capy::task<void> {co_return ;};

        {
            auto t = l(torn_down);
            BOOST_TEST(!torn_down);
        }

        BOOST_TEST(torn_down);
    }

    void testDelete()
    {
        bool td1 = false, td2 = false;
        {
            auto l = []() -> capy::task<void>
                    {
                        co_await self_destroy_awaitable{};
                    };
            int dispatch_count = 0;
            test_executor ex(dispatch_count);

            run_async(ex,
                [t = tear_down_t(td1)] { BOOST_TEST(false); },
                [t = tear_down_t(td2)](std::exception_ptr) {BOOST_TEST(false);})(l());

            BOOST_TEST(dispatch_count == 1); // async_run dispatches once to start the test.
        }
        // CHECK if the handlers got destroyed - since we're doing a hard shutdown here, it's ok if they were to live on in the executor.
        BOOST_TEST(td1 == true);
        BOOST_TEST(td2 == true);
    }

    void testStop()
    {

        std::atomic<int> state = 0;

        auto l = [&]() -> capy::task<void>
        {
            struct scope_check
            {
                std::atomic<int> &state;
                ~scope_check() { BOOST_TEST(state == 2);}
            } sc{state};

            state = 1;
            co_await stop_only_awaitable{};
            state = 2;
        };


        {
            std::jthread jt(
                [&](std::stop_token st)
                {
                    int dispatch_count = 0;
                    test_executor ex(dispatch_count);
                    run_async(ex, st)(l());
                }
            );
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            BOOST_TEST(state == 1);
        }

        BOOST_TEST(state == 2);
    }

    void
    run()
    {
        testReturnValue();
        testException();
        testTaskAwaitsTask();
        testMoveOperations();
        testAwaitReady();

        // task<void> tests
        testVoidTaskBasic();
        testVoidTaskException();
        testVoidTaskAwaits();
        testVoidTaskChain();
        testVoidTaskMove();

        // affinity preservation tests
        testNoDispatcherRunsInline();
        testFinalSuspendUsesDispatcher();

        // run_async() function tests
        testAsyncRunValueTask();
        testAsyncRunVoidTask();
        testAsyncRunTaskWithException();
        testAsyncRunVoidTaskWithException();
        testAsyncRunWithNestedAwaits();
        testAsyncRunChained();
        testAsyncRunErrorHandler();
        testAsyncRunFireAndForget();
        testAsyncRunSingleHandler();

        // get_stop_token() tests
        testGetStopTokenBasic();
        testGetStopTokenWithSource();
        testGetStopTokenPropagation();
        testGetStopTokenInLoop();
        testGetStopTokenMultipleCalls();

        testTearDown();
        testDelete();
        testStop();
    }
};

TEST_SUITE(
    task_test,
    "boost.capy.task");

} // capy
} // boost
