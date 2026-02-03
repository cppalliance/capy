//
// Copyright (c) 2026 Michael Vandeberg
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Test that header file is self-contained.
#include <boost/capy/when_any.hpp>

#include <boost/capy/ex/execution_context.hpp>
#include <boost/capy/ex/run_async.hpp>
#include <boost/capy/task.hpp>
#include <boost/capy/when_all.hpp>

#include "test_helpers.hpp"
#include "test_suite.hpp"

#include <atomic>
#include <queue>
#include <stdexcept>
#include <string>
#include <type_traits>

namespace boost {
namespace capy {

struct when_any_test
{
    //----------------------------------------------------------
    // Basic functionality tests
    //----------------------------------------------------------

    // Test: Single task returns immediately
    void
    testSingleTask()
    {
        int dispatch_count = 0;
        test_executor ex(dispatch_count);
        bool completed = false;
        int result = 0;
        std::size_t winner_index = 999;

        run_async(ex,
            [&](auto&& r) {
                completed = true;
                winner_index = r.first;
                result = std::get<0>(r.second);
            },
            [](std::exception_ptr) {})(
            when_any(returns_int(42)));

        BOOST_TEST(completed);
        BOOST_TEST_EQ(winner_index, 0u);
        BOOST_TEST_EQ(result, 42);
    }

    // Test: Two tasks - first completes wins
    void
    testTwoTasksFirstWins()
    {
        int dispatch_count = 0;
        test_executor ex(dispatch_count);
        bool completed = false;
        std::size_t winner_index = 999;
        int result_value = 0;

        run_async(ex,
            [&](auto&& r) {
                completed = true;
                winner_index = r.first;
                // Variant is deduplicated to single int type
                result_value = std::get<int>(r.second);
            },
            [](std::exception_ptr) {})(
            when_any(returns_int(10), returns_int(20)));

        BOOST_TEST(completed);
        // One of them should win, with correct index-to-value mapping
        BOOST_TEST(winner_index == 0 || winner_index == 1);
        if (winner_index == 0)
            BOOST_TEST_EQ(result_value, 10);
        else
            BOOST_TEST_EQ(result_value, 20);
    }

    // Test: Three tasks with different types
    void
    testMixedTypes()
    {
        int dispatch_count = 0;
        test_executor ex(dispatch_count);
        bool completed = false;
        std::size_t winner_index = 999;
        std::variant<int, std::string> result_value;

        run_async(ex,
            [&](auto&& r) {
                completed = true;
                winner_index = r.first;
                result_value = r.second;
            },
            [](std::exception_ptr) {})(
            when_any(returns_int(1), returns_string("hello"), returns_int(3)));

        BOOST_TEST(completed);
        BOOST_TEST(winner_index == 0 || winner_index == 1 || winner_index == 2);
        if (winner_index == 0)
            BOOST_TEST_EQ(std::get<int>(result_value), 1);
        else if (winner_index == 1)
            BOOST_TEST_EQ(std::get<std::string>(result_value), "hello");
        else
            BOOST_TEST_EQ(std::get<int>(result_value), 3);
    }

    // Test: Void task can win
    void
    testVoidTaskWins()
    {
        int dispatch_count = 0;
        test_executor ex(dispatch_count);
        bool completed = false;
        std::size_t winner_index = 999;
        std::variant<std::monostate, int> result_value;

        run_async(ex,
            [&](auto&& r) {
                completed = true;
                winner_index = r.first;
                result_value = r.second;
            },
            [](std::exception_ptr) {})(
            when_any(void_task(), returns_int(42)));

        BOOST_TEST(completed);
        BOOST_TEST(winner_index == 0 || winner_index == 1);
        if (winner_index == 0)
            BOOST_TEST(std::holds_alternative<std::monostate>(result_value));
        else
            BOOST_TEST_EQ(std::get<int>(result_value), 42);
    }

    // Test: All void tasks
    void
    testAllVoidTasks()
    {
        int dispatch_count = 0;
        test_executor ex(dispatch_count);
        bool completed = false;
        std::size_t winner_index = 999;
        std::variant<std::monostate> result_value;

        run_async(ex,
            [&](auto&& r) {
                completed = true;
                winner_index = r.first;
                result_value = r.second;
            },
            [](std::exception_ptr) {})(
            when_any(void_task(), void_task(), void_task()));

        BOOST_TEST(completed);
        BOOST_TEST(winner_index == 0 || winner_index == 1 || winner_index == 2);
        // All void tasks produce monostate regardless of index
        BOOST_TEST(std::holds_alternative<std::monostate>(result_value));
    }

    //----------------------------------------------------------
    // Exception handling tests
    //----------------------------------------------------------

    // Test: Exception from single task propagates
    void
    testSingleTaskException()
    {
        int dispatch_count = 0;
        test_executor ex(dispatch_count);
        bool completed = false;
        bool caught_exception = false;
        std::string error_msg;

        run_async(ex,
            [&](auto&&) { completed = true; },
            [&](std::exception_ptr ep) {
                try {
                    std::rethrow_exception(ep);
                } catch (test_exception const& e) {
                    caught_exception = true;
                    error_msg = e.what();
                }
            })(when_any(throws_exception("test error")));

        BOOST_TEST(!completed);
        BOOST_TEST(caught_exception);
        BOOST_TEST_EQ(error_msg, "test error");
    }

    // Test: Exception wins the race (exception is a valid completion)
    void
    testExceptionWinsRace()
    {
        int dispatch_count = 0;
        test_executor ex(dispatch_count);
        bool caught_exception = false;
        std::string error_msg;

        run_async(ex,
            [](auto&&) {},
            [&](std::exception_ptr ep) {
                try {
                    std::rethrow_exception(ep);
                } catch (test_exception const& e) {
                    caught_exception = true;
                    error_msg = e.what();
                }
            })(when_any(throws_exception("winner error"), returns_int(42)));

        // With synchronous executor, first task (the thrower) wins
        BOOST_TEST(caught_exception);
        BOOST_TEST_EQ(error_msg, "winner error");
    }

    // Test: Void task exception
    void
    testVoidTaskException()
    {
        int dispatch_count = 0;
        test_executor ex(dispatch_count);
        bool caught_exception = false;
        std::string error_msg;

        run_async(ex,
            [](auto&&) {},
            [&](std::exception_ptr ep) {
                try {
                    std::rethrow_exception(ep);
                } catch (test_exception const& e) {
                    caught_exception = true;
                    error_msg = e.what();
                }
            })(when_any(void_throws_exception("void error"), returns_int(42)));

        BOOST_TEST(caught_exception);
        BOOST_TEST_EQ(error_msg, "void error");
    }

    // Test: Multiple exceptions - first wins
    void
    testMultipleExceptionsFirstWins()
    {
        int dispatch_count = 0;
        test_executor ex(dispatch_count);
        bool caught_exception = false;
        std::string error_msg;

        run_async(ex,
            [](auto&&) {},
            [&](std::exception_ptr ep) {
                try {
                    std::rethrow_exception(ep);
                } catch (test_exception const& e) {
                    caught_exception = true;
                    error_msg = e.what();
                }
            })(when_any(
                throws_exception("error_1"),
                throws_exception("error_2"),
                throws_exception("error_3")));

        BOOST_TEST(caught_exception);
        // One of them wins
        BOOST_TEST(
            error_msg == "error_1" ||
            error_msg == "error_2" ||
            error_msg == "error_3");
    }

    //----------------------------------------------------------
    // Stop token propagation tests
    //----------------------------------------------------------

    // Test: Stop is requested when winner completes
    void
    testStopRequestedOnCompletion()
    {
        int dispatch_count = 0;
        test_executor ex(dispatch_count);
        std::atomic<int> completion_count{0};
        bool completed = false;

        auto counting_task = [&]() -> task<int> {
            ++completion_count;
            co_return completion_count.load();
        };

        run_async(ex,
            [&](auto&&) {
                completed = true;
            },
            [](std::exception_ptr) {})(
            when_any(counting_task(), counting_task(), counting_task()));

        BOOST_TEST(completed);
        // All three tasks should run to completion
        // (stop is requested, but synchronous tasks complete anyway)
        BOOST_TEST_EQ(completion_count.load(), 3);
    }

    // Test: All tasks complete even after winner (cleanup)
    void
    testAllTasksCompleteForCleanup()
    {
        int dispatch_count = 0;
        test_executor ex(dispatch_count);
        std::atomic<int> completion_count{0};
        bool completed = false;

        auto counting_task = [&](int value) -> task<int> {
            ++completion_count;
            co_return value;
        };

        run_async(ex,
            [&](auto&& r) {
                completed = true;
                // Winner should be first task (synchronous executor)
                BOOST_TEST_EQ(r.first, 0u);
            },
            [](std::exception_ptr) {})(
            when_any(
                counting_task(1),
                counting_task(2),
                counting_task(3),
                counting_task(4)));

        BOOST_TEST(completed);
        // All four tasks must complete for proper cleanup
        BOOST_TEST_EQ(completion_count.load(), 4);
    }

    //----------------------------------------------------------
    // Long-lived task cancellation tests
    //----------------------------------------------------------

    // Test: Long-lived tasks exit early when stop is requested
    void
    testLongLivedTasksCancelledOnWinner()
    {
        std::queue<coro> work_queue;
        queuing_executor ex(work_queue);

        std::atomic<int> cancelled_count{0};
        std::atomic<int> completed_normally_count{0};
        bool when_any_completed = false;
        std::size_t winner_index = 999;
        int winner_value = 0;

        // A task that completes immediately
        auto fast_task = [&]() -> task<int> {
            ++completed_normally_count;
            co_return 42;
        };

        // A task that does multiple steps, checking stop token between each
        auto slow_task = [&](int id, int steps) -> task<int> {
            for (int i = 0; i < steps; ++i) {
                auto token = co_await this_coro::stop_token;
                if (token.stop_requested()) {
                    ++cancelled_count;
                    co_return -1;  // Cancelled
                }
                co_await yield_awaitable{};
            }
            ++completed_normally_count;
            co_return id;
        };

        run_async(ex,
            [&](auto&& r) {
                when_any_completed = true;
                winner_index = r.first;
                winner_value = std::get<int>(r.second);
            },
            [](std::exception_ptr) {})(
            when_any(fast_task(), slow_task(100, 10), slow_task(200, 10)));

        // Process work queue until empty
        while (!work_queue.empty()) {
            auto h = work_queue.front();
            work_queue.pop();
            h.resume();
        }

        BOOST_TEST(when_any_completed);
        BOOST_TEST_EQ(winner_index, 0u);  // fast_task wins
        BOOST_TEST_EQ(winner_value, 42);

        // The fast task completed normally
        BOOST_TEST_EQ(completed_normally_count.load(), 1);

        // Both slow tasks should have been cancelled
        BOOST_TEST_EQ(cancelled_count.load(), 2);
    }

    // Test: Slow task can win if it finishes first
    void
    testSlowTaskCanWin()
    {
        std::queue<coro> work_queue;
        queuing_executor ex(work_queue);

        std::atomic<int> cancelled_count{0};
        std::atomic<int> completed_normally_count{0};
        bool when_any_completed = false;
        std::size_t winner_index = 999;
        int winner_value = 0;

        // A task that does a few steps then completes
        auto medium_task = [&](int id, int steps) -> task<int> {
            for (int i = 0; i < steps; ++i) {
                auto token = co_await this_coro::stop_token;
                if (token.stop_requested()) {
                    ++cancelled_count;
                    co_return -1;
                }
                co_await yield_awaitable{};
            }
            ++completed_normally_count;
            co_return id;
        };

        // Task 0: 3 steps, Task 1: 1 step (wins), Task 2: 4 steps
        // With FIFO scheduling, task1 completes after 1 yield while others
        // are still in progress and will observe the stop request.
        run_async(ex,
            [&](auto&& r) {
                when_any_completed = true;
                winner_index = r.first;
                winner_value = std::get<int>(r.second);
            },
            [](std::exception_ptr) {})(
            when_any(medium_task(10, 3), medium_task(20, 1), medium_task(30, 4)));

        // Process work queue until empty
        while (!work_queue.empty()) {
            auto h = work_queue.front();
            work_queue.pop();
            h.resume();
        }

        BOOST_TEST(when_any_completed);
        BOOST_TEST_EQ(winner_index, 1u);  // Task with 1 step wins
        BOOST_TEST_EQ(winner_value, 20);

        // Only the winner completed normally
        BOOST_TEST_EQ(completed_normally_count.load(), 1);

        // Other two tasks were cancelled
        BOOST_TEST_EQ(cancelled_count.load(), 2);
    }

    // Test: Tasks that don't check stop token still complete (cleanup)
    void
    testNonCooperativeTasksStillComplete()
    {
        std::queue<coro> work_queue;
        queuing_executor ex(work_queue);

        std::atomic<int> completion_count{0};
        bool when_any_completed = false;

        // A task that completes immediately
        auto fast_task = [&]() -> task<int> {
            ++completion_count;
            co_return 42;
        };

        // A task that ignores stop token (non-cooperative)
        auto non_cooperative_task = [&](int id, int steps) -> task<int> {
            for (int i = 0; i < steps; ++i) {
                // Deliberately NOT checking stop token
                co_await yield_awaitable{};
            }
            ++completion_count;
            co_return id;
        };

        run_async(ex,
            [&](auto&& r) {
                when_any_completed = true;
                BOOST_TEST_EQ(r.first, 0u);  // fast_task wins
            },
            [](std::exception_ptr) {})(
            when_any(fast_task(), non_cooperative_task(100, 3), non_cooperative_task(200, 3)));

        // Process work queue until empty
        while (!work_queue.empty()) {
            auto h = work_queue.front();
            work_queue.pop();
            h.resume();
        }

        BOOST_TEST(when_any_completed);

        // All three tasks complete (non-cooperative tasks run to completion)
        BOOST_TEST_EQ(completion_count.load(), 3);
    }

    // Test: Mixed cooperative and non-cooperative tasks
    void
    testMixedCooperativeAndNonCooperativeTasks()
    {
        std::queue<coro> work_queue;
        queuing_executor ex(work_queue);

        std::atomic<int> cooperative_cancelled{0};
        std::atomic<int> non_cooperative_finished{0};
        std::atomic<int> winner_finished{0};
        bool when_any_completed = false;

        auto fast_task = [&]() -> task<int> {
            ++winner_finished;
            co_return 1;
        };

        auto cooperative_slow = [&](int steps) -> task<int> {
            for (int i = 0; i < steps; ++i) {
                auto token = co_await this_coro::stop_token;
                if (token.stop_requested()) {
                    ++cooperative_cancelled;
                    co_return -1;
                }
                co_await yield_awaitable{};
            }
            co_return 2;
        };

        auto non_cooperative_slow = [&](int steps) -> task<int> {
            for (int i = 0; i < steps; ++i) {
                co_await yield_awaitable{};
            }
            ++non_cooperative_finished;
            co_return 3;
        };

        run_async(ex,
            [&](auto&& r) {
                when_any_completed = true;
                BOOST_TEST_EQ(r.first, 0u);
            },
            [](std::exception_ptr) {})(
            when_any(fast_task(), cooperative_slow(5), non_cooperative_slow(5)));

        while (!work_queue.empty()) {
            auto h = work_queue.front();
            work_queue.pop();
            h.resume();
        }

        BOOST_TEST(when_any_completed);
        BOOST_TEST_EQ(winner_finished.load(), 1);
        BOOST_TEST_EQ(cooperative_cancelled.load(), 1);
        BOOST_TEST_EQ(non_cooperative_finished.load(), 1);
    }

    //----------------------------------------------------------
    // Nested when_any tests
    //----------------------------------------------------------

    // Test: Nested when_any
    void
    testNestedWhenAny()
    {
        int dispatch_count = 0;
        test_executor ex(dispatch_count);
        bool completed = false;
        int result = 0;

        auto inner1 = []() -> task<int> {
            auto [idx, res] = co_await when_any(returns_int(10), returns_int(20));
            co_return std::get<int>(res);
        };

        auto inner2 = []() -> task<int> {
            auto [idx, res] = co_await when_any(returns_int(30), returns_int(40));
            co_return std::get<int>(res);
        };

        std::size_t winner_index = 999;

        run_async(ex,
            [&](auto&& r) {
                completed = true;
                winner_index = r.first;
                result = std::get<int>(r.second);
            },
            [](std::exception_ptr) {})(
            when_any(inner1(), inner2()));

        BOOST_TEST(completed);
        BOOST_TEST(winner_index == 0 || winner_index == 1);
        // inner1 returns 10 or 20, inner2 returns 30 or 40
        if (winner_index == 0)
            BOOST_TEST(result == 10 || result == 20);
        else
            BOOST_TEST(result == 30 || result == 40);
    }

    // Test: when_any inside when_all
    void
    testWhenAnyInsideWhenAll()
    {
        int dispatch_count = 0;
        test_executor ex(dispatch_count);
        bool completed = false;

        auto race1 = []() -> task<int> {
            auto [idx, res] = co_await when_any(returns_int(1), returns_int(2));
            co_return std::get<int>(res);
        };

        auto race2 = []() -> task<int> {
            auto [idx, res] = co_await when_any(returns_int(3), returns_int(4));
            co_return std::get<int>(res);
        };

        run_async(ex,
            [&](std::tuple<int, int> t) {
                auto [a, b] = t;
                completed = true;
                BOOST_TEST((a == 1 || a == 2));
                BOOST_TEST((b == 3 || b == 4));
            },
            [](std::exception_ptr) {})(
            when_all(race1(), race2()));

        BOOST_TEST(completed);
    }

    // Test: when_all inside when_any
    void
    testWhenAllInsideWhenAny()
    {
        int dispatch_count = 0;
        test_executor ex(dispatch_count);
        bool completed = false;
        std::size_t winner_index = 999;
        int result_value = 0;

        auto concurrent1 = []() -> task<int> {
            auto [a, b] = co_await when_all(returns_int(1), returns_int(2));
            co_return a + b;
        };

        auto concurrent2 = []() -> task<int> {
            auto [a, b] = co_await when_all(returns_int(3), returns_int(4));
            co_return a + b;
        };

        run_async(ex,
            [&](auto&& r) {
                completed = true;
                winner_index = r.first;
                result_value = std::get<int>(r.second);
            },
            [](std::exception_ptr) {})(
            when_any(concurrent1(), concurrent2()));

        BOOST_TEST(completed);
        BOOST_TEST(winner_index == 0 || winner_index == 1);
        // concurrent1 returns 1+2=3, concurrent2 returns 3+4=7
        if (winner_index == 0)
            BOOST_TEST_EQ(result_value, 3);
        else
            BOOST_TEST_EQ(result_value, 7);
    }

    //----------------------------------------------------------
    // Edge case tests
    //----------------------------------------------------------

    // Test: Large number of tasks
    void
    testManyTasks()
    {
        int dispatch_count = 0;
        test_executor ex(dispatch_count);
        bool completed = false;
        std::size_t winner_index = 999;
        int result_value = 0;

        run_async(ex,
            [&](auto r) {
                completed = true;
                winner_index = r.first;
                result_value = std::get<int>(r.second);
            },
            [](std::exception_ptr) {})(when_any(
                returns_int(1), returns_int(2), returns_int(3), returns_int(4),
                returns_int(5), returns_int(6), returns_int(7), returns_int(8)));

        BOOST_TEST(completed);
        BOOST_TEST(winner_index < 8);
        // Verify correct index-to-value mapping (index 0 -> value 1, etc.)
        BOOST_TEST_EQ(result_value, static_cast<int>(winner_index + 1));
    }

    // Test: Task that does multiple internal operations
    static task<int>
    multi_step_task(int start)
    {
        int value = start;
        value += co_await returns_int(1);
        value += co_await returns_int(2);
        co_return value;
    }

    void
    testTasksWithMultipleSteps()
    {
        int dispatch_count = 0;
        test_executor ex(dispatch_count);
        bool completed = false;
        std::size_t winner_index = 999;
        int result_value = 0;

        run_async(ex,
            [&](auto&& r) {
                completed = true;
                winner_index = r.first;
                result_value = std::get<int>(r.second);
            },
            [](std::exception_ptr) {})(
            when_any(multi_step_task(10), multi_step_task(20)));

        BOOST_TEST(completed);
        BOOST_TEST(winner_index == 0 || winner_index == 1);
        // Index 0: 10+1+2=13, Index 1: 20+1+2=23
        if (winner_index == 0)
            BOOST_TEST_EQ(result_value, 13);
        else
            BOOST_TEST_EQ(result_value, 23);
    }

    //----------------------------------------------------------
    // Awaitable lifecycle tests
    //----------------------------------------------------------

    // Test: when_any result is move constructible
    void
    testAwaitableMoveConstruction()
    {
        int dispatch_count = 0;
        test_executor ex(dispatch_count);
        bool completed = false;
        std::size_t winner_index = 999;
        int result_value = 0;

        auto awaitable1 = when_any(returns_int(1), returns_int(2));
        auto awaitable2 = std::move(awaitable1);

        run_async(ex,
            [&](auto&& r) {
                completed = true;
                winner_index = r.first;
                result_value = std::get<int>(r.second);
            },
            [](std::exception_ptr) {})(std::move(awaitable2));

        BOOST_TEST(completed);
        BOOST_TEST(winner_index == 0 || winner_index == 1);
        if (winner_index == 0)
            BOOST_TEST_EQ(result_value, 1);
        else
            BOOST_TEST_EQ(result_value, 2);
    }

    // Test: when_any can be stored and awaited later
    void
    testDeferredAwait()
    {
        int dispatch_count = 0;
        test_executor ex(dispatch_count);
        bool completed = false;
        std::size_t winner_index = 999;
        int result_value = 0;

        auto deferred = when_any(returns_int(10), returns_int(20));

        run_async(ex,
            [&](auto&& r) {
                completed = true;
                winner_index = r.first;
                result_value = std::get<int>(r.second);
            },
            [](std::exception_ptr) {})(std::move(deferred));

        BOOST_TEST(completed);
        BOOST_TEST(winner_index == 0 || winner_index == 1);
        if (winner_index == 0)
            BOOST_TEST_EQ(result_value, 10);
        else
            BOOST_TEST_EQ(result_value, 20);
    }

    //----------------------------------------------------------
    // Variant access tests
    //----------------------------------------------------------

    // Test: Correct variant alternative is populated
    void
    testVariantAlternativePopulated()
    {
        int dispatch_count = 0;
        test_executor ex(dispatch_count);
        bool completed = false;

        // Note: <int, string, int> deduplicates to variant<int, string>
        run_async(ex,
            [&](auto&& r) {
                completed = true;
                // With synchronous executor, first task wins
                BOOST_TEST_EQ(r.first, 0u);
                BOOST_TEST(std::holds_alternative<int>(r.second));
                BOOST_TEST_EQ(std::get<int>(r.second), 42);
            },
            [](std::exception_ptr) {})(
            when_any(returns_int(42), returns_string("hello"), returns_int(99)));

        BOOST_TEST(completed);
    }

    // Test: Can use std::visit on result variant
    void
    testVariantVisit()
    {
        int dispatch_count = 0;
        test_executor ex(dispatch_count);
        bool completed = false;
        std::size_t winner_index = 999;
        std::variant<int, std::string> result_value;

        run_async(ex,
            [&](auto&& r) {
                completed = true;
                winner_index = r.first;
                result_value = r.second;
            },
            [](std::exception_ptr) {})(
            when_any(returns_int(42), returns_string("hello")));

        BOOST_TEST(completed);
        BOOST_TEST(winner_index == 0 || winner_index == 1);
        if (winner_index == 0)
            BOOST_TEST_EQ(std::get<int>(result_value), 42);
        else
            BOOST_TEST_EQ(std::get<std::string>(result_value), "hello");
    }

    //----------------------------------------------------------
    // Parent stop token propagation tests
    //----------------------------------------------------------

    // Test: Parent stop token already requested before when_any starts
    void
    testParentStopAlreadyRequested()
    {
        std::queue<coro> work_queue;
        queuing_executor ex(work_queue);

        std::atomic<int> saw_stop_count{0};
        bool when_any_completed = false;
        std::size_t winner_index = 999;

        // A task that checks stop token on first suspension
        auto check_stop_task = [&](int id) -> task<int> {
            auto token = co_await this_coro::stop_token;
            if (token.stop_requested()) {
                ++saw_stop_count;
            }
            co_return id;
        };

        // Use a stop_source to simulate parent cancellation
        std::stop_source parent_stop;
        parent_stop.request_stop();

        // Use run_async with stop_token parameter to test propagation
        run_async(ex, parent_stop.get_token(),
            [&](auto&& r) {
                when_any_completed = true;
                winner_index = r.first;
            },
            [](std::exception_ptr) {})(
            when_any(check_stop_task(1), check_stop_task(2), check_stop_task(3)));

        while (!work_queue.empty()) {
            auto h = work_queue.front();
            work_queue.pop();
            h.resume();
        }

        BOOST_TEST(when_any_completed);
        // All tasks should have seen the stop token as requested
        // (inherited from parent)
        BOOST_TEST_EQ(saw_stop_count.load(), 3);
    }

    // Test: Parent stop requested after tasks start but before winner
    void
    testParentStopDuringExecution()
    {
        std::queue<coro> work_queue;
        queuing_executor ex(work_queue);

        std::atomic<int> cancelled_count{0};
        bool when_any_completed = false;

        auto slow_task = [&](int id, int steps) -> task<int> {
            for (int i = 0; i < steps; ++i) {
                auto token = co_await this_coro::stop_token;
                if (token.stop_requested()) {
                    ++cancelled_count;
                    co_return -1;
                }
                co_await yield_awaitable{};
            }
            co_return id;
        };

        std::stop_source parent_stop;

        // Use run_async with stop_token parameter
        run_async(ex, parent_stop.get_token(),
            [&](auto&&) {
                when_any_completed = true;
            },
            [](std::exception_ptr) {})(
            when_any(slow_task(1, 10), slow_task(2, 10)));

        // Run a few iterations, then request parent stop
        for (int i = 0; i < 3 && !work_queue.empty(); ++i) {
            auto h = work_queue.front();
            work_queue.pop();
            h.resume();
        }

        // Request stop from parent
        parent_stop.request_stop();

        // Finish processing
        while (!work_queue.empty()) {
            auto h = work_queue.front();
            work_queue.pop();
            h.resume();
        }

        BOOST_TEST(when_any_completed);
        // Both tasks should have been cancelled by parent stop
        BOOST_TEST_EQ(cancelled_count.load(), 2);
    }

    //----------------------------------------------------------
    // Interleaved exception tests
    //----------------------------------------------------------

    // Test: Multiple exceptions thrown with interleaved execution
    void
    testInterleavedExceptions()
    {
        std::queue<coro> work_queue;
        queuing_executor ex(work_queue);

        bool caught_exception = false;
        std::string error_msg;

        // Tasks that yield before throwing
        auto delayed_throw = [](int id, int yields) -> task<int> {
            for (int i = 0; i < yields; ++i) {
                co_await yield_awaitable{};
            }
            throw test_exception(("error_" + std::to_string(id)).c_str());
            co_return id;
        };

        run_async(ex,
            [](auto&&) {},
            [&](std::exception_ptr ep) {
                try {
                    std::rethrow_exception(ep);
                } catch (test_exception const& e) {
                    caught_exception = true;
                    error_msg = e.what();
                }
            })(when_any(delayed_throw(1, 2), delayed_throw(2, 1), delayed_throw(3, 3)));

        while (!work_queue.empty()) {
            auto h = work_queue.front();
            work_queue.pop();
            h.resume();
        }

        BOOST_TEST(caught_exception);
        // Task 2 throws first (after 1 yield)
        BOOST_TEST_EQ(error_msg, "error_2");
    }

    //----------------------------------------------------------
    // Nested stop propagation tests
    //----------------------------------------------------------

    // Test: Stop propagates through nested when_any - outer task cancelled before inner starts
    void
    testNestedStopPropagationOuterCancelled()
    {
        std::queue<coro> work_queue;
        queuing_executor ex(work_queue);

        std::atomic<int> outer_cancelled{0};
        bool when_any_completed = false;
        std::size_t winner_index = 999;

        auto fast_task = [&]() -> task<int> {
            co_return 42;
        };

        // A task that checks stop before launching inner when_any
        auto nested_when_any_task = [&]() -> task<int> {
            auto token = co_await this_coro::stop_token;
            if (token.stop_requested()) {
                ++outer_cancelled;
                co_return -1;
            }
            // Won't reach here if stopped
            co_return 100;
        };

        run_async(ex,
            [&](auto&& r) {
                when_any_completed = true;
                winner_index = r.first;
            },
            [](std::exception_ptr) {})(
            when_any(fast_task(), nested_when_any_task()));

        while (!work_queue.empty()) {
            auto h = work_queue.front();
            work_queue.pop();
            h.resume();
        }

        BOOST_TEST(when_any_completed);
        BOOST_TEST_EQ(winner_index, 0u);  // fast_task wins
        // The nested task should see stop and exit early
        BOOST_TEST_EQ(outer_cancelled.load(), 1);
    }

    // Test: Stop propagates to inner when_any's children
    void
    testNestedStopPropagationInnerCancelled()
    {
        std::queue<coro> work_queue;
        queuing_executor ex(work_queue);

        std::atomic<int> inner_cancelled{0};
        std::atomic<int> inner_completed{0};
        bool when_any_completed = false;
        std::size_t winner_index = 999;

        // Fast task that yields first to let nested when_any start
        auto yielding_fast_task = [&]() -> task<int> {
            co_await yield_awaitable{};
            co_return 42;
        };

        auto slow_inner_task = [&](int steps) -> task<int> {
            for (int i = 0; i < steps; ++i) {
                auto token = co_await this_coro::stop_token;
                if (token.stop_requested()) {
                    ++inner_cancelled;
                    co_return -1;
                }
                co_await yield_awaitable{};
            }
            ++inner_completed;
            co_return 100;
        };

        // A task containing a nested when_any - doesn't check stop first
        auto nested_when_any_task = [&]() -> task<int> {
            // Start inner when_any immediately (no stop check first)
            auto [idx, res] = co_await when_any(
                slow_inner_task(10),
                slow_inner_task(10));
            co_return std::get<int>(res);
        };

        run_async(ex,
            [&](auto&& r) {
                when_any_completed = true;
                winner_index = r.first;
            },
            [](std::exception_ptr) {})(
            when_any(yielding_fast_task(), nested_when_any_task()));

        while (!work_queue.empty()) {
            auto h = work_queue.front();
            work_queue.pop();
            h.resume();
        }

        BOOST_TEST(when_any_completed);
        // One of them should win
        BOOST_TEST(winner_index == 0 || winner_index == 1);

        if (winner_index == 0) {
            // If yielding_fast_task won, the inner tasks should be cancelled
            BOOST_TEST_EQ(inner_cancelled.load(), 2);
            BOOST_TEST_EQ(inner_completed.load(), 0);
        } else {
            // If nested_when_any_task won (one of its inner tasks completed)
            // one inner task completes, other gets cancelled
            BOOST_TEST_EQ(inner_completed.load(), 1);
            BOOST_TEST_EQ(inner_cancelled.load(), 1);
        }
    }

    //----------------------------------------------------------
    // Variant usage pattern tests
    //----------------------------------------------------------

    // Test: Document correct pattern for variant access based on index
    void
    testVariantAccessByIndex()
    {
        int dispatch_count = 0;
        test_executor ex(dispatch_count);
        bool completed = false;
        bool correct_access = false;

        run_async(ex,
            [&](auto&& r) {
                completed = true;
                // The correct pattern: use index to determine which type to access
                switch (r.first) {
                    case 0:
                        correct_access = std::holds_alternative<int>(r.second);
                        BOOST_TEST_EQ(std::get<int>(r.second), 42);
                        break;
                    case 1:
                        correct_access = std::holds_alternative<std::string>(r.second);
                        BOOST_TEST_EQ(std::get<std::string>(r.second), "hello");
                        break;
                    case 2:
                        correct_access = std::holds_alternative<double>(r.second);
                        BOOST_TEST_EQ(std::get<double>(r.second), 3.14);
                        break;
                }
            },
            [](std::exception_ptr) {})(
            when_any(returns_int(42), returns_string("hello"), []() -> task<double> { co_return 3.14; }()));

        BOOST_TEST(completed);
        BOOST_TEST(correct_access);
    }

    // Test: Variant with duplicate types - index disambiguation
    void
    testVariantDuplicateTypesIndexDisambiguation()
    {
        int dispatch_count = 0;
        test_executor ex(dispatch_count);
        bool completed = false;
        std::size_t winner_index = 999;
        int result_value = 0;

        // when_any(int, int, int) deduplicates to variant<int>
        // but winner_index tells us WHICH task won
        run_async(ex,
            [&](auto&& r) {
                completed = true;
                winner_index = r.first;
                result_value = std::get<int>(r.second);
            },
            [](std::exception_ptr) {})(
            when_any(returns_int(100), returns_int(200), returns_int(300)));

        BOOST_TEST(completed);
        // With synchronous executor, first task wins
        BOOST_TEST_EQ(winner_index, 0u);
        BOOST_TEST_EQ(result_value, 100);
    }

    void
    run()
    {
        // Basic functionality
        testSingleTask();
        testTwoTasksFirstWins();
        testMixedTypes();
        testVoidTaskWins();
        testAllVoidTasks();

        // Exception handling
        testSingleTaskException();
        testExceptionWinsRace();
        testVoidTaskException();
        testMultipleExceptionsFirstWins();

        // Stop token propagation
        testStopRequestedOnCompletion();
        testAllTasksCompleteForCleanup();

        // Parent stop token propagation
        testParentStopAlreadyRequested();
        testParentStopDuringExecution();

        // Long-lived task cancellation
        testLongLivedTasksCancelledOnWinner();
        testSlowTaskCanWin();
        testNonCooperativeTasksStillComplete();
        testMixedCooperativeAndNonCooperativeTasks();

        // Interleaved exceptions
        testInterleavedExceptions();

        // Nested combinators
        testNestedWhenAny();
        testWhenAnyInsideWhenAll();
        testWhenAllInsideWhenAny();

        // Nested stop propagation
        testNestedStopPropagationOuterCancelled();
        testNestedStopPropagationInnerCancelled();

        // Edge cases
        testManyTasks();
        testTasksWithMultipleSteps();

        // Awaitable lifecycle
        testAwaitableMoveConstruction();
        testDeferredAwait();

        // Variant access
        testVariantAlternativePopulated();
        testVariantVisit();
        testVariantAccessByIndex();
        testVariantDuplicateTypesIndexDisambiguation();
    }
};

TEST_SUITE(
    when_any_test,
    "boost.capy.when_any");

//----------------------------------------------------------
// Homogeneous when_any tests (vector overload)
//----------------------------------------------------------

struct when_any_vector_test
{
    //----------------------------------------------------------
    // Basic functionality tests
    //----------------------------------------------------------

    // Test: Single task in vector
    void
    testSingleTaskVector()
    {
        int dispatch_count = 0;
        test_executor ex(dispatch_count);
        bool completed = false;
        int result = 0;
        std::size_t winner_index = 999;

        std::vector<task<int>> tasks;
        tasks.push_back(returns_int(42));

        run_async(ex,
            [&](std::pair<std::size_t, int> r) {
                completed = true;
                winner_index = r.first;
                result = r.second;
            },
            [](std::exception_ptr) {})(
            when_any(std::move(tasks)));

        BOOST_TEST(completed);
        BOOST_TEST_EQ(winner_index, 0u);
        BOOST_TEST_EQ(result, 42);
    }

    // Test: Multiple tasks in vector
    void
    testMultipleTasksVector()
    {
        int dispatch_count = 0;
        test_executor ex(dispatch_count);
        bool completed = false;
        std::size_t winner_index = 999;
        int result_value = 0;

        std::vector<task<int>> tasks;
        tasks.push_back(returns_int(10));
        tasks.push_back(returns_int(20));
        tasks.push_back(returns_int(30));

        run_async(ex,
            [&](std::pair<std::size_t, int> r) {
                completed = true;
                winner_index = r.first;
                result_value = r.second;
            },
            [](std::exception_ptr) {})(
            when_any(std::move(tasks)));

        BOOST_TEST(completed);
        BOOST_TEST(winner_index < 3);
        // Verify correct index-to-value mapping
        BOOST_TEST_EQ(result_value, static_cast<int>((winner_index + 1) * 10));
    }

    // Test: Empty vector throws
    void
    testEmptyVectorThrows()
    {
        int dispatch_count = 0;
        test_executor ex(dispatch_count);
        bool caught_exception = false;

        std::vector<task<int>> tasks;

        run_async(ex,
            [](std::pair<std::size_t, int>) {},
            [&](std::exception_ptr ep) {
                try {
                    std::rethrow_exception(ep);
                } catch (std::invalid_argument const&) {
                    caught_exception = true;
                }
            })(when_any(std::move(tasks)));

        BOOST_TEST(caught_exception);
    }

    // Test: Void tasks in vector
    void
    testVoidTasksVector()
    {
        int dispatch_count = 0;
        test_executor ex(dispatch_count);
        bool completed = false;
        std::size_t winner_index = 999;

        std::vector<task<void>> tasks;
        tasks.push_back(void_task());
        tasks.push_back(void_task());
        tasks.push_back(void_task());

        run_async(ex,
            [&](std::size_t idx) {
                completed = true;
                winner_index = idx;
            },
            [](std::exception_ptr) {})(
            when_any(std::move(tasks)));

        BOOST_TEST(completed);
        BOOST_TEST(winner_index < 3);
    }

    //----------------------------------------------------------
    // Exception handling tests
    //----------------------------------------------------------

    // Test: Exception from task in vector
    void
    testExceptionInVector()
    {
        int dispatch_count = 0;
        test_executor ex(dispatch_count);
        bool caught_exception = false;
        std::string error_msg;

        std::vector<task<int>> tasks;
        tasks.push_back(throws_exception("vector error"));

        run_async(ex,
            [](std::pair<std::size_t, int>) {},
            [&](std::exception_ptr ep) {
                try {
                    std::rethrow_exception(ep);
                } catch (test_exception const& e) {
                    caught_exception = true;
                    error_msg = e.what();
                }
            })(when_any(std::move(tasks)));

        BOOST_TEST(caught_exception);
        BOOST_TEST_EQ(error_msg, "vector error");
    }

    // Test: Exception wins race in vector
    void
    testExceptionWinsRaceVector()
    {
        int dispatch_count = 0;
        test_executor ex(dispatch_count);
        bool caught_exception = false;
        std::string error_msg;

        std::vector<task<int>> tasks;
        tasks.push_back(throws_exception("winner"));
        tasks.push_back(returns_int(42));
        tasks.push_back(returns_int(99));

        run_async(ex,
            [](std::pair<std::size_t, int>) {},
            [&](std::exception_ptr ep) {
                try {
                    std::rethrow_exception(ep);
                } catch (test_exception const& e) {
                    caught_exception = true;
                    error_msg = e.what();
                }
            })(when_any(std::move(tasks)));

        BOOST_TEST(caught_exception);
        BOOST_TEST_EQ(error_msg, "winner");
    }

    // Test: Void task exception in vector
    void
    testVoidExceptionInVector()
    {
        int dispatch_count = 0;
        test_executor ex(dispatch_count);
        bool caught_exception = false;
        std::string error_msg;

        std::vector<task<void>> tasks;
        tasks.push_back(void_throws_exception("void vector error"));
        tasks.push_back(void_task());

        run_async(ex,
            [](std::size_t) {},
            [&](std::exception_ptr ep) {
                try {
                    std::rethrow_exception(ep);
                } catch (test_exception const& e) {
                    caught_exception = true;
                    error_msg = e.what();
                }
            })(when_any(std::move(tasks)));

        BOOST_TEST(caught_exception);
        BOOST_TEST_EQ(error_msg, "void vector error");
    }

    //----------------------------------------------------------
    // Stop token propagation tests
    //----------------------------------------------------------

    // Test: All tasks complete for cleanup (vector)
    void
    testAllTasksCompleteForCleanupVector()
    {
        int dispatch_count = 0;
        test_executor ex(dispatch_count);
        std::atomic<int> completion_count{0};
        bool completed = false;

        auto counting_task = [&](int value) -> task<int> {
            ++completion_count;
            co_return value;
        };

        std::vector<task<int>> tasks;
        tasks.push_back(counting_task(1));
        tasks.push_back(counting_task(2));
        tasks.push_back(counting_task(3));
        tasks.push_back(counting_task(4));

        run_async(ex,
            [&](std::pair<std::size_t, int>) {
                completed = true;
            },
            [](std::exception_ptr) {})(
            when_any(std::move(tasks)));

        BOOST_TEST(completed);
        // All four tasks must complete for proper cleanup
        BOOST_TEST_EQ(completion_count.load(), 4);
    }

    //----------------------------------------------------------
    // Long-lived task cancellation tests (vector)
    //----------------------------------------------------------

    // Test: Long-lived tasks cancelled on winner (vector)
    void
    testLongLivedTasksCancelledVector()
    {
        std::queue<coro> work_queue;
        queuing_executor ex(work_queue);

        std::atomic<int> cancelled_count{0};
        std::atomic<int> completed_normally_count{0};
        bool when_any_completed = false;
        std::size_t winner_index = 999;
        int winner_value = 0;

        auto fast_task = [&]() -> task<int> {
            ++completed_normally_count;
            co_return 42;
        };

        auto slow_task = [&](int id, int steps) -> task<int> {
            for (int i = 0; i < steps; ++i) {
                auto token = co_await this_coro::stop_token;
                if (token.stop_requested()) {
                    ++cancelled_count;
                    co_return -1;
                }
                co_await yield_awaitable{};
            }
            ++completed_normally_count;
            co_return id;
        };

        std::vector<task<int>> tasks;
        tasks.push_back(fast_task());
        tasks.push_back(slow_task(100, 10));
        tasks.push_back(slow_task(200, 10));

        run_async(ex,
            [&](std::pair<std::size_t, int> r) {
                when_any_completed = true;
                winner_index = r.first;
                winner_value = r.second;
            },
            [](std::exception_ptr) {})(
            when_any(std::move(tasks)));

        while (!work_queue.empty()) {
            auto h = work_queue.front();
            work_queue.pop();
            h.resume();
        }

        BOOST_TEST(when_any_completed);
        BOOST_TEST_EQ(winner_index, 0u);
        BOOST_TEST_EQ(winner_value, 42);
        BOOST_TEST_EQ(completed_normally_count.load(), 1);
        BOOST_TEST_EQ(cancelled_count.load(), 2);
    }

    //----------------------------------------------------------
    // Large vector tests
    //----------------------------------------------------------

    // Test: Many tasks in vector
    void
    testManyTasksVector()
    {
        int dispatch_count = 0;
        test_executor ex(dispatch_count);
        bool completed = false;
        std::size_t winner_index = 999;
        int result_value = 0;

        std::vector<task<int>> tasks;
        for (int i = 1; i <= 20; ++i)
            tasks.push_back(returns_int(i));

        run_async(ex,
            [&](std::pair<std::size_t, int> r) {
                completed = true;
                winner_index = r.first;
                result_value = r.second;
            },
            [](std::exception_ptr) {})(
            when_any(std::move(tasks)));

        BOOST_TEST(completed);
        BOOST_TEST(winner_index < 20);
        // Verify correct index-to-value mapping (index 0 -> value 1, etc.)
        BOOST_TEST_EQ(result_value, static_cast<int>(winner_index + 1));
    }

    //----------------------------------------------------------
    // Nested combinator tests
    //----------------------------------------------------------

    // Test: Nested when_any with vectors
    void
    testNestedWhenAnyVector()
    {
        int dispatch_count = 0;
        test_executor ex(dispatch_count);
        bool completed = false;
        int result = 0;

        auto inner = []() -> task<int> {
            std::vector<task<int>> tasks;
            tasks.push_back(returns_int(10));
            tasks.push_back(returns_int(20));
            auto [idx, res] = co_await when_any(std::move(tasks));
            co_return res;
        };

        std::vector<task<int>> outer_tasks;
        outer_tasks.push_back(inner());
        outer_tasks.push_back(inner());

        run_async(ex,
            [&](std::pair<std::size_t, int> r) {
                completed = true;
                result = r.second;
            },
            [](std::exception_ptr) {})(
            when_any(std::move(outer_tasks)));

        BOOST_TEST(completed);
        BOOST_TEST(result == 10 || result == 20);
    }

    // Test: when_any vector inside when_all
    void
    testWhenAnyVectorInsideWhenAll()
    {
        int dispatch_count = 0;
        test_executor ex(dispatch_count);
        bool completed = false;

        auto race = []() -> task<int> {
            std::vector<task<int>> tasks;
            tasks.push_back(returns_int(1));
            tasks.push_back(returns_int(2));
            auto [idx, res] = co_await when_any(std::move(tasks));
            co_return res;
        };

        run_async(ex,
            [&](std::tuple<int, int> t) {
                auto [a, b] = t;
                completed = true;
                BOOST_TEST((a == 1 || a == 2));
                BOOST_TEST((b == 1 || b == 2));
            },
            [](std::exception_ptr) {})(
            when_all(race(), race()));

        BOOST_TEST(completed);
    }

    //----------------------------------------------------------
    // Mixed variadic and vector tests
    //----------------------------------------------------------

    // Test: Mix variadic and vector when_any
    void
    testMixedVariadicAndVector()
    {
        int dispatch_count = 0;
        test_executor ex(dispatch_count);
        bool completed = false;
        std::size_t outer_winner = 999;

        auto variadic_race = []() -> task<int> {
            auto [idx, res] = co_await when_any(returns_int(1), returns_int(2));
            co_return std::get<int>(res);
        };

        auto vector_race = []() -> task<int> {
            std::vector<task<int>> tasks;
            tasks.push_back(returns_int(3));
            tasks.push_back(returns_int(4));
            auto [idx, res] = co_await when_any(std::move(tasks));
            co_return res;
        };

        run_async(ex,
            [&](auto r) {
                completed = true;
                outer_winner = r.first;
                auto result = std::get<int>(r.second);
                if (outer_winner == 0)
                    BOOST_TEST((result == 1 || result == 2));
                else
                    BOOST_TEST((result == 3 || result == 4));
            },
            [](std::exception_ptr) {})(
            when_any(variadic_race(), vector_race()));

        BOOST_TEST(completed);
    }

    void
    run()
    {
        // Basic functionality
        testSingleTaskVector();
        testMultipleTasksVector();
        testEmptyVectorThrows();
        testVoidTasksVector();

        // Exception handling
        testExceptionInVector();
        testExceptionWinsRaceVector();
        testVoidExceptionInVector();

        // Stop token propagation
        testAllTasksCompleteForCleanupVector();

        // Long-lived task cancellation
        testLongLivedTasksCancelledVector();

        // Large vectors
        testManyTasksVector();

        // Nested combinators
        testNestedWhenAnyVector();
        testWhenAnyVectorInsideWhenAll();

        // Mixed variadic and vector
        testMixedVariadicAndVector();
    }
};

TEST_SUITE(
    when_any_vector_test,
    "boost.capy.when_any_vector");

} // capy
} // boost
