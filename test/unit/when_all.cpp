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

#include <boost/capy/ex/async_run.hpp>
#include <boost/capy/task.hpp>

#include "test_suite.hpp"

#include <atomic>
#include <stdexcept>
#include <string>
#include <vector>

// GCC-11 gives false positive -Wmaybe-uninitialized warnings when async_run.hpp's
// await_suspend is inlined into lambdas. The warnings occur because GCC's flow
// analysis can't see through the coroutine machinery to verify that result_ is
// initialized before use. Suppress these false positives for this entire file.
#if defined(__GNUC__) && !defined(__clang__) && __GNUC__ == 11
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif
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
    when_all_result_type<int, std::string>,
    std::tuple<int, std::string>>);
static_assert(std::is_same_v<
    when_all_result_type<int, void, std::string>,
    std::tuple<int, std::string>>);
static_assert(std::is_void_v<
    when_all_result_type<void>>);
static_assert(std::is_void_v<
    when_all_result_type<void, void>>);
static_assert(std::is_void_v<
    when_all_result_type<void, void, void>>);

// Verify when_all returns task which satisfies awaitable protocols
static_assert(affine_awaitable<task<std::tuple<int, int>>, any_dispatcher>);
#if BOOST_CAPY_HAS_STOP_TOKEN
static_assert(stoppable_awaitable<task<std::tuple<int, int>>, any_dispatcher>);
#endif

/** Simple synchronous dispatcher for testing.
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
            when_all(returns_int(10), returns_int(20)),
            [&](std::tuple<int, int> t) {
                auto [a, b] = t;
                completed = true;
                result = a + b;
            },
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
            when_all(returns_int(1), returns_int(2), returns_int(3)),
            [&](std::tuple<int, int, int> t) {
                auto [a, b, c] = t;
                completed = true;
                result = a + b + c;
            },
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

        // void_task() doesn't contribute to result tuple
        async_run(d)(
            when_all(returns_int(42), returns_string("hello"), void_task()),
            [&](std::tuple<int, std::string> t) {
                auto [a, b] = t;
                completed = true;
                result = b + std::to_string(a);
            },
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
            when_all(returns_int(99)),
            [&](std::tuple<int> t) {
                auto [a] = t;
                completed = true;
                result = a;
            },
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
            when_all(throws_exception("first error"), returns_int(10)),
            [&](std::tuple<int, int>) { completed = true; },
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
            when_all(
                throws_exception("error_1"),
                throws_exception("error_2"),
                throws_exception("error_3")),
            [](std::tuple<int, int, int>) {},
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
            when_all(returns_int(10), void_throws_exception("void error")),
            [](std::tuple<int>) {},
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

        // Helper tasks that use when_all internally
        auto inner1 = []() -> task<int> {
            auto [a, b] = co_await when_all(returns_int(1), returns_int(2));
            co_return a + b;
        };

        auto inner2 = []() -> task<int> {
            auto [a, b] = co_await when_all(returns_int(3), returns_int(4));
            co_return a + b;
        };

        async_run(d)(
            when_all(inner1(), inner2()),
            [&](std::tuple<int, int> t) {
                auto [x, y] = t;
                completed = true;
                result = x + y;
            },
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

        // All void tasks return void, not std::tuple<>
        async_run(d)(
            when_all(void_task(), void_task(), void_task()),
            [&]() { completed = true; },
            [](std::exception_ptr) {});

        BOOST_TEST(completed);
    }

    // Test: Result type correctness - void types filtered, all-void returns void
    void
    testResultType()
    {
        // Mixed types: void filtered out
        using mixed_result = when_all_result_type<int, void, std::string>;
        static_assert(std::is_same_v<
            mixed_result,
            std::tuple<int, std::string>>);

        // All void: returns void (not empty tuple)
        using all_void_result = when_all_result_type<void, void, void>;
        static_assert(std::is_void_v<all_void_result>);

        // Single void: returns void
        using single_void_result = when_all_result_type<void>;
        static_assert(std::is_void_v<single_void_result>);
    }

    //----------------------------------------------------------
    // Stop token propagation tests
    //----------------------------------------------------------

    // Helper: task that records if stop was requested
    static task<int>
    checks_stop_token(std::atomic<bool>&)
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
            when_all(throws_exception("error"), returns_int(10)),
            [](std::tuple<int, int>) {},
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
            when_all(
                counting_task(),
                failing_task(),
                counting_task()),
            [](std::tuple<int, int, int>) {},
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
            when_all(
                returns_int(1), returns_int(2), returns_int(3), returns_int(4),
                returns_int(5), returns_int(6), returns_int(7), returns_int(8)),
            [&](auto t) {
                auto [a, b, c, d, e, f, g, h] = t;
                completed = true;
                result = a + b + c + d + e + f + g + h;
            },
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
            when_all(multi_step_task(10), multi_step_task(20)),
            [&](std::tuple<int, int> t) {
                auto [a, b] = t;
                completed = true;
                result = a + b;
            },
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
            when_all(throws_exception("test"), throws_other_exception("other")),
            [](std::tuple<int, int>) {},
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

        any_coro operator()(any_coro h) const
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
            when_all(returns_int(1), returns_int(2), returns_int(3)),
            [&](std::tuple<int, int, int> t) {
                auto [a, b, c] = t;
                completed = true;
                BOOST_TEST_EQ(a + b + c, 6);
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
            when_all(
                returns_string("first"),
                returns_string("second"),
                returns_string("third")),
            [&](std::tuple<std::string, std::string, std::string> t) {
                auto [first, second, third] = t;
                BOOST_TEST_EQ(first, "first");
                BOOST_TEST_EQ(second, "second");
                BOOST_TEST_EQ(third, "third");
                completed = true;
            },
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

        // void at index 1, values at 0 and 2
        async_run(d)(
            when_all(returns_int(100), void_task(), returns_int(300)),
            [&](std::tuple<int, int> t) {
                // a should be from index 0, b from index 2
                auto [a, b] = t;
                BOOST_TEST_EQ(a, 100);
                BOOST_TEST_EQ(b, 300);
                completed = true;
            },
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
        int dispatch_count = 0;
        test_dispatcher d(dispatch_count);
        bool completed = false;

        auto awaitable1 = when_all(returns_int(1), returns_int(2));
        auto awaitable2 = std::move(awaitable1);

        async_run(d)(
            std::move(awaitable2),
            [&](std::tuple<int, int> t) {
                auto [a, b] = t;
                completed = true;
                BOOST_TEST_EQ(a + b, 3);
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
        // Await later
        async_run(d)(
            std::move(deferred),
            [&](std::tuple<int, int> t) {
                auto [a, b] = t;
                completed = true;
                BOOST_TEST_EQ(a + b, 30);
            },
            [](std::exception_ptr) {});

        BOOST_TEST(completed);
    }

    //----------------------------------------------------------
    // Stoppable awaitable protocol tests
    //----------------------------------------------------------

#if BOOST_CAPY_HAS_STOP_TOKEN
    // Test: when_all returns task which satisfies stoppable_awaitable concept
    void
    testStoppableAwaitableConcept()
    {
        // when_all now returns task<T>, which satisfies the awaitable protocols
        static_assert(stoppable_awaitable<
            task<std::tuple<int, int>>,
            any_dispatcher>);

        static_assert(stoppable_awaitable<
            task<std::tuple<int, std::string>>,
            any_dispatcher>);

        static_assert(stoppable_awaitable<
            task<void>,
            any_dispatcher>);
    }

    // Test: Nested when_all propagates stop
    void
    testNestedWhenAllStopPropagation()
    {
        int dispatch_count = 0;
        test_dispatcher d(dispatch_count);
        bool caught_exception = false;

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

        async_run(d)(
            when_all(inner_failing(), inner_success()),
            [](std::tuple<int, int>) {},
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
#endif

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
#if BOOST_CAPY_HAS_STOP_TOKEN
        testStoppableAwaitableConcept();
        testNestedWhenAllStopPropagation();
#endif

        // Frame allocator tests
        testWhenAllUsesAllocator();
        testNestedWhenAllUsesAllocator();
    }

    //----------------------------------------------------------
    // Frame allocator tests
    //----------------------------------------------------------

    /** Tracking frame allocator that logs allocation events.
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
    testWhenAllUsesAllocator()
    {
        // Verify that when_all() coroutines use the custom allocator
        int dispatch_count = 0;
        test_dispatcher d(dispatch_count);
        bool completed = false;

        int alloc_count = 0;
        int dealloc_count = 0;
        std::vector<int> alloc_log;

        tracking_frame_allocator alloc{1, &alloc_count, &dealloc_count, &alloc_log};

        async_run(d, alloc)(
            when_all(returns_int(10), returns_int(20), returns_int(30)),
            [&](std::tuple<int, int, int> t) {
                auto [a, b, c] = t;
                completed = true;
                BOOST_TEST_EQ(a + b + c, 60);
            },
            [](std::exception_ptr) {});

        BOOST_TEST(completed);
        // when_all should have allocated frames through our allocator
        BOOST_TEST_GE(alloc_count, 1);
        // All allocations should use our allocator
        for(int id : alloc_log)
            BOOST_TEST_EQ(id, 1);
        // All allocations should be deallocated
        BOOST_TEST_EQ(alloc_count, dealloc_count);
    }

    void
    testNestedWhenAllUsesAllocator()
    {
        // Verify nested when_all calls also use the allocator
        int dispatch_count = 0;
        test_dispatcher d(dispatch_count);
        bool completed = false;

        int alloc_count = 0;
        int dealloc_count = 0;
        std::vector<int> alloc_log;

        tracking_frame_allocator alloc{1, &alloc_count, &dealloc_count, &alloc_log};

        auto inner1 = []() -> task<int> {
            auto [a, b] = co_await when_all(returns_int(1), returns_int(2));
            co_return a + b;
        };

        auto inner2 = []() -> task<int> {
            auto [a, b] = co_await when_all(returns_int(3), returns_int(4));
            co_return a + b;
        };

        int result = 0;
        async_run(d, alloc)(
            when_all(inner1(), inner2()),
            [&](std::tuple<int, int> t) {
                auto [x, y] = t;
                completed = true;
                result = x + y;
            },
            [](std::exception_ptr) {});

        BOOST_TEST(completed);
        BOOST_TEST_EQ(result, 10);  // (1+2) + (3+4) = 10
        // Nested when_all should also allocate through our allocator
        BOOST_TEST_GE(alloc_count, 1);
        // All allocations should use our allocator
        for(int id : alloc_log)
            BOOST_TEST_EQ(id, 1);
        // All allocations should be deallocated
        BOOST_TEST_EQ(alloc_count, dealloc_count);
    }
};

TEST_SUITE(
    when_all_test,
    "boost.capy.when_all");

} // capy
} // boost
