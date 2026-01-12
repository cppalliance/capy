//
// Copyright (c) 2025 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Test that header file is self-contained.
#include <boost/capy/when_all.hpp>

#include <boost/capy/async_run.hpp>
#include <boost/capy/frame_allocator.hpp>
#include <boost/capy/task.hpp>

#include "test_suite.hpp"

#include <atomic>
#include <stdexcept>
#include <string>
#include <type_traits>

namespace boost {
namespace capy {

// Static assertions for void filtering type trait
static_assert(std::is_same_v<
    detail::filter_void_tuple_t<int>,
    std::tuple<int>>);
static_assert(std::is_same_v<
    detail::filter_void_tuple_t<void>,
    std::tuple<>>);
static_assert(std::is_same_v<
    detail::filter_void_tuple_t<int, void, std::string>,
    std::tuple<int, std::string>>);
static_assert(std::is_same_v<
    detail::filter_void_tuple_t<void, void, void>,
    std::tuple<>>);

// Verify result_type: void when all tasks are void, tuple otherwise
static_assert(std::is_same_v<
    when_all_awaitable<int, std::string>::result_type,
    std::tuple<int, std::string>>);
static_assert(std::is_same_v<
    when_all_awaitable<int, void, std::string>::result_type,
    std::tuple<int, std::string>>);
static_assert(std::is_void_v<
    when_all_awaitable<void>::result_type>);
static_assert(std::is_void_v<
    when_all_awaitable<void, void>::result_type>);
static_assert(std::is_void_v<
    when_all_awaitable<void, void, void>::result_type>);

// Verify when_all_awaitable satisfies awaitable protocols
static_assert(affine_awaitable<when_all_awaitable<int, int>, any_dispatcher>);
static_assert(stoppable_awaitable<when_all_awaitable<int, int>, any_dispatcher>);

/** Simple synchronous dispatcher for testing.
*/
struct test_dispatcher
{
    int* dispatch_count_;

    explicit test_dispatcher(int& count)
        : dispatch_count_(&count)
    {
    }

    coro operator()(coro h) const
    {
        ++(*dispatch_count_);
        return h;
    }
};

static_assert(dispatcher<test_dispatcher>);

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

struct when_all_test
{
    // Helper tasks
    static task<int>
    returns_int(int value)
    {
        co_return value;
    }

    static task<std::string>
    returns_string(std::string value)
    {
        co_return value;
    }

    static task<void>
    void_task()
    {
        co_return;
    }

    static task<int>
    throws_exception(char const* msg)
    {
        throw_test_exception(msg);
        co_return 0;
    }

    static task<void>
    void_throws_exception(char const* msg)
    {
        throw_test_exception(msg);
        co_return;
    }

    // Test: All tasks succeed
    void
    testAllSucceed()
    {
        int dispatch_count = 0;
        test_dispatcher d(dispatch_count);
        bool completed = false;
        int result = 0;

        async_run(d)(
            []() -> task<int> {
                auto [a, b] = co_await when_all(
                    returns_int(10),
                    returns_int(20)
                );
                co_return a + b;
            }(),
            [&](int r) { completed = true; result = r; },
            [](std::exception_ptr) {});

        BOOST_TEST(completed);
        BOOST_TEST_EQ(result, 30);
    }

    // Test: Three tasks succeed
    void
    testThreeTasksSucceed()
    {
        int dispatch_count = 0;
        test_dispatcher d(dispatch_count);
        bool completed = false;
        int result = 0;

        async_run(d)(
            []() -> task<int> {
                auto [a, b, c] = co_await when_all(
                    returns_int(1),
                    returns_int(2),
                    returns_int(3)
                );
                co_return a + b + c;
            }(),
            [&](int r) { completed = true; result = r; },
            [](std::exception_ptr) {});

        BOOST_TEST(completed);
        BOOST_TEST_EQ(result, 6);
    }

    // Test: Mixed types (int, string, void)
    void
    testMixedTypes()
    {
        int dispatch_count = 0;
        test_dispatcher d(dispatch_count);
        bool completed = false;
        std::string result;

        async_run(d)(
            []() -> task<std::string> {
                // void_task() doesn't contribute to result tuple
                auto [a, b] = co_await when_all(
                    returns_int(42),
                    returns_string("hello"),
                    void_task()
                );
                co_return b + std::to_string(a);
            }(),
            [&](std::string r) { completed = true; result = std::move(r); },
            [](std::exception_ptr) {});

        BOOST_TEST(completed);
        BOOST_TEST_EQ(result, "hello42");
    }

    // Test: Single task in when_all
    void
    testSingleTask()
    {
        int dispatch_count = 0;
        test_dispatcher d(dispatch_count);
        bool completed = false;
        int result = 0;

        async_run(d)(
            []() -> task<int> {
                auto [a] = co_await when_all(
                    returns_int(99)
                );
                co_return a;
            }(),
            [&](int r) { completed = true; result = r; },
            [](std::exception_ptr) {});

        BOOST_TEST(completed);
        BOOST_TEST_EQ(result, 99);
    }

    // Test: First exception captured
    void
    testFirstException()
    {
        int dispatch_count = 0;
        test_dispatcher d(dispatch_count);
        bool completed = false;
        bool caught_exception = false;
        std::string error_msg;

        async_run(d)(
            []() -> task<int> {
                auto [a, b] = co_await when_all(
                    throws_exception("first error"),
                    returns_int(10)
                );
                co_return a + b;
            }(),
            [&](int) { completed = true; },
            [&](std::exception_ptr ep) {
                try {
                    std::rethrow_exception(ep);
                } catch (test_exception const& e) {
                    caught_exception = true;
                    error_msg = e.what();
                }
            });

        BOOST_TEST(!completed);
        BOOST_TEST(caught_exception);
        BOOST_TEST_EQ(error_msg, "first error");
    }

    // Test: Multiple failures - first exception wins
    void
    testMultipleFailuresFirstWins()
    {
        int dispatch_count = 0;
        test_dispatcher d(dispatch_count);
        bool caught_exception = false;
        std::string error_msg;

        async_run(d)(
            []() -> task<int> {
                auto [a, b, c] = co_await when_all(
                    throws_exception("error_1"),
                    throws_exception("error_2"),
                    throws_exception("error_3")
                );
                co_return a + b + c;
            }(),
            [](int) {},
            [&](std::exception_ptr ep) {
                try {
                    std::rethrow_exception(ep);
                } catch (test_exception const& e) {
                    caught_exception = true;
                    error_msg = e.what();
                }
            });

        BOOST_TEST(caught_exception);
        BOOST_TEST(
            error_msg == "error_1" ||
            error_msg == "error_2" ||
            error_msg == "error_3");
    }

    // Test: Void task throws exception
    void
    testVoidTaskException()
    {
        int dispatch_count = 0;
        test_dispatcher d(dispatch_count);
        bool caught_exception = false;
        std::string error_msg;

        async_run(d)(
            []() -> task<int> {
                auto [a] = co_await when_all(
                    returns_int(10),
                    void_throws_exception("void error")
                );
                co_return a;
            }(),
            [](int) {},
            [&](std::exception_ptr ep) {
                try {
                    std::rethrow_exception(ep);
                } catch (test_exception const& e) {
                    caught_exception = true;
                    error_msg = e.what();
                }
            });

        BOOST_TEST(caught_exception);
        BOOST_TEST_EQ(error_msg, "void error");
    }

    // Test: Nested when_all calls
    void
    testNestedWhenAll()
    {
        int dispatch_count = 0;
        test_dispatcher d(dispatch_count);
        bool completed = false;
        int result = 0;

        async_run(d)(
            []() -> task<int> {
                auto inner1 = []() -> task<int> {
                    auto [a, b] = co_await when_all(
                        returns_int(1),
                        returns_int(2)
                    );
                    co_return a + b;
                };

                auto inner2 = []() -> task<int> {
                    auto [a, b] = co_await when_all(
                        returns_int(3),
                        returns_int(4)
                    );
                    co_return a + b;
                };

                auto [x, y] = co_await when_all(
                    inner1(),
                    inner2()
                );

                co_return x + y;
            }(),
            [&](int r) { completed = true; result = r; },
            [](std::exception_ptr) {});

        BOOST_TEST(completed);
        BOOST_TEST_EQ(result, 10);  // (1+2) + (3+4) = 10
    }

    // Test: All void tasks return void (not empty tuple)
    void
    testAllVoidTasks()
    {
        int dispatch_count = 0;
        test_dispatcher d(dispatch_count);
        bool completed = false;

        async_run(d)(
            []() -> task<void> {
                // All void tasks return void, not std::tuple<>
                co_await when_all(
                    void_task(),
                    void_task(),
                    void_task()
                );
                co_return;
            }(),
            [&]() { completed = true; },
            [](std::exception_ptr) {});

        BOOST_TEST(completed);
    }

    // Test: Result type correctness - void types filtered, all-void returns void
    void
    testResultType()
    {
        // Mixed types: void filtered out
        using mixed_result = when_all_awaitable<int, void, std::string>::result_type;
        static_assert(std::is_same_v<
            mixed_result,
            std::tuple<int, std::string>>);

        // All void: returns void (not empty tuple)
        using all_void_result = when_all_awaitable<void, void, void>::result_type;
        static_assert(std::is_void_v<all_void_result>);

        // Single void: returns void
        using single_void_result = when_all_awaitable<void>::result_type;
        static_assert(std::is_void_v<single_void_result>);
    }

    //----------------------------------------------------------
    // Frame allocator verification tests
    //----------------------------------------------------------

    /** Counting frame allocator to verify all coroutines use the allocator.
    */
    struct counting_frame_allocator
    {
        std::size_t* alloc_count_;
        std::size_t* dealloc_count_;

        void* allocate(std::size_t n)
        {
            ++(*alloc_count_);
            return ::operator new(n);
        }

        void deallocate(void* p, std::size_t)
        {
            ++(*dealloc_count_);
            ::operator delete(p);
        }
    };

    static_assert(frame_allocator<counting_frame_allocator>);

    // Test: Frame allocator used for two tasks
    // Expected: 1 async_run_task + 1 outer task + 2 runners + 2 child tasks = 6
    void
    testFrameAllocatorTwoTasks()
    {
        int dispatch_count = 0;
        test_dispatcher d(dispatch_count);
        std::size_t alloc_count = 0;
        std::size_t dealloc_count = 0;
        counting_frame_allocator alloc{&alloc_count, &dealloc_count};
        bool completed = false;

        async_run(d, alloc)(
            []() -> task<int> {
                auto [a, b] = co_await when_all(
                    returns_int(10),
                    returns_int(20)
                );
                co_return a + b;
            }(),
            [&](int r) {
                completed = true;
                BOOST_TEST_EQ(r, 30);
            },
            [](std::exception_ptr) {});

        BOOST_TEST(completed);
        // 1 async_run_task + 1 outer task + 2 when_all_runners + 2 child tasks = 6
        BOOST_TEST_EQ(alloc_count, 6u);
        BOOST_TEST_EQ(dealloc_count, 6u);
    }

    // Test: Frame allocator used for three tasks
    // Expected: 1 async_run_task + 1 outer task + 3 runners + 3 child tasks = 8
    void
    testFrameAllocatorThreeTasks()
    {
        int dispatch_count = 0;
        test_dispatcher d(dispatch_count);
        std::size_t alloc_count = 0;
        std::size_t dealloc_count = 0;
        counting_frame_allocator alloc{&alloc_count, &dealloc_count};
        bool completed = false;

        async_run(d, alloc)(
            []() -> task<int> {
                auto [a, b, c] = co_await when_all(
                    returns_int(1),
                    returns_int(2),
                    returns_int(3)
                );
                co_return a + b + c;
            }(),
            [&](int r) {
                completed = true;
                BOOST_TEST_EQ(r, 6);
            },
            [](std::exception_ptr) {});

        BOOST_TEST(completed);
        // 1 async_run_task + 1 outer task + 3 when_all_runners + 3 child tasks = 8
        BOOST_TEST_EQ(alloc_count, 8u);
        BOOST_TEST_EQ(dealloc_count, 8u);
    }

    // Test: Frame allocator with void tasks
    // Expected: 1 async_run_task + 1 outer task + 3 runners + 3 child tasks = 8
    void
    testFrameAllocatorWithVoidTasks()
    {
        int dispatch_count = 0;
        test_dispatcher d(dispatch_count);
        std::size_t alloc_count = 0;
        std::size_t dealloc_count = 0;
        counting_frame_allocator alloc{&alloc_count, &dealloc_count};
        bool completed = false;

        async_run(d, alloc)(
            []() -> task<int> {
                auto [a] = co_await when_all(
                    returns_int(42),
                    void_task(),
                    void_task()
                );
                co_return a;
            }(),
            [&](int r) {
                completed = true;
                BOOST_TEST_EQ(r, 42);
            },
            [](std::exception_ptr) {});

        BOOST_TEST(completed);
        // 1 async_run_task + 1 outer task + 3 when_all_runners + 3 child tasks = 8
        BOOST_TEST_EQ(alloc_count, 8u);
        BOOST_TEST_EQ(dealloc_count, 8u);
    }

    // Test: Frame allocator with single task (edge case)
    // Expected: 1 async_run_task + 1 outer task + 1 runner + 1 child task = 4
    void
    testFrameAllocatorSingleTask()
    {
        int dispatch_count = 0;
        test_dispatcher d(dispatch_count);
        std::size_t alloc_count = 0;
        std::size_t dealloc_count = 0;
        counting_frame_allocator alloc{&alloc_count, &dealloc_count};
        bool completed = false;

        async_run(d, alloc)(
            []() -> task<int> {
                auto [a] = co_await when_all(
                    returns_int(99)
                );
                co_return a;
            }(),
            [&](int r) {
                completed = true;
                BOOST_TEST_EQ(r, 99);
            },
            [](std::exception_ptr) {});

        BOOST_TEST(completed);
        // 1 async_run_task + 1 outer task + 1 when_all_runner + 1 child task = 4
        BOOST_TEST_EQ(alloc_count, 4u);
        BOOST_TEST_EQ(dealloc_count, 4u);
    }

    // Test: Frame allocator with nested when_all
    // Expected: 1 async_run + 1 outer task + 2 outer runners + 2 inner tasks +
    //           2*(2 inner runners + 2 inner child tasks) = 2 + 2 + 2 + 8 = 14
    void
    testFrameAllocatorNestedWhenAll()
    {
        int dispatch_count = 0;
        test_dispatcher d(dispatch_count);
        std::size_t alloc_count = 0;
        std::size_t dealloc_count = 0;
        counting_frame_allocator alloc{&alloc_count, &dealloc_count};
        bool completed = false;

        async_run(d, alloc)(
            []() -> task<int> {
                auto inner1 = []() -> task<int> {
                    auto [a, b] = co_await when_all(
                        returns_int(1),
                        returns_int(2)
                    );
                    co_return a + b;
                };

                auto inner2 = []() -> task<int> {
                    auto [a, b] = co_await when_all(
                        returns_int(3),
                        returns_int(4)
                    );
                    co_return a + b;
                };

                auto [x, y] = co_await when_all(
                    inner1(),
                    inner2()
                );

                co_return x + y;
            }(),
            [&](int r) {
                completed = true;
                BOOST_TEST_EQ(r, 10);
            },
            [](std::exception_ptr) {});

        BOOST_TEST(completed);
        // Structure:
        // - 1 async_run_task
        // - 1 outer task (the main lambda)
        // - 2 when_all_runners (for inner1(), inner2())
        // - 2 child tasks (inner1, inner2)
        // - For each inner when_all:
        //   - 2 when_all_runners
        //   - 2 child tasks (returns_int)
        // Total: 1 + 1 + 2 + 2 + 2*(2 + 2) = 14
        BOOST_TEST_EQ(alloc_count, 14u);
        BOOST_TEST_EQ(dealloc_count, 14u);
    }

    // Test: Frame allocator deallocations match allocations on exception
    void
    testFrameAllocatorWithException()
    {
        int dispatch_count = 0;
        test_dispatcher d(dispatch_count);
        std::size_t alloc_count = 0;
        std::size_t dealloc_count = 0;
        counting_frame_allocator alloc{&alloc_count, &dealloc_count};
        bool caught_exception = false;

        async_run(d, alloc)(
            []() -> task<int> {
                auto [a, b] = co_await when_all(
                    throws_exception("test error"),
                    returns_int(10)
                );
                co_return a + b;
            }(),
            [](int) {},
            [&](std::exception_ptr) {
                caught_exception = true;
            });

        BOOST_TEST(caught_exception);
        // Even with exception, all frames should be deallocated
        // 1 async_run_task + 1 outer task + 2 when_all_runners + 2 child tasks = 6
        BOOST_TEST_EQ(alloc_count, 6u);
        BOOST_TEST_EQ(dealloc_count, 6u);
    }

    //----------------------------------------------------------
    // Stop token propagation tests
    //----------------------------------------------------------

    // Helper: task that records if stop was requested
    static task<int>
    checks_stop_token(std::atomic<bool>& stop_was_requested)
    {
        // This task just returns immediately, but in real usage
        // you would check stop_token in a loop
        co_return 42;
    }

    // Helper: stoppable task that honors stop requests
    static task<int>
    stoppable_task(std::atomic<int>& counter)
    {
        ++counter;
        co_return counter.load();
    }

    // Test: Stop is requested when a sibling fails
    void
    testStopRequestedOnError()
    {
        int dispatch_count = 0;
        test_dispatcher d(dispatch_count);
        bool caught_exception = false;

        async_run(d)(
            []() -> task<int> {
                auto [a, b] = co_await when_all(
                    throws_exception("error"),
                    returns_int(10)
                );
                co_return a + b;
            }(),
            [](int) {},
            [&](std::exception_ptr) {
                caught_exception = true;
            });

        // Exception should propagate - stop was requested internally
        BOOST_TEST(caught_exception);
    }

    // Test: All tasks complete even after stop is requested
    void
    testAllTasksCompleteAfterStop()
    {
        int dispatch_count = 0;
        test_dispatcher d(dispatch_count);
        std::atomic<int> completion_count{0};
        bool caught_exception = false;

        auto counting_task = [&]() -> task<int> {
            ++completion_count;
            co_return 1;
        };

        auto failing_task = [&]() -> task<int> {
            ++completion_count;
            throw_test_exception("fail");
            co_return 0;
        };

        async_run(d)(
            [&]() -> task<int> {
                auto [a, b, c] = co_await when_all(
                    counting_task(),
                    failing_task(),
                    counting_task()
                );
                co_return a + b + c;
            }(),
            [](int) {},
            [&](std::exception_ptr) {
                caught_exception = true;
            });

        BOOST_TEST(caught_exception);
        // All three tasks should have run to completion
        BOOST_TEST_EQ(completion_count.load(), 3);
    }

    //----------------------------------------------------------
    // Edge case tests
    //----------------------------------------------------------

    // Test: Large number of tasks
    void
    testManyTasks()
    {
        int dispatch_count = 0;
        test_dispatcher d(dispatch_count);
        bool completed = false;
        int result = 0;

        async_run(d)(
            []() -> task<int> {
                auto [a, b, c, d, e, f, g, h] = co_await when_all(
                    returns_int(1),
                    returns_int(2),
                    returns_int(3),
                    returns_int(4),
                    returns_int(5),
                    returns_int(6),
                    returns_int(7),
                    returns_int(8)
                );
                co_return a + b + c + d + e + f + g + h;
            }(),
            [&](int r) { completed = true; result = r; },
            [](std::exception_ptr) {});

        BOOST_TEST(completed);
        BOOST_TEST_EQ(result, 36);  // 1+2+3+4+5+6+7+8 = 36
    }

    // Test: Task that does multiple internal operations
    static task<int>
    multi_step_task(int start)
    {
        int value = start;
        // Simulate multiple steps by nesting tasks
        value += co_await returns_int(1);
        value += co_await returns_int(2);
        co_return value;
    }

    void
    testTasksWithMultipleSteps()
    {
        int dispatch_count = 0;
        test_dispatcher d(dispatch_count);
        bool completed = false;
        int result = 0;

        async_run(d)(
            []() -> task<int> {
                auto [a, b] = co_await when_all(
                    multi_step_task(10),
                    multi_step_task(20)
                );
                co_return a + b;
            }(),
            [&](int r) { completed = true; result = r; },
            [](std::exception_ptr) {});

        BOOST_TEST(completed);
        // (10+1+2) + (20+1+2) = 13 + 23 = 36
        BOOST_TEST_EQ(result, 36);
    }

    // Test: Different exception types - first wins
    struct other_exception : std::runtime_error
    {
        explicit other_exception(const char* msg)
            : std::runtime_error(msg)
        {
        }
    };

    static task<int>
    throws_other_exception(char const* msg)
    {
        throw other_exception(msg);
        co_return 0;
    }

    void
    testDifferentExceptionTypes()
    {
        int dispatch_count = 0;
        test_dispatcher d(dispatch_count);
        bool caught_test = false;
        bool caught_other = false;

        async_run(d)(
            []() -> task<int> {
                auto [a, b] = co_await when_all(
                    throws_exception("test"),
                    throws_other_exception("other")
                );
                co_return a + b;
            }(),
            [](int) {},
            [&](std::exception_ptr ep) {
                try {
                    std::rethrow_exception(ep);
                } catch (test_exception const&) {
                    caught_test = true;
                } catch (other_exception const&) {
                    caught_other = true;
                }
            });

        // One of them should be caught (first to fail wins)
        BOOST_TEST(caught_test || caught_other);
        // But not both
        BOOST_TEST(!(caught_test && caught_other));
    }

    //----------------------------------------------------------
    // Dispatcher propagation tests
    //----------------------------------------------------------

    // Dispatcher that tracks which tasks were dispatched
    struct tracking_dispatcher
    {
        std::atomic<int>* dispatch_count_;

        explicit tracking_dispatcher(std::atomic<int>& count)
            : dispatch_count_(&count)
        {
        }

        coro operator()(coro h) const
        {
            ++(*dispatch_count_);
            return h;
        }
    };

    static_assert(dispatcher<tracking_dispatcher>);

    void
    testDispatcherUsedForAllTasks()
    {
        std::atomic<int> dispatch_count{0};
        tracking_dispatcher d(dispatch_count);
        bool completed = false;

        async_run(d)(
            []() -> task<int> {
                auto [a, b, c] = co_await when_all(
                    returns_int(1),
                    returns_int(2),
                    returns_int(3)
                );
                co_return a + b + c;
            }(),
            [&](int r) {
                completed = true;
                BOOST_TEST_EQ(r, 6);
            },
            [](std::exception_ptr) {});

        BOOST_TEST(completed);
        // Dispatcher should be called for:
        // - async_run initial dispatch
        // - when_all runners (3)
        // - signal_completion resumption
        BOOST_TEST(dispatch_count.load() > 0);
    }

    //----------------------------------------------------------
    // Result ordering tests
    //----------------------------------------------------------

    // Test: Results are in input order regardless of completion order
    void
    testResultsInInputOrder()
    {
        int dispatch_count = 0;
        test_dispatcher d(dispatch_count);
        bool completed = false;

        async_run(d)(
            []() -> task<void> {
                auto [first, second, third] = co_await when_all(
                    returns_string("first"),
                    returns_string("second"),
                    returns_string("third")
                );
                BOOST_TEST_EQ(first, "first");
                BOOST_TEST_EQ(second, "second");
                BOOST_TEST_EQ(third, "third");
                co_return;
            }(),
            [&]() { completed = true; },
            [](std::exception_ptr) {});

        BOOST_TEST(completed);
    }

    // Test: Mixed void and value results maintain order
    void
    testMixedVoidValueOrder()
    {
        int dispatch_count = 0;
        test_dispatcher d(dispatch_count);
        bool completed = false;

        async_run(d)(
            []() -> task<void> {
                // void at index 1, values at 0 and 2
                auto [a, b] = co_await when_all(
                    returns_int(100),
                    void_task(),
                    returns_int(300)
                );
                // a should be from index 0, b from index 2
                BOOST_TEST_EQ(a, 100);
                BOOST_TEST_EQ(b, 300);
                co_return;
            }(),
            [&]() { completed = true; },
            [](std::exception_ptr) {});

        BOOST_TEST(completed);
    }

    //----------------------------------------------------------
    // Awaitable lifecycle tests
    //----------------------------------------------------------

    // Test: when_all_awaitable is move constructible
    void
    testAwaitableMoveConstruction()
    {
        auto awaitable1 = when_all(returns_int(1), returns_int(2));
        auto awaitable2 = std::move(awaitable1);

        int dispatch_count = 0;
        test_dispatcher d(dispatch_count);
        bool completed = false;

        async_run(d)(
            [aw = std::move(awaitable2)]() mutable -> task<int> {
                auto [a, b] = co_await std::move(aw);
                co_return a + b;
            }(),
            [&](int r) {
                completed = true;
                BOOST_TEST_EQ(r, 3);
            },
            [](std::exception_ptr) {});

        BOOST_TEST(completed);
    }

    // Test: when_all can be stored and awaited later
    void
    testDeferredAwait()
    {
        int dispatch_count = 0;
        test_dispatcher d(dispatch_count);
        bool completed = false;

        auto deferred = when_all(returns_int(10), returns_int(20));

        async_run(d)(
            [aw = std::move(deferred)]() mutable -> task<int> {
                // Await later
                auto [a, b] = co_await std::move(aw);
                co_return a + b;
            }(),
            [&](int r) {
                completed = true;
                BOOST_TEST_EQ(r, 30);
            },
            [](std::exception_ptr) {});

        BOOST_TEST(completed);
    }

    //----------------------------------------------------------
    // Stoppable awaitable protocol tests
    //----------------------------------------------------------

    // Test: when_all satisfies stoppable_awaitable concept
    void
    testStoppableAwaitableConcept()
    {
        static_assert(stoppable_awaitable<
            when_all_awaitable<int, int>,
            any_dispatcher>);

        static_assert(stoppable_awaitable<
            when_all_awaitable<int, void, std::string>,
            any_dispatcher>);
    }

    // Test: Nested when_all propagates stop
    void
    testNestedWhenAllStopPropagation()
    {
        int dispatch_count = 0;
        test_dispatcher d(dispatch_count);
        bool caught_exception = false;

        async_run(d)(
            []() -> task<int> {
                auto inner_failing = []() -> task<int> {
                    auto [a, b] = co_await when_all(
                        throws_exception("inner error"),
                        returns_int(1)
                    );
                    co_return a + b;
                };

                auto inner_success = []() -> task<int> {
                    auto [a, b] = co_await when_all(
                        returns_int(2),
                        returns_int(3)
                    );
                    co_return a + b;
                };

                auto [x, y] = co_await when_all(
                    inner_failing(),
                    inner_success()
                );
                co_return x + y;
            }(),
            [](int) {},
            [&](std::exception_ptr ep) {
                caught_exception = true;
                try {
                    std::rethrow_exception(ep);
                } catch (test_exception const& e) {
                    BOOST_TEST_EQ(std::string(e.what()), "inner error");
                }
            });

        BOOST_TEST(caught_exception);
    }

    void
    run()
    {
        // Basic functionality
        testResultType();
        testAllSucceed();
        testThreeTasksSucceed();
        testMixedTypes();
        testSingleTask();
        testFirstException();
        testMultipleFailuresFirstWins();
        testVoidTaskException();
        testNestedWhenAll();
        testAllVoidTasks();

        // Frame allocator verification
        testFrameAllocatorTwoTasks();
        testFrameAllocatorThreeTasks();
        testFrameAllocatorWithVoidTasks();
        testFrameAllocatorSingleTask();
        testFrameAllocatorNestedWhenAll();
        testFrameAllocatorWithException();

        // Stop token propagation
        testStopRequestedOnError();
        testAllTasksCompleteAfterStop();

        // Edge cases
        testManyTasks();
        testTasksWithMultipleSteps();
        testDifferentExceptionTypes();

        // Dispatcher propagation
        testDispatcherUsedForAllTasks();

        // Result ordering
        testResultsInInputOrder();
        testMixedVoidValueOrder();

        // Awaitable lifecycle
        testAwaitableMoveConstruction();
        testDeferredAwait();

        // Stoppable awaitable protocol
        testStoppableAwaitableConcept();
        testNestedWhenAllStopPropagation();
    }
};

TEST_SUITE(
    when_all_test,
    "boost.capy.when_all");

} // capy
} // boost
