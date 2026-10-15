//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Test that header file is self-contained.
#include <boost/capy/when_all.hpp>

#include <boost/capy/cond.hpp>
#include <boost/capy/error.hpp>
#include <boost/capy/ex/async_event.hpp>
#include <boost/capy/ex/abandon.hpp>
#include <boost/capy/ex/run.hpp>
#include <boost/capy/ex/run_async.hpp>
#include <boost/capy/ex/this_coro.hpp>
#include <boost/capy/ex/strand.hpp>
#include <boost/capy/ex/thread_pool.hpp>
#include <boost/capy/io_task.hpp>
#include <boost/capy/task.hpp>

#include "test_helpers.hpp"

#include <atomic>
#include <latch>
#include <memory>
#include <queue>
#include <ranges>
#include <stdexcept>
#include <string>
#include <system_error>
#include <tuple>
#include <vector>

// GCC gives false positive -Wmaybe-uninitialized on structured bindings
// via the tuple protocol inside coroutine frames.
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif
#include <type_traits>

namespace boost {
namespace capy {

// Verify when_all returns task which satisfies awaitable protocols
static_assert(IoAwaitable<task<io_result<size_t, size_t>>>);

struct when_all_strand_test
{
    // Regression for #131: executor_ref::dispatch() formerly
    // returned void, discarding the symmetric transfer handle
    // from strand::dispatch(). This caused when_all child
    // runners to never resume, deadlocking the caller.
    void
    testStrandWhenAll()
    {
        thread_pool pool(2);
        strand s{pool.get_executor()};
        std::latch done(1);
        bool completed = false;

        auto outer = [&]() -> task<io_result<std::tuple<>, std::tuple<>>> {
            co_return co_await when_all(
                []() -> io_task<> { co_return io_result<>{{}}; }(),
                []() -> io_task<> { co_return io_result<>{{}}; }()
            );
        };

        run_async(s,
            [&](auto&&...) {
                completed = true;
                done.count_down();
            },
            [&](auto) {
                done.count_down();
            }
        )(outer());

        done.wait();
        BOOST_TEST(completed);
    }

    // Verify strand + when_all propagates values correctly
    void
    testStrandWhenAllWithValues()
    {
        thread_pool pool(2);
        strand s{pool.get_executor()};
        std::latch done(1);
        bool completed = false;
        size_t result = 0;

        auto outer = [&]() -> task<io_result<size_t, size_t>> {
            co_return co_await when_all(
                []() -> io_task<size_t> {
                    co_return io_result<size_t>{std::error_code(), 10};
                }(),
                []() -> io_task<size_t> {
                    co_return io_result<size_t>{std::error_code(), 20};
                }());
        };

        run_async(s,
            [&](io_result<size_t, size_t> r) {
                completed = true;
                result = std::get<1>(r) + std::get<2>(r);
                done.count_down();
            },
            [&](auto) {
                done.count_down();
            }
        )(outer());

        done.wait();
        BOOST_TEST(completed);
        BOOST_TEST_EQ(result, 30u);
    }

    void
    run()
    {
        testStrandWhenAll();
        testStrandWhenAllWithValues();
    }
};

TEST_SUITE(
    when_all_strand_test,
    "boost.capy.when_all_strand");

// Verify IoAwaitableRange concept
static_assert(IoAwaitableRange<std::vector<io_task<size_t>>>);
static_assert(IoAwaitableRange<std::vector<io_task<>>>);

// io_task helpers for io_result-aware spec tests
namespace {

io_task<size_t>
io_success_size(size_t n)
{
    co_return io_result<size_t>{std::error_code(), n};
}

io_task<size_t>
io_error_size(std::error_code ec, size_t n = 0)
{
    co_return io_result<size_t>{ec, n};
}

io_task<>
io_void_ok()
{
    co_return io_result<>{};
}

io_task<>
io_void_error(std::error_code ec)
{
    co_return io_result<>{ec};
}

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4702) // unreachable code after throw
#endif

io_task<>
io_void_throws(char const* msg)
{
    throw test_exception(msg);
    co_return io_result<>{};
}

io_task<size_t>
io_throws_size(char const* msg)
{
    throw test_exception(msg);
    co_return io_result<size_t>{std::error_code(), 0};
}

#if defined(_MSC_VER)
#pragma warning(pop)
#endif

io_task<std::string>
io_success_string(std::string s)
{
    co_return io_result<std::string>{std::error_code(), std::move(s)};
}

io_task<size_t, int>
io_success_size_int(size_t n, int flags)
{
    co_return io_result<size_t, int>{std::error_code(), n, flags};
}

// Suspends until stop token fires, then returns ECANCELED.
io_task<size_t>
io_pending_size()
{
    co_await stop_only_awaitable{};
    co_return io_result<size_t>{make_error_code(error::canceled), 0};
}

} // anonymous namespace

struct when_all_range_test
{
    void
    testSingleElement()
    {
        int dc = 0;
        test_executor ex(dc);
        bool completed = false;

        std::vector<io_task<size_t>> tasks;
        tasks.push_back(io_success_size(42));

        run_async(ex,
            [&](io_result<std::vector<size_t>> r) {
                completed = true;
                BOOST_TEST(!std::get<0>(r));
                BOOST_TEST_EQ(std::get<1>(r).size(), 1u);
                BOOST_TEST_EQ(std::get<1>(r)[0], 42u);
            },
            [](std::exception_ptr) {})(
            when_all(std::move(tasks)));

        BOOST_TEST(completed);
    }

    void
    testMultipleElements()
    {
        int dc = 0;
        test_executor ex(dc);
        bool completed = false;

        std::vector<io_task<size_t>> tasks;
        tasks.push_back(io_success_size(10));
        tasks.push_back(io_success_size(20));
        tasks.push_back(io_success_size(30));

        run_async(ex,
            [&](io_result<std::vector<size_t>> r) {
                completed = true;
                BOOST_TEST(!std::get<0>(r));
                BOOST_TEST_EQ(std::get<1>(r).size(), 3u);
                BOOST_TEST_EQ(std::get<1>(r)[0], 10u);
                BOOST_TEST_EQ(std::get<1>(r)[1], 20u);
                BOOST_TEST_EQ(std::get<1>(r)[2], 30u);
            },
            [](std::exception_ptr) {})(
            when_all(std::move(tasks)));

        BOOST_TEST(completed);
    }

    // A range whose iterator holds the element it yields: each
    // element is taken before the next step replaces it.
    void
    testStashingRange()
    {
        int dc = 0;
        test_executor ex(dc);
        bool completed = false;

        stashing_range tasks([](std::size_t i)
            {
                return io_success_size((i + 1) * 10);
            }, 3);
        static_assert(!std::ranges::forward_range<decltype(tasks)>);

        run_async(ex,
            [&](io_result<std::vector<size_t>> r) {
                completed = true;
                BOOST_TEST(!std::get<0>(r));
                BOOST_TEST_EQ(std::get<1>(r).size(), 3u);
                BOOST_TEST_EQ(std::get<1>(r)[0], 10u);
                BOOST_TEST_EQ(std::get<1>(r)[1], 20u);
                BOOST_TEST_EQ(std::get<1>(r)[2], 30u);
            },
            [](std::exception_ptr) {})(
            when_all(tasks));

        BOOST_TEST(completed);
    }

    // A view yielding its awaitables by value: each lives only for its
    // step of the iteration.
    // A range that throws while when_all takes it destroys the
    // awaitables already taken and reports the exception.
    void
    testRangeThrowsWhileTaken()
    {
        int dc = 0;
        test_executor ex(dc);
        int down = 0;
        bool completed = false;
        bool threw = false;
        auto body = [](destroy_count_t) -> io_task<size_t>
        {
            co_return io_result<size_t>{std::error_code(), 1};
        };
        auto tasks = std::views::iota(0, 3) |
            std::views::transform([&](int i)
            {
                if(i == 1)
                    throw test_exception("range");
                return body(destroy_count_t(down));
            });

        run_async(ex,
            [&](io_result<std::vector<size_t>>) { completed = true; },
            [&](std::exception_ptr) { threw = true; })(
            when_all(tasks));

        BOOST_TEST(!completed);
        BOOST_TEST(threw);
        BOOST_TEST_EQ(down, 1);
    }

    void
    testPrvalueRange()
    {
        int dc = 0;
        test_executor ex(dc);
        bool completed = false;

        auto tasks = std::views::iota(1, 4) |
            std::views::transform([](int i)
            {
                return io_success_size(static_cast<size_t>(i * 10));
            });

        run_async(ex,
            [&](io_result<std::vector<size_t>> r) {
                completed = true;
                BOOST_TEST(!std::get<0>(r));
                BOOST_TEST_EQ(std::get<1>(r).size(), 3u);
                BOOST_TEST_EQ(std::get<1>(r)[0], 10u);
                BOOST_TEST_EQ(std::get<1>(r)[1], 20u);
                BOOST_TEST_EQ(std::get<1>(r)[2], 30u);
            },
            [](std::exception_ptr) {})(
            when_all(tasks));

        BOOST_TEST(completed);
    }

    void
    testEmptyRange()
    {
        int dc = 0;
        test_executor ex(dc);
        bool caught = false;

        std::vector<io_task<size_t>> tasks;

        run_async(ex,
            [](io_result<std::vector<size_t>>) {},
            [&](std::exception_ptr ep) {
                try {
                    std::rethrow_exception(ep);
                } catch (std::invalid_argument const&) {
                    caught = true;
                }
            })(when_all(std::move(tasks)));

        BOOST_TEST(caught);
    }

    void
    testVoidRange()
    {
        int dc = 0;
        test_executor ex(dc);
        bool completed = false;

        std::vector<io_task<>> tasks;
        tasks.push_back(io_void_ok());
        tasks.push_back(io_void_ok());
        tasks.push_back(io_void_ok());

        run_async(ex,
            [&](io_result<> r) {
                completed = true;
                BOOST_TEST(!std::get<0>(r));
            },
            [](std::exception_ptr) {})(
            when_all(std::move(tasks)));

        BOOST_TEST(completed);
    }

    void
    testEmptyVoidRange()
    {
        int dc = 0;
        test_executor ex(dc);
        bool caught = false;

        std::vector<io_task<>> tasks;

        run_async(ex,
            [](io_result<>) {},
            [&](std::exception_ptr ep) {
                try {
                    std::rethrow_exception(ep);
                } catch (std::invalid_argument const&) {
                    caught = true;
                }
            })(when_all(std::move(tasks)));

        BOOST_TEST(caught);
    }

    void
    testErrorCancelsSiblings()
    {
        int dc = 0;
        test_executor ex(dc);
        bool completed = false;
        std::error_code result_ec;

        std::vector<io_task<size_t>> tasks;
        tasks.push_back(io_error_size(make_error_code(error::eof)));
        tasks.push_back(io_pending_size());

        run_async(ex,
            [&](io_result<std::vector<size_t>> r) {
                completed = true;
                result_ec = std::get<0>(r);
            },
            [](std::exception_ptr) {})(
            when_all(std::move(tasks)));

        BOOST_TEST(completed);
        BOOST_TEST(result_ec == cond::eof);
    }

    void
    testMultipleErrorsFirstWins()
    {
        int dc = 0;
        test_executor ex(dc);
        bool completed = false;
        std::error_code result_ec;

        std::vector<io_task<size_t>> tasks;
        tasks.push_back(io_error_size(make_error_code(error::eof)));
        tasks.push_back(io_error_size(make_error_code(error::timeout)));

        run_async(ex,
            [&](io_result<std::vector<size_t>> r) {
                completed = true;
                result_ec = std::get<0>(r);
            },
            [](std::exception_ptr) {})(
            when_all(std::move(tasks)));

        BOOST_TEST(completed);
        BOOST_TEST(result_ec == cond::eof);
    }

    void
    testException()
    {
        int dc = 0;
        test_executor ex(dc);
        bool caught = false;
        std::string msg;

        std::vector<io_task<size_t>> tasks;
        tasks.push_back(io_success_size(1));
        tasks.push_back(io_throws_size("range error"));
        tasks.push_back(io_success_size(3));

        run_async(ex,
            [](io_result<std::vector<size_t>>) {},
            [&](std::exception_ptr ep) {
                try {
                    std::rethrow_exception(ep);
                } catch (test_exception const& e) {
                    caught = true;
                    msg = e.what();
                }
            })(when_all(std::move(tasks)));

        BOOST_TEST(caught);
        BOOST_TEST_EQ(msg, "range error");
    }

    void
    testVoidRangeError()
    {
        int dc = 0;
        test_executor ex(dc);
        bool completed = false;
        std::error_code result_ec;

        std::vector<io_task<>> tasks;
        tasks.push_back(io_void_ok());
        tasks.push_back(io_void_error(make_error_code(error::eof)));

        run_async(ex,
            [&](io_result<> r) {
                completed = true;
                result_ec = std::get<0>(r);
            },
            [](std::exception_ptr) {})(
            when_all(std::move(tasks)));

        BOOST_TEST(completed);
        BOOST_TEST(result_ec == cond::eof);
    }

    void
    testVoidRangeException()
    {
        int dc = 0;
        test_executor ex(dc);
        bool caught = false;

        std::vector<io_task<>> tasks;
        tasks.push_back(io_void_ok());
        tasks.push_back(io_void_throws("void range error"));

        run_async(ex,
            [](io_result<>) {},
            [&](std::exception_ptr ep) {
                try {
                    std::rethrow_exception(ep);
                } catch (test_exception const&) {
                    caught = true;
                }
            })(when_all(std::move(tasks)));

        BOOST_TEST(caught);
    }

    void
    testExceptionBeatsError()
    {
        int dc = 0;
        test_executor ex(dc);
        bool caught = false;

        std::vector<io_task<size_t>> tasks;
        tasks.push_back(io_throws_size("exception wins"));
        tasks.push_back(io_error_size(make_error_code(error::eof)));

        run_async(ex,
            [](io_result<std::vector<size_t>>) {},
            [&](std::exception_ptr ep) {
                try {
                    std::rethrow_exception(ep);
                } catch (test_exception const&) {
                    caught = true;
                }
            })(when_all(std::move(tasks)));

        BOOST_TEST(caught);
    }

    void
    testAllTasksCompleteAfterError()
    {
        int dc = 0;
        test_executor ex(dc);
        std::atomic<int> completion_count{0};
        bool completed = false;

        auto counting_io = [&]() -> io_task<size_t> {
            ++completion_count;
            co_return io_result<size_t>{std::error_code(), 1};
        };

        auto failing_io = [&]() -> io_task<size_t> {
            ++completion_count;
            co_return io_result<size_t>{make_error_code(error::eof), 0};
        };

        std::vector<io_task<size_t>> tasks;
        tasks.push_back(counting_io());
        tasks.push_back(failing_io());
        tasks.push_back(counting_io());

        run_async(ex,
            [&](io_result<std::vector<size_t>>) {
                completed = true;
            },
            [](std::exception_ptr) {})(
            when_all(std::move(tasks)));

        BOOST_TEST(completed);
        BOOST_TEST_EQ(completion_count.load(), 3);
    }

    void
    testErrorViaSuccessHandler()
    {
        int dc = 0;
        test_executor ex(dc);
        bool success_called = false;
        bool error_called = false;

        std::vector<io_task<size_t>> tasks;
        tasks.push_back(io_error_size(make_error_code(error::eof)));

        run_async(ex,
            [&](io_result<std::vector<size_t>> r) {
                success_called = true;
                BOOST_TEST(!!std::get<0>(r));
            },
            [&](std::exception_ptr) {
                error_called = true;
            })(when_all(std::move(tasks)));

        BOOST_TEST(success_called);
        BOOST_TEST(!error_called);
    }

    void
    testStringResults()
    {
        int dc = 0;
        test_executor ex(dc);
        bool completed = false;

        std::vector<io_task<std::string>> tasks;
        tasks.push_back(io_success_string("first"));
        tasks.push_back(io_success_string("second"));
        tasks.push_back(io_success_string("third"));

        run_async(ex,
            [&](io_result<std::vector<std::string>> r) {
                completed = true;
                BOOST_TEST(!std::get<0>(r));
                BOOST_TEST_EQ(std::get<1>(r)[0], "first");
                BOOST_TEST_EQ(std::get<1>(r)[1], "second");
                BOOST_TEST_EQ(std::get<1>(r)[2], "third");
            },
            [](std::exception_ptr) {})(
            when_all(std::move(tasks)));

        BOOST_TEST(completed);
    }

    void
    testNestedRangeInVariadic()
    {
        int dc = 0;
        test_executor ex(dc);
        bool completed = false;

        auto range_task = []() -> io_task<std::vector<size_t>> {
            std::vector<io_task<size_t>> tasks;
            tasks.push_back(io_success_size(1));
            tasks.push_back(io_success_size(2));
            tasks.push_back(io_success_size(3));
            co_return co_await when_all(std::move(tasks));
        };

        auto io_size_task = []() -> io_task<size_t> {
            co_return io_result<size_t>{std::error_code(), 99};
        };

        run_async(ex,
            [&](io_result<std::vector<size_t>, size_t> r) {
                completed = true;
                BOOST_TEST(!std::get<0>(r));
                BOOST_TEST_EQ(std::get<1>(r).size(), 3u);
                BOOST_TEST_EQ(std::get<1>(r)[0] + std::get<1>(r)[1] + std::get<1>(r)[2], 6u);
                BOOST_TEST_EQ(std::get<2>(r), 99u);
            },
            [](std::exception_ptr) {})(
            when_all(range_task(), io_size_task()));

        BOOST_TEST(completed);
    }

    void
    testStrandRange()
    {
        thread_pool pool(2);
        strand s{pool.get_executor()};
        std::latch done(1);
        bool completed = false;
        size_t result = 0;

        auto outer = [&]() -> task<io_result<std::vector<size_t>>> {
            std::vector<io_task<size_t>> tasks;
            tasks.push_back(io_success_size(10));
            tasks.push_back(io_success_size(20));
            co_return co_await when_all(std::move(tasks));
        };

        run_async(s,
            [&](io_result<std::vector<size_t>> r) {
                completed = true;
                BOOST_TEST(!std::get<0>(r));
                result = std::get<1>(r)[0] + std::get<1>(r)[1];
                done.count_down();
            },
            [&](auto) {
                done.count_down();
            }
        )(outer());

        done.wait();
        BOOST_TEST(completed);
        BOOST_TEST_EQ(result, 30u);
    }

    void
    testStopAlreadyRequestedRange()
    {
        // A caller stop token already requested when the range
        // when_all launches makes the launcher propagate the stop to
        // its source. Pre-requested stop_source + inline executor keep
        // this deterministic.
        int dc = 0;
        test_executor ex(dc);
        std::stop_source src;
        src.request_stop();
        bool completed = false;

        std::vector<io_task<size_t>> tasks;
        tasks.push_back(io_success_size(1));
        tasks.push_back(io_success_size(2));

        run_async(ex, src.get_token(),
            [&](io_result<std::vector<size_t>>) { completed = true; },
            [](std::exception_ptr) {})(when_all(std::move(tasks)));

        BOOST_TEST(completed);
    }

    void
    testStopAlreadyRequestedTuple()
    {
        // Same, but for the variadic (tuple) when_all launcher.
        int dc = 0;
        test_executor ex(dc);
        std::stop_source src;
        src.request_stop();
        bool completed = false;

        auto outer = []() -> task<io_result<std::tuple<>, std::tuple<>>> {
            co_return co_await when_all(io_void_ok(), io_void_ok());
        };

        run_async(ex, src.get_token(),
            [&](auto&&...) { completed = true; },
            [](std::exception_ptr) {})(outer());

        BOOST_TEST(completed);
    }

    void
    run()
    {
        testStopAlreadyRequestedRange();
        testStopAlreadyRequestedTuple();
        testSingleElement();
        testMultipleElements();
        testPrvalueRange();
        testRangeThrowsWhileTaken();
        testStashingRange();
        testEmptyRange();
        testVoidRange();
        testEmptyVoidRange();
        testErrorCancelsSiblings();
        testMultipleErrorsFirstWins();
        testException();
        testVoidRangeError();
        testVoidRangeException();
        testExceptionBeatsError();
        testAllTasksCompleteAfterError();
        testErrorViaSuccessHandler();
        testStringResults();
        testNestedRangeInVariadic();
        testStrandRange();
    }
};

TEST_SUITE(
    when_all_range_test,
    "boost.capy.when_all_range");

// Tests for io_result-aware when_all behavior per the combinators spec.
// Each test is labelled with the spec row it verifies.
struct when_all_io_result_test
{
    // Spec Row 1: All tasks return !ec
    // Return tuple of all results. No cancellation.
    void
    testAllSucceed()
    {
        int dc = 0;
        test_executor ex(dc);
        bool completed = false;
        size_t n1 = 0, n2 = 0, n3 = 0;

        run_async(ex,
            [&](io_result<size_t, size_t, size_t> r) {
                completed = true;
                BOOST_TEST(!std::get<0>(r));
                n1 = std::get<1>(r);
                n2 = std::get<2>(r);
                n3 = std::get<3>(r);
            },
            [](std::exception_ptr) {})(
            when_all(
                io_success_size(10),
                io_success_size(20),
                io_success_size(30)));

        BOOST_TEST(completed);
        BOOST_TEST_EQ(n1, 10u);
        BOOST_TEST_EQ(n2, 20u);
        BOOST_TEST_EQ(n3, 30u);
    }

    // Spec Row 1 (single child)
    void
    testSingleTaskSuccess()
    {
        int dc = 0;
        test_executor ex(dc);
        bool completed = false;
        size_t result = 0;

        run_async(ex,
            [&](io_result<size_t> r) {
                completed = true;
                BOOST_TEST(!std::get<0>(r));
                result = std::get<1>(r);
            },
            [](std::exception_ptr) {})(
            when_all(io_success_size(42)));

        BOOST_TEST(completed);
        BOOST_TEST_EQ(result, 42u);
    }

    // Spec Row 2: One task returns ec, others pending
    // Cancel siblings. Propagate error.
    void
    testOneErrorCancelsSiblings()
    {
        int dc = 0;
        test_executor ex(dc);
        bool completed = false;
        std::error_code result_ec;

        run_async(ex,
            [&](io_result<size_t, size_t> r) {
                completed = true;
                result_ec = std::get<0>(r);
            },
            [](std::exception_ptr) {})(
            when_all(
                io_error_size(make_error_code(error::eof)),
                io_pending_size()));

        BOOST_TEST(completed);
        BOOST_TEST(result_ec == cond::eof);
    }

    // Spec Row 3: Multiple tasks return ec concurrently
    // Each triggers stop (idempotent). First ec wins.
    void
    testMultipleErrorsFirstWins()
    {
        int dc = 0;
        test_executor ex(dc);
        bool completed = false;
        std::error_code result_ec;

        run_async(ex,
            [&](io_result<size_t, size_t> r) {
                completed = true;
                result_ec = std::get<0>(r);
            },
            [](std::exception_ptr) {})(
            when_all(
                io_error_size(make_error_code(error::eof)),
                io_error_size(make_error_code(error::timeout))));

        BOOST_TEST(completed);
        BOOST_TEST(result_ec == cond::eof);
    }

    // Spec Row 4: ec == eof, n == 0
    // Error. Cancel siblings.
    void
    testEofWithZeroBytes()
    {
        int dc = 0;
        test_executor ex(dc);
        bool completed = false;
        std::error_code result_ec;

        run_async(ex,
            [&](io_result<size_t, size_t> r) {
                completed = true;
                result_ec = std::get<0>(r);
            },
            [](std::exception_ptr) {})(
            when_all(
                io_error_size(make_error_code(error::eof), 0),
                io_pending_size()));

        BOOST_TEST(completed);
        BOOST_TEST(result_ec == cond::eof);
    }

    // Spec Row 5: ec != 0, n > 0 (partial transfer)
    // Error. Cancel siblings. Values stored as-is.
    void
    testPartialTransferIsError()
    {
        int dc = 0;
        test_executor ex(dc);
        bool completed = false;
        std::error_code result_ec;
        size_t partial = 0;

        run_async(ex,
            [&](io_result<size_t> r) {
                completed = true;
                result_ec = std::get<0>(r);
                partial = std::get<1>(r);
            },
            [](std::exception_ptr) {})(
            when_all(
                io_error_size(make_error_code(error::eof), 42)));

        BOOST_TEST(completed);
        BOOST_TEST(result_ec == cond::eof);
        BOOST_TEST_EQ(partial, 42u);
    }

    // Spec Row 5 (with sibling)
    void
    testPartialTransferValuePreserved()
    {
        int dc = 0;
        test_executor ex(dc);
        bool completed = false;
        std::error_code result_ec;
        size_t n1 = 0;

        run_async(ex,
            [&](io_result<size_t, size_t> r) {
                completed = true;
                result_ec = std::get<0>(r);
                n1 = std::get<1>(r);
            },
            [](std::exception_ptr) {})(
            when_all(
                io_error_size(make_error_code(error::eof), 42),
                io_pending_size()));

        BOOST_TEST(completed);
        BOOST_TEST(result_ec == cond::eof);
        BOOST_TEST_EQ(n1, 42u);
    }

    // Spec Row 6: Zero-length buffer, ({}, 0)
    // Success. No cancellation.
    void
    testZeroTransferSuccess()
    {
        int dc = 0;
        test_executor ex(dc);
        bool completed = false;

        run_async(ex,
            [&](io_result<size_t> r) {
                completed = true;
                BOOST_TEST(!std::get<0>(r));
                BOOST_TEST_EQ(std::get<1>(r), 0u);
            },
            [](std::exception_ptr) {})(
            when_all(io_success_size(0)));

        BOOST_TEST(completed);
    }

    // Spec Row 7: Zero-length buffer, (ec, 0)
    // Error (ec reflects stream state). Cancel siblings.
    void
    testZeroTransferError()
    {
        int dc = 0;
        test_executor ex(dc);
        bool completed = false;
        std::error_code result_ec;

        run_async(ex,
            [&](io_result<size_t> r) {
                completed = true;
                result_ec = std::get<0>(r);
            },
            [](std::exception_ptr) {})(
            when_all(
                io_error_size(make_error_code(error::eof), 0)));

        BOOST_TEST(completed);
        BOOST_TEST(result_ec == cond::eof);
    }

    // Spec Row 8: One task throws
    // Capture exception. Cancel siblings. Rethrow after all complete.
    void
    testOneThrows()
    {
        int dc = 0;
        test_executor ex(dc);
        bool completed = false;
        bool caught = false;
        std::string msg;

        run_async(ex,
            [&](io_result<size_t, size_t>) { completed = true; },
            [&](std::exception_ptr ep) {
                try { std::rethrow_exception(ep); }
                catch (test_exception const& e) {
                    caught = true;
                    msg = e.what();
                }
            })(when_all(
                io_throws_size("boom"),
                io_pending_size()));

        BOOST_TEST(!completed);
        BOOST_TEST(caught);
        BOOST_TEST_EQ(msg, "boom");
    }

    // Spec Row 9: Multiple tasks throw
    // First exception captured. Others discarded. Rethrow first.
    void
    testMultipleThrowsFirstWins()
    {
        int dc = 0;
        test_executor ex(dc);
        bool completed = false;
        bool caught = false;
        std::string msg;

        run_async(ex,
            [&](io_result<size_t, size_t>) { completed = true; },
            [&](std::exception_ptr ep) {
                try { std::rethrow_exception(ep); }
                catch (test_exception const& e) {
                    caught = true;
                    msg = e.what();
                }
            })(when_all(
                io_throws_size("first"),
                io_throws_size("second")));

        BOOST_TEST(!completed);
        BOOST_TEST(caught);
        BOOST_TEST_EQ(msg, "first");
    }

    // Spec Row 10: One throws, another returns ec (either order)
    // Exception always wins.
    void
    testExceptionBeatsError()
    {
        int dc = 0;
        test_executor ex(dc);
        bool completed = false;
        bool caught = false;
        std::string msg;

        run_async(ex,
            [&](io_result<size_t, size_t>) { completed = true; },
            [&](std::exception_ptr ep) {
                try { std::rethrow_exception(ep); }
                catch (test_exception const& e) {
                    caught = true;
                    msg = e.what();
                }
            })(when_all(
                io_throws_size("exception wins"),
                io_error_size(make_error_code(error::eof))));

        BOOST_TEST(!completed);
        BOOST_TEST(caught);
        BOOST_TEST_EQ(msg, "exception wins");
    }

    // Spec Row 10 (reversed): error first, then throw
    // Exception still wins.
    void
    testExceptionBeatsErrorReversed()
    {
        int dc = 0;
        test_executor ex(dc);
        bool completed = false;
        bool caught = false;

        run_async(ex,
            [&](io_result<size_t, size_t>) { completed = true; },
            [&](std::exception_ptr ep) {
                try { std::rethrow_exception(ep); }
                catch (test_exception const&) { caught = true; }
            })(when_all(
                io_error_size(make_error_code(error::eof)),
                io_throws_size("exception")));

        BOOST_TEST(!completed);
        BOOST_TEST(caught);
    }

    // Spec Row 11: Parent stop token fires
    // Not a special case. Children return ECANCELED,
    // which is an error like any other. First ec wins.
    void
    testCanceledIsNormalError()
    {
        int dc = 0;
        test_executor ex(dc);
        bool completed = false;
        std::error_code result_ec;

        run_async(ex,
            [&](io_result<size_t, size_t> r) {
                completed = true;
                result_ec = std::get<0>(r);
            },
            [](std::exception_ptr) {})(
            when_all(
                io_error_size(make_error_code(error::canceled)),
                io_error_size(make_error_code(error::canceled))));

        BOOST_TEST(completed);
        BOOST_TEST(result_ec == cond::canceled);
    }

    // Spec Row 12: All tasks fail
    // Propagate single error_code (first wins). Not a tuple of failures.
    void
    testAllFail()
    {
        int dc = 0;
        test_executor ex(dc);
        bool completed = false;
        std::error_code result_ec;

        run_async(ex,
            [&](io_result<size_t, size_t, size_t> r) {
                completed = true;
                result_ec = std::get<0>(r);
            },
            [](std::exception_ptr) {})(
            when_all(
                io_error_size(make_error_code(error::eof)),
                io_error_size(make_error_code(error::timeout)),
                io_error_size(make_error_code(error::canceled))));

        BOOST_TEST(completed);
        BOOST_TEST(result_ec == cond::eof);
    }

    // Spec Row 13: Failure reaches caller via io_result's ec
    // Error goes through success handler, not exception handler.
    void
    testErrorViaSuccessHandler()
    {
        int dc = 0;
        test_executor ex(dc);
        bool success_called = false;
        bool error_called = false;

        run_async(ex,
            [&](io_result<size_t> r) {
                success_called = true;
                BOOST_TEST(!!std::get<0>(r));
            },
            [&](std::exception_ptr) {
                error_called = true;
            })(when_all(
                io_error_size(make_error_code(error::eof))));

        BOOST_TEST(success_called);
        BOOST_TEST(!error_called);
    }

    // Spec Row 14 (mixed value types)
    void
    testMixedValueTypes()
    {
        int dc = 0;
        test_executor ex(dc);
        bool completed = false;
        size_t n = 0;
        std::string s;

        run_async(ex,
            [&](io_result<size_t, std::string> r) {
                completed = true;
                BOOST_TEST(!std::get<0>(r));
                n = std::get<1>(r);
                s = std::get<2>(r);
            },
            [](std::exception_ptr) {})(
            when_all(
                io_success_size(42),
                io_success_string("hello")));

        BOOST_TEST(completed);
        BOOST_TEST_EQ(n, 42u);
        BOOST_TEST_EQ(s, "hello");
    }

    // Spec Row 14 (multi-value child: io_result<T1, T2> contributes tuple<T1, T2>)
    void
    testMultiValueChild()
    {
        int dc = 0;
        test_executor ex(dc);
        bool completed = false;
        size_t n = 0;
        std::tuple<size_t, int> tf;

        run_async(ex,
            [&](io_result<size_t, std::tuple<size_t, int>> r) {
                completed = true;
                BOOST_TEST(!std::get<0>(r));
                n = std::get<1>(r);
                tf = std::get<2>(r);
            },
            [](std::exception_ptr) {})(
            when_all(
                io_success_size(42),
                io_success_size_int(10, 7)));

        BOOST_TEST(completed);
        BOOST_TEST_EQ(n, 42u);
        BOOST_TEST_EQ(std::get<0>(tf), 10u);
        BOOST_TEST_EQ(std::get<1>(tf), 7);
    }

    // Spec Row 14 (void results: io_result<> contributes tuple<>)
    void
    testVoidResults()
    {
        int dc = 0;
        test_executor ex(dc);
        bool completed = false;
        size_t n = 0;

        run_async(ex,
            [&](io_result<size_t, std::tuple<>> r) {
                completed = true;
                BOOST_TEST(!std::get<0>(r));
                n = std::get<1>(r);
            },
            [](std::exception_ptr) {})(
            when_all(
                io_success_size(42),
                io_void_ok()));

        BOOST_TEST(completed);
        BOOST_TEST_EQ(n, 42u);
    }

    // First error in time wins, not first in tuple order.
    // Child 0 (pending) gets cancelled after child 1 fails with eof.
    // The outer ec must be eof, not canceled.
    void
    testFirstErrorInTimeWins()
    {
        int dc = 0;
        test_executor ex(dc);
        bool completed = false;
        std::error_code result_ec;

        run_async(ex,
            [&](io_result<size_t, size_t> r) {
                completed = true;
                result_ec = std::get<0>(r);
            },
            [](std::exception_ptr) {})(
            when_all(
                io_pending_size(),
                io_error_size(make_error_code(error::eof))));

        BOOST_TEST(completed);
        BOOST_TEST(result_ec == cond::eof);
    }

    // A runner its context destroys before it first runs is a lost
    // branch; the parent goes with the last one.
    void testRunnersDestroyedBeforeStartDestroyParent()
    {
        int a_down = 0, b_down = 0, parent_down = 0;
        std::queue<continuation> q;
        auto branch = [](destroy_count_t) -> task<io_result<>>
        {
            co_return io_result<>{};
        };
        auto parent = [&](destroy_count_t) -> task<void>
        {
            std::ignore = co_await when_all(
                branch(destroy_count_t(a_down)),
                branch(destroy_count_t(b_down)));
        };
        run_async(queuing_executor(q))(parent(destroy_count_t(parent_down)));
        BOOST_TEST_EQ(q.size(), 1u);
        auto start = q.front();
        q.pop();
        safe_resume(start.h);     // parent posts both runners
        BOOST_TEST_EQ(q.size(), 2u);

        // An unstarted runner owns no branch: the branches stay with the
        // join until it is unwound.
        capy::abandon(q.front());
        q.pop();
        BOOST_TEST_EQ(a_down, 0);
        BOOST_TEST_EQ(b_down, 0);
        BOOST_TEST_EQ(parent_down, 0);

        capy::abandon(q.front());
        q.pop();
        pump(q);
        BOOST_TEST_EQ(a_down, 1);
        BOOST_TEST_EQ(b_down, 1);
        BOOST_TEST_EQ(parent_down, 1);
    }

    // Both branches torn down from below: the combinator and the
    // parent go with the last one.
    void testTeardownOfAllBranchesDestroysParent()
    {
        int a_down = 0, b_down = 0, parent_down = 0;
        parked sa, sb;
        auto branch = [](parked* s, destroy_count_t)
            -> task<io_result<>>
        {
            co_await park_awaitable{s};
            co_return io_result<>{};
        };
        auto parent = [&](destroy_count_t) -> task<void>
        {
            std::ignore = co_await when_all(
                branch(&sa, destroy_count_t(a_down)),
                branch(&sb, destroy_count_t(b_down)));
        };
        int n = 0;
        test_executor ex(n);
        run_async(ex)(parent(destroy_count_t(parent_down)));
        BOOST_TEST(sa && sb);
        capy::abandon(sa);
        BOOST_TEST_EQ(a_down, 1);
        BOOST_TEST_EQ(parent_down, 0); // b still holds a share
        capy::abandon(sb);
        BOOST_TEST_EQ(b_down, 1);
        BOOST_TEST_EQ(parent_down, 1);
    }

    // A branch that completes after a sibling was lost destroys the
    // parent instead of resuming it.
    void testLostBranchParentIsDestroyedNotResumed()
    {
        int parent_down = 0;
        bool resumed = false;
        parked sa, sb;
        auto branch = [](parked* s) -> task<io_result<>>
        {
            co_await park_awaitable{s};
            co_return io_result<>{};
        };
        auto parent = [&](destroy_count_t) -> task<void>
        {
            std::ignore = co_await when_all(branch(&sa), branch(&sb));
            resumed = true;
        };
        int n = 0;
        test_executor ex(n);
        run_async(ex)(parent(destroy_count_t(parent_down)));
        capy::abandon(sa);
        sb.resume();
        BOOST_TEST(!resumed);
        BOOST_TEST_EQ(parent_down, 1);
    }

    // Parks the when_all runner itself, so tearing it down enters
    // through the runner's promise rather than through a branch task.
    struct park_io_awaitable : park_awaitable
    {
        io_result<> await_resume() const noexcept { return {}; }
    };

    // Losing every branch under run_async(ex, alloc) frees the runner
    // frames before the trampoline holding their allocator.
    void testLostBranchesFreeBeforeAllocatorOwner()
    {
        alloc_counts st;
        int parent_down = 0;
        parked sa, sb;
        auto parent = [](parked* a,
            parked* b, destroy_count_t) -> task<void>
        {
            std::ignore = co_await when_all(
                park_io_awaitable{{a}}, park_io_awaitable{{b}});
        };
        int n = 0;
        test_executor ex(n);
        run_async(ex, counting_allocator<std::byte>(st))(
            parent(&sa, &sb, destroy_count_t(parent_down)));
        capy::abandon(sa);
        capy::abandon(sb);
        BOOST_TEST_EQ(parent_down, 1);
        BOOST_TEST(st.allocs > 0);
        BOOST_TEST(st.balanced());
    }

    // Branch tasks lost under run(mr) are freed before the frame that
    // owns the resource is destroyed.
    void testLostTaskBranchesFreedBeforeResourceOwner()
    {
        lease_resource mr;
        int a_down = 0, b_down = 0, owner_down = 0, n = 0;
        parked sa, sb;
        test_executor ex(n);
        auto branch = [](parked* s, destroy_count_t)
            -> task<io_result<>>
        {
            co_await park_awaitable{s};
            co_return io_result<>{};
        };
        auto join = [&]() -> task<void>
        {
            std::ignore = co_await when_all(
                branch(&sa, destroy_count_t(a_down)),
                branch(&sb, destroy_count_t(b_down)));
        };
        auto owner = [&](destroy_count_t) -> task<void>
        {
            lease l(mr);
            co_await capy::run(&mr)(join());
        };
        run_async(ex)(owner(destroy_count_t(owner_down)));
        BOOST_TEST(sa && sb);
        capy::abandon(sa);
        capy::abandon(sb);
        BOOST_TEST_EQ(a_down, 1);
        BOOST_TEST_EQ(b_down, 1);
        BOOST_TEST_EQ(owner_down, 1);
        BOOST_TEST(mr.allocs.load() > 0);
        BOOST_TEST(mr.balanced());
    }

    // The range overload tears down like the variadic one: no branch
    // destroys a sibling, and the parent goes with the last branch.
    void testRangeTeardownOfAllBranchesDestroysParent()
    {
        constexpr int count = 3;
        int down[count] = {}, parent_down = 0;
        parked slots[count];
        auto branch = [](parked* s, destroy_count_t)
            -> task<io_result<>>
        {
            co_await park_awaitable{s};
            co_return io_result<>{};
        };
        auto parent = [&](destroy_count_t) -> task<void>
        {
            std::vector<task<io_result<>>> tasks;
            for(int i = 0; i < count; ++i)
                tasks.push_back(
                    branch(&slots[i], destroy_count_t(down[i])));
            std::ignore = co_await when_all(std::move(tasks));
        };
        int n = 0;
        test_executor ex(n);
        run_async(ex)(parent(destroy_count_t(parent_down)));
        for(int i = 0; i < count; ++i)
        {
            BOOST_TEST(slots[i]);
            capy::abandon(slots[i]);
            for(int j = 0; j < count; ++j)
                BOOST_TEST_EQ(down[j], j <= i ? 1 : 0);
            BOOST_TEST_EQ(parent_down, i == count - 1 ? 1 : 0);
        }
    }

    // A branch that finished before its sibling was lost does not keep
    // the parent alive or resume it.
    void testFinishedThenLostDestroysParent()
    {
        int parent_down = 0;
        bool resumed = false;
        parked sa, sb;
        auto branch = [](parked* s) -> task<io_result<>>
        {
            co_await park_awaitable{s};
            co_return io_result<>{};
        };
        auto parent = [&](destroy_count_t) -> task<void>
        {
            std::ignore = co_await when_all(branch(&sa), branch(&sb));
            resumed = true;
        };
        int n = 0;
        test_executor ex(n);
        run_async(ex)(parent(destroy_count_t(parent_down)));
        sa.resume();
        BOOST_TEST_EQ(parent_down, 0);
        capy::abandon(sb);
        BOOST_TEST(!resumed);
        BOOST_TEST_EQ(parent_down, 1);
    }

    // A join inside a branch of another: the outer parent goes only
    // once every leaf of both joins is torn down.
    void testNestedJoinsTeardown()
    {
        int leaf_down = 0, inner_down = 0, parent_down = 0;
        parked sa, sb, sc;
        auto branch = [](parked* s, destroy_count_t) -> task<io_result<>>
        {
            co_await park_awaitable{s};
            co_return io_result<>{};
        };
        auto inner = [&](destroy_count_t) -> task<io_result<>>
        {
            std::ignore = co_await when_all(
                branch(&sb, destroy_count_t(leaf_down)),
                branch(&sc, destroy_count_t(leaf_down)));
            co_return io_result<>{};
        };
        auto parent = [&](destroy_count_t) -> task<void>
        {
            std::ignore = co_await when_all(
                branch(&sa, destroy_count_t(leaf_down)),
                inner(destroy_count_t(inner_down)));
        };
        int n = 0;
        test_executor ex(n);
        run_async(ex)(parent(destroy_count_t(parent_down)));
        BOOST_TEST(sa && sb && sc);
        capy::abandon(sb);
        capy::abandon(sa);
        BOOST_TEST_EQ(leaf_down, 2);
        BOOST_TEST_EQ(inner_down, 0);
        BOOST_TEST_EQ(parent_down, 0);
        capy::abandon(sc);
        BOOST_TEST_EQ(leaf_down, 3);
        BOOST_TEST_EQ(inner_down, 1);
        BOOST_TEST_EQ(parent_down, 1);
    }

    // A stop that loses the last branch while the join is still
    // stopping its branches leaves the join's stop state alive until
    // that request returns. Freeing it early shows only under a
    // sanitizer, and only libstdc++'s callback order makes it reachable.
    void testStopLosingLastBranch()
    {
        std::queue<continuation> q;
        bool closed = false;
        int parent_down = 0;
        async_event ev;
        std::stop_source src;
        auto branch = [&]() -> task<io_result<>>
        {
            auto tok = (co_await this_coro::environment)->stop_token;
            // Registered before the wait's, so libstdc++ runs it
            // after the wait's and relocks the stop state between.
            std::stop_callback extra(tok, [] {});
            auto [ec] = co_await ev.wait();
            co_return io_result<>{ec};
        };
        auto parent = [&](destroy_count_t) -> task<void>
        {
            std::ignore = co_await when_all(branch(), branch());
        };
        run_async(closing_executor{&q, &closed}, src.get_token())(
            parent(destroy_count_t(parent_down)));
        pump(q);
        BOOST_TEST_EQ(parent_down, 0);
        closed = true;
        src.request_stop();
        BOOST_TEST_EQ(parent_down, 1);
    }

    // A branch that hops executors through run is torn down below the
    // hop: the branch is lost once it is back on the caller's executor.
    void testRunInsideBranchTornDown()
    {
        int leaf_down = 0, hop_down = 0, parent_down = 0, n = 0;
        parked sa, sb;
        std::queue<continuation> q;
        queuing_executor caller(q);
        test_executor inner(n);
        auto leaf = [](parked* s, destroy_count_t) -> task<void>
        {
            co_await park_awaitable{s};
        };
        auto hop = [&](parked* s, destroy_count_t) -> task<io_result<>>
        {
            co_await capy::run(inner)(leaf(s, destroy_count_t(leaf_down)));
            co_return io_result<>{};
        };
        auto parent = [&](destroy_count_t) -> task<void>
        {
            std::ignore = co_await when_all(
                hop(&sa, destroy_count_t(hop_down)),
                hop(&sb, destroy_count_t(hop_down)));
        };
        run_async(caller)(parent(destroy_count_t(parent_down)));
        pump(q);
        BOOST_TEST(sa && sb);

        capy::abandon(sa);
        BOOST_TEST_EQ(leaf_down, 1);
        BOOST_TEST_EQ(hop_down, 0); // lost on the caller's executor
        BOOST_TEST(!q.empty());
        pump(q);
        BOOST_TEST_EQ(hop_down, 1);
        BOOST_TEST_EQ(parent_down, 0);

        capy::abandon(sb);
        pump(q);
        BOOST_TEST_EQ(leaf_down, 2);
        BOOST_TEST_EQ(hop_down, 2);
        BOOST_TEST_EQ(parent_down, 1);
    }

    // Runners abandoned before they start free their branches before
    // the last loss unwinds the frame that owns the branches' memory.
    void testUnstartedRunnersFreeBeforeResourceOwner()
    {
        for(bool range : {false, true})
        {
            lease_resource mr;
            std::queue<continuation> q;
            bool closed = false;
            int owner_down = 0;
            auto branch = []() -> task<io_result<>>
            {
                co_return io_result<>{};
            };
            auto join = [&]() -> task<void>
            {
                if(range)
                {
                    std::vector<task<io_result<>>> tasks;
                    tasks.push_back(branch());
                    tasks.push_back(branch());
                    std::ignore = co_await when_all(std::move(tasks));
                }
                else
                {
                    std::ignore = co_await when_all(branch(), branch());
                }
            };
            auto owner = [&](destroy_count_t) -> task<void>
            {
                lease l(mr);
                co_await capy::run(&mr)(join());
            };
            run_async(closing_executor{&q, &closed})(
                owner(destroy_count_t(owner_down)));
            BOOST_TEST_EQ(q.size(), 2u);    // both runners posted
            closed = true;
            while(!q.empty())
            {
                auto c = q.front();
                q.pop();
                capy::abandon(c);
            }
            BOOST_TEST_EQ(owner_down, 1);
            BOOST_TEST(mr.balanced());
        }
    }

    // Losing a branch asks the others to stop.
    void testLostBranchRequestsStop()
    {
        parked sa, sb;
        std::stop_token seen;
        auto branch = [](parked* s, std::stop_token* out)
            -> task<io_result<>>
        {
            if(out)
                *out = (co_await this_coro::environment)->stop_token;
            co_await park_awaitable{s};
            co_return io_result<>{};
        };
        auto parent = [&]() -> task<void>
        {
            std::ignore = co_await when_all(
                branch(&sa, nullptr), branch(&sb, &seen));
        };
        int n = 0;
        test_executor ex(n);
        run_async(ex)(parent());
        BOOST_TEST(!seen.stop_requested());
        capy::abandon(sa);
        BOOST_TEST(seen.stop_requested());
        capy::abandon(sb);
    }

    void
    run()
    {
        testAllSucceed();
        testSingleTaskSuccess();
        testOneErrorCancelsSiblings();
        testMultipleErrorsFirstWins();
        testEofWithZeroBytes();
        testPartialTransferIsError();
        testPartialTransferValuePreserved();
        testZeroTransferSuccess();
        testZeroTransferError();
        testOneThrows();
        testMultipleThrowsFirstWins();
        testExceptionBeatsError();
        testExceptionBeatsErrorReversed();
        testCanceledIsNormalError();
        testAllFail();
        testErrorViaSuccessHandler();
        testMixedValueTypes();
        testMultiValueChild();
        testVoidResults();
        testFirstErrorInTimeWins();
        testRunnersDestroyedBeforeStartDestroyParent();
        testTeardownOfAllBranchesDestroysParent();
        testLostBranchParentIsDestroyedNotResumed();
        testLostBranchRequestsStop();
        testUnstartedRunnersFreeBeforeResourceOwner();
        testRunInsideBranchTornDown();
        testStopLosingLastBranch();
        testLostTaskBranchesFreedBeforeResourceOwner();
        testRangeTeardownOfAllBranchesDestroysParent();
        testFinishedThenLostDestroysParent();
        testNestedJoinsTeardown();
        testLostBranchesFreeBeforeAllocatorOwner();
    }
};

TEST_SUITE(
    when_all_io_result_test,
    "boost.capy.when_all_io_result");

} // capy
} // boost
