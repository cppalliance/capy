//
// Copyright (c) 2026 Michael Vandeberg
// Copyright (c) 2026 Steve Gerbino
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Test that header file is self-contained.
#include <boost/capy/when_any.hpp>

#include <boost/capy/cond.hpp>
#include <boost/capy/error.hpp>
#include <boost/capy/ex/async_event.hpp>
#include <boost/capy/ex/abandon.hpp>
#include <boost/capy/ex/run.hpp>
#include <boost/capy/ex/run_async.hpp>
#include <boost/capy/ex/this_coro.hpp>
#include <boost/capy/results.hpp>
#include <boost/capy/task.hpp>

#include "test_helpers.hpp"
#include "test_suite.hpp"

#include <ranges>
#include <atomic>
#include <queue>
#include <stdexcept>
#include <string>
#include <system_error>
#include <type_traits>
#include <variant>
#include <vector>

namespace boost {
namespace capy {

namespace {

task<results<size_t>>
results_success_size(size_t n)
{
    co_return results<size_t>{std::error_code(), n};
}

task<results<size_t>>
results_error_size(std::error_code ec, size_t n = 0)
{
    co_return results<size_t>{ec, n};
}

task<results<std::string>>
results_success_string(std::string s)
{
    co_return results<std::string>{std::error_code(), std::move(s)};
}

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4702) // unreachable code after throw
#endif

task<results<size_t>>
results_throws_size(char const* msg)
{
    throw test_exception(msg);
    co_return results<size_t>{std::error_code(), 0};
}

#if defined(_MSC_VER)
#pragma warning(pop)
#endif

// Suspends until stop token fires, then returns ECANCELED.
task<results<size_t>>
results_pending_size()
{
    co_await stop_only_awaitable{};
    co_return results<size_t>{make_error_code(error::canceled), 0};
}

// Awaitable that completes immediately (await_ready = true)
// returning a successful results<size_t>.
struct immediate_context_awaitable
{
    size_t n_;

    explicit immediate_context_awaitable(size_t n) noexcept
        : n_(n)
    {
    }

    bool await_ready() const noexcept { return true; }

    std::coroutine_handle<>
    await_suspend(std::coroutine_handle<>, await_context const*)
    {
        return std::noop_coroutine();
    }

    results<size_t> await_resume()
    {
        return results<size_t>{std::error_code(), n_};
    }
};

int g_throwing_move_count = 0;

// A payload whose move constructor throws on the third and later moves.
// The results constructor performs the first move and the branch
// taking the awaited result the second; the extract into when_any's
// result variant is the third, inside when_any's try/catch, exercising
// the winner-exception path.
struct throwing_move_payload
{
    int v;
    explicit throwing_move_payload(int x) noexcept : v(x) {}
    throwing_move_payload(throwing_move_payload&& o) : v(o.v)
    {
        if(++g_throwing_move_count >= 3)
            throw std::runtime_error("payload move");
    }
    throwing_move_payload&
    operator=(throwing_move_payload&&) noexcept = default;
};

struct throwing_payload_awaitable
{
    // Suspend (then resume immediately) so the transform await_suspend
    // thunk is exercised for this awaitable type.
    bool await_ready() const noexcept { return false; }
    std::coroutine_handle<>
    await_suspend(std::coroutine_handle<> h, await_context const*)
    {
        return h;
    }
    results<throwing_move_payload> await_resume()
    {
        return results<throwing_move_payload>{
            std::error_code(), throwing_move_payload{7}};
    }
};

task<results<>>
results_void_ok()
{
    co_return results<>{};
}

task<results<>>
results_void_error(std::error_code ec)
{
    co_return results<>{ec};
}

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4702) // unreachable code after throw
#endif
task<results<>>
results_throws_void(char const* msg)
{
    throw test_exception(msg);
    co_return results<>{};
}
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

} // anonymous namespace

struct when_any_vector_test
{
    void
    testSingleTaskSuccess()
    {
        int dc = 0;
        test_executor ex(dc);
        bool completed = false;

        std::vector<task<results<size_t>>> tasks;
        tasks.push_back(results_success_size(42));

        run_async(ex,
            [&](std::variant<std::error_code, std::pair<std::size_t, size_t>> v) {
                completed = true;
                BOOST_TEST_EQ(v.index(), 1u);
                auto [idx, val] = std::get<1>(v);
                BOOST_TEST_EQ(idx, 0u);
                BOOST_TEST_EQ(val, 42u);
            },
            [](std::exception_ptr) {})(
            when_any(std::move(tasks)));

        BOOST_TEST(completed);
    }

    void
    testMultipleTasksFirstSuccessWins()
    {
        int dc = 0;
        test_executor ex(dc);
        bool completed = false;

        std::vector<task<results<size_t>>> tasks;
        tasks.push_back(results_success_size(10));
        tasks.push_back(results_success_size(20));
        tasks.push_back(results_success_size(30));

        run_async(ex,
            [&](std::variant<std::error_code, std::pair<std::size_t, size_t>> v) {
                completed = true;
                BOOST_TEST_EQ(v.index(), 1u);
                auto [idx, val] = std::get<1>(v);
                BOOST_TEST(idx < 3);
                BOOST_TEST_EQ(val, (idx + 1) * 10);
            },
            [](std::exception_ptr) {})(
            when_any(std::move(tasks)));

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
                return results_success_size((i + 1) * 10);
            }, 3);
        static_assert(!std::ranges::forward_range<decltype(tasks)>);

        run_async(ex,
            [&](std::variant<std::error_code, std::pair<std::size_t, size_t>> v) {
                completed = true;
                BOOST_TEST_EQ(v.index(), 1u);
                auto [idx, val] = std::get<1>(v);
                BOOST_TEST_EQ(val, (idx + 1) * 10);
            },
            [](std::exception_ptr) {})(
            when_any(tasks));

        BOOST_TEST(completed);
    }

    // A view yielding its awaitables by value: each lives only for its
    // step of the iteration.
    void
    testPrvalueRange()
    {
        int dc = 0;
        test_executor ex(dc);
        bool completed = false;

        auto tasks = std::views::iota(1, 4) |
            std::views::transform([](int i)
            {
                return results_success_size(static_cast<size_t>(i * 10));
            });

        run_async(ex,
            [&](std::variant<std::error_code, std::pair<std::size_t, size_t>> v) {
                completed = true;
                BOOST_TEST_EQ(v.index(), 1u);
                auto [idx, val] = std::get<1>(v);
                BOOST_TEST_EQ(val, (idx + 1) * 10);
            },
            [](std::exception_ptr) {})(
            when_any(tasks));

        BOOST_TEST(completed);
    }

    void
    testEmptyVectorThrows()
    {
        int dc = 0;
        test_executor ex(dc);
        bool caught = false;

        std::vector<task<results<size_t>>> tasks;

        run_async(ex,
            [](std::variant<std::error_code, std::pair<std::size_t, size_t>>) {},
            [&](std::exception_ptr ep) {
                try {
                    std::rethrow_exception(ep);
                } catch (std::invalid_argument const&) {
                    caught = true;
                }
            })(when_any(std::move(tasks)));

        BOOST_TEST(caught);
    }

    void
    testVoidTasksSuccess()
    {
        int dc = 0;
        test_executor ex(dc);
        bool completed = false;

        std::vector<task<results<>>> tasks;
        tasks.push_back(results_void_ok());
        tasks.push_back(results_void_ok());
        tasks.push_back(results_void_ok());

        run_async(ex,
            [&](std::variant<std::error_code, std::size_t> v) {
                completed = true;
                BOOST_TEST_EQ(v.index(), 1u);
                BOOST_TEST(std::get<1>(v) < 3);
            },
            [](std::exception_ptr) {})(
            when_any(std::move(tasks)));

        BOOST_TEST(completed);
    }

    void
    testVoidTasksAllThrow()
    {
        // Void tasks that throw exercise the void homogeneous state's
        // record_exception. The inline executor makes ordering, and
        // thus which exception is reported last, deterministic.
        int dc = 0;
        test_executor ex(dc);
        bool completed = false;
        bool caught = false;
        std::string msg;

        std::vector<task<results<>>> tasks;
        tasks.push_back(results_throws_void("first"));
        tasks.push_back(results_throws_void("second"));

        run_async(ex,
            [&](std::variant<std::error_code, std::size_t>) {
                completed = true;
            },
            [&](std::exception_ptr ep) {
                try { std::rethrow_exception(ep); }
                catch(test_exception const& e) { caught = true; msg = e.what(); }
            })(when_any(std::move(tasks)));

        BOOST_TEST(!completed);
        BOOST_TEST(caught);
        BOOST_TEST_EQ(msg, "second");
    }

    void
    testErrorDoesNotWin()
    {
        int dc = 0;
        test_executor ex(dc);
        bool completed = false;

        std::vector<task<results<size_t>>> tasks;
        tasks.push_back(results_error_size(make_error_code(error::eof)));
        tasks.push_back(results_success_size(100));

        run_async(ex,
            [&](std::variant<std::error_code, std::pair<std::size_t, size_t>> v) {
                completed = true;
                BOOST_TEST_EQ(v.index(), 1u);
                auto [idx, val] = std::get<1>(v);
                BOOST_TEST_EQ(idx, 1u);
                BOOST_TEST_EQ(val, 100u);
            },
            [](std::exception_ptr) {})(
            when_any(std::move(tasks)));

        BOOST_TEST(completed);
    }

    void
    testAllFailReturnsError()
    {
        int dc = 0;
        test_executor ex(dc);
        bool completed = false;

        std::vector<task<results<size_t>>> tasks;
        tasks.push_back(results_error_size(make_error_code(error::eof)));
        tasks.push_back(results_error_size(make_error_code(error::timeout)));

        run_async(ex,
            [&](std::variant<std::error_code, std::pair<std::size_t, size_t>> v) {
                completed = true;
                BOOST_TEST_EQ(v.index(), 0u);
                BOOST_TEST(!!std::get<0>(v));
            },
            [](std::exception_ptr) {})(
            when_any(std::move(tasks)));

        BOOST_TEST(completed);
    }

    void
    testAllThrowRethrows()
    {
        int dc = 0;
        test_executor ex(dc);
        bool completed = false;
        bool caught = false;
        std::string msg;

        std::vector<task<results<size_t>>> tasks;
        tasks.push_back(results_throws_size("first"));
        tasks.push_back(results_throws_size("second"));

        run_async(ex,
            [&](std::variant<std::error_code, std::pair<std::size_t, size_t>>) {
                completed = true;
            },
            [&](std::exception_ptr ep) {
                try { std::rethrow_exception(ep); }
                catch (test_exception const& e) {
                    caught = true;
                    msg = e.what();
                }
            })(when_any(std::move(tasks)));

        BOOST_TEST(!completed);
        BOOST_TEST(caught);
        BOOST_TEST_EQ(msg, "second");
    }

    void
    testExceptionDoesNotWin()
    {
        int dc = 0;
        test_executor ex(dc);
        bool completed = false;

        std::vector<task<results<size_t>>> tasks;
        tasks.push_back(results_throws_size("boom"));
        tasks.push_back(results_success_size(55));

        run_async(ex,
            [&](std::variant<std::error_code, std::pair<std::size_t, size_t>> v) {
                completed = true;
                BOOST_TEST_EQ(v.index(), 1u);
                auto [idx, val] = std::get<1>(v);
                BOOST_TEST_EQ(idx, 1u);
                BOOST_TEST_EQ(val, 55u);
            },
            [](std::exception_ptr) {})(
            when_any(std::move(tasks)));

        BOOST_TEST(completed);
    }

    // Last failure wins. Error child runs last, so error is reported.
    void
    testLastFailureWins()
    {
        int dc = 0;
        test_executor ex(dc);
        bool completed = false;
        std::error_code result_ec;

        std::vector<task<results<size_t>>> tasks;
        tasks.push_back(results_throws_size("exception"));
        tasks.push_back(results_error_size(make_error_code(error::eof)));

        run_async(ex,
            [&](std::variant<std::error_code, std::pair<std::size_t, size_t>> v) {
                completed = true;
                BOOST_TEST_EQ(v.index(), 0u);
                result_ec = std::get<0>(v);
            },
            [](std::exception_ptr) {})(
            when_any(std::move(tasks)));

        BOOST_TEST(completed);
        BOOST_TEST(result_ec == cond::eof);
    }

    void
    testVoidErrorDoesNotWin()
    {
        int dc = 0;
        test_executor ex(dc);
        bool completed = false;

        std::vector<task<results<>>> tasks;
        tasks.push_back(results_void_error(make_error_code(error::eof)));
        tasks.push_back(results_void_ok());

        run_async(ex,
            [&](std::variant<std::error_code, std::size_t> v) {
                completed = true;
                BOOST_TEST_EQ(v.index(), 1u);
                BOOST_TEST_EQ(std::get<1>(v), 1u);
            },
            [](std::exception_ptr) {})(
            when_any(std::move(tasks)));

        BOOST_TEST(completed);
    }

    void
    testAllTasksCompleteForCleanup()
    {
        int dc = 0;
        test_executor ex(dc);
        std::atomic<int> completion_count{0};
        bool completed = false;

        auto counting = [&](size_t value) -> task<results<size_t>> {
            ++completion_count;
            co_return results<size_t>{std::error_code(), value};
        };

        std::vector<task<results<size_t>>> tasks;
        tasks.push_back(counting(1));
        tasks.push_back(counting(2));
        tasks.push_back(counting(3));
        tasks.push_back(counting(4));

        run_async(ex,
            [&](std::variant<std::error_code, std::pair<std::size_t, size_t>>) {
                completed = true;
            },
            [](std::exception_ptr) {})(
            when_any(std::move(tasks)));

        BOOST_TEST(completed);
        BOOST_TEST_EQ(completion_count.load(), 4);
    }

    void
    testLongLivedTasksCancelled()
    {
        std::queue<continuation> work_queue;
        queuing_executor ex(work_queue);

        std::atomic<int> cancelled_count{0};
        std::atomic<int> completed_normally_count{0};
        bool when_any_completed = false;

        auto fast = [&]() -> task<results<size_t>> {
            ++completed_normally_count;
            co_return results<size_t>{std::error_code(), 42};
        };

        auto slow = [&](size_t id, int steps) -> task<results<size_t>> {
            for (int i = 0; i < steps; ++i) {
                auto token = (co_await this_coro::environment)->stop_token;
                if (token.stop_requested()) {
                    ++cancelled_count;
                    co_return results<size_t>{make_error_code(error::canceled), 0};
                }
                co_await yield_awaitable{};
            }
            ++completed_normally_count;
            co_return results<size_t>{std::error_code(), id};
        };

        std::vector<task<results<size_t>>> tasks;
        tasks.push_back(fast());
        tasks.push_back(slow(100, 10));
        tasks.push_back(slow(200, 10));

        run_async(ex,
            [&](std::variant<std::error_code, std::pair<std::size_t, size_t>> v) {
                when_any_completed = true;
                BOOST_TEST_EQ(v.index(), 1u);
                auto [idx, val] = std::get<1>(v);
                BOOST_TEST_EQ(idx, 0u);
                BOOST_TEST_EQ(val, 42u);
            },
            [](std::exception_ptr) {})(
            when_any(std::move(tasks)));

        pump(work_queue);

        BOOST_TEST(when_any_completed);
        BOOST_TEST_EQ(completed_normally_count.load(), 1);
        BOOST_TEST_EQ(cancelled_count.load(), 2);
    }

    void
    testManyTasks()
    {
        int dc = 0;
        test_executor ex(dc);
        bool completed = false;

        std::vector<task<results<size_t>>> tasks;
        for (size_t i = 1; i <= 20; ++i)
            tasks.push_back(results_success_size(i));

        run_async(ex,
            [&](std::variant<std::error_code, std::pair<std::size_t, size_t>> v) {
                completed = true;
                BOOST_TEST_EQ(v.index(), 1u);
                auto [idx, val] = std::get<1>(v);
                BOOST_TEST(idx < 20);
                BOOST_TEST_EQ(val, idx + 1);
            },
            [](std::exception_ptr) {})(
            when_any(std::move(tasks)));

        BOOST_TEST(completed);
    }

    void
    testNestedWhenAny()
    {
        int dc = 0;
        test_executor ex(dc);
        bool completed = false;
        size_t result = 0;

        auto inner = []() -> task<results<size_t>> {
            std::vector<task<results<size_t>>> tasks;
            tasks.push_back(results_success_size(10));
            tasks.push_back(results_success_size(20));
            auto v = co_await when_any(std::move(tasks));
            if(v.index() == 1)
                co_return results<size_t>{std::error_code(), std::get<1>(v).second};
            co_return results<size_t>{std::get<0>(v), 0};
        };

        std::vector<task<results<size_t>>> outer_tasks;
        outer_tasks.push_back(inner());
        outer_tasks.push_back(inner());

        run_async(ex,
            [&](std::variant<std::error_code, std::pair<std::size_t, size_t>> v) {
                completed = true;
                BOOST_TEST_EQ(v.index(), 1u);
                result = std::get<1>(v).second;
            },
            [](std::exception_ptr) {})(
            when_any(std::move(outer_tasks)));

        BOOST_TEST(completed);
        BOOST_TEST(result == 10 || result == 20);
    }

    void
    run()
    {
        testSingleTaskSuccess();
        testMultipleTasksFirstSuccessWins();
        testPrvalueRange();
        testStashingRange();
        testEmptyVectorThrows();
        testVoidTasksSuccess();
        testVoidTasksAllThrow();

        testErrorDoesNotWin();
        testAllFailReturnsError();
        testAllThrowRethrows();
        testExceptionDoesNotWin();
        testLastFailureWins();
        testVoidErrorDoesNotWin();

        testAllTasksCompleteForCleanup();
        testLongLivedTasksCancelled();

        testManyTasks();
        testNestedWhenAny();
    }
};

TEST_SUITE(
    when_any_vector_test,
    "boost.capy.when_any_vector");

// Tests for results-aware when_any behavior per the combinators spec.
// Each test is labelled with the spec row it verifies.
struct when_any_results_test
{
    // Spec Row 1: First task to return !ec
    // Wins. Cancel siblings. Return winner's result.
    void
    testFirstSuccessWins()
    {
        int dc = 0;
        test_executor ex(dc);
        bool completed = false;
        size_t winner_index = 999;
        size_t result = 0;

        run_async(ex,
            [&](std::variant<std::error_code, size_t, size_t> v) {
                completed = true;
                winner_index = v.index();
                if(v.index() == 1)
                    result = std::get<1>(v);
            },
            [](std::exception_ptr) {})(
            when_any(
                results_success_size(42),
                results_pending_size()));

        BOOST_TEST(completed);
        // Child 0 succeeded -> variant at index 1
        BOOST_TEST_EQ(winner_index, 1u);
        BOOST_TEST_EQ(result, 42u);
    }

    // Spec Row 1 (single child)
    void
    testSingleTaskSuccess()
    {
        int dc = 0;
        test_executor ex(dc);
        bool completed = false;

        run_async(ex,
            [&](std::variant<std::error_code, size_t> v) {
                completed = true;
                BOOST_TEST_EQ(v.index(), 1u);
                BOOST_TEST_EQ(std::get<1>(v), 99u);
            },
            [](std::exception_ptr) {})(
            when_any(results_success_size(99)));

        BOOST_TEST(completed);
    }

    // Spec Row 2: One task returns ec, others pending
    // Does not win. Keep waiting.
    void
    testErrorDoesNotWin()
    {
        int dc = 0;
        test_executor ex(dc);
        bool completed = false;
        size_t winner_index = 999;

        run_async(ex,
            [&](std::variant<std::error_code, size_t, size_t> v) {
                completed = true;
                winner_index = v.index();
                if(v.index() == 2)
                    BOOST_TEST_EQ(std::get<2>(v), 100u);
            },
            [](std::exception_ptr) {})(
            when_any(
                results_error_size(make_error_code(error::eof)),
                results_success_size(100)));

        BOOST_TEST(completed);
        // Child 0 failed, child 1 won -> index 2
        BOOST_TEST_EQ(winner_index, 2u);
    }

    // Spec Row 3: One succeeds, one already failed
    // Successful task wins.
    void
    testSuccessAfterFailure()
    {
        int dc = 0;
        test_executor ex(dc);
        bool completed = false;
        size_t winner_index = 999;
        size_t result = 0;

        run_async(ex,
            [&](std::variant<std::error_code, size_t, size_t> v) {
                completed = true;
                winner_index = v.index();
                if(v.index() == 2)
                    result = std::get<2>(v);
            },
            [](std::exception_ptr) {})(
            when_any(
                results_error_size(make_error_code(error::eof)),
                results_success_size(77)));

        BOOST_TEST(completed);
        BOOST_TEST_EQ(winner_index, 2u);
        BOOST_TEST_EQ(result, 77u);
    }

    // Spec Row 4: All tasks return ec (all fail)
    // No winner. Variant holds error_code at index 0.
    void
    testAllFail()
    {
        int dc = 0;
        test_executor ex(dc);
        bool completed = false;

        run_async(ex,
            [&](std::variant<std::error_code, size_t, size_t> v) {
                completed = true;
                BOOST_TEST_EQ(v.index(), 0u);
                auto ec = std::get<0>(v);
                // Spec: which child's ec is unspecified
                BOOST_TEST(!!ec);
            },
            [](std::exception_ptr) {})(
            when_any(
                results_error_size(make_error_code(error::eof)),
                results_error_size(make_error_code(error::timeout))));

        BOOST_TEST(completed);
    }

    // Spec Row 5: One task throws, others pending
    // Exception does not win. Keep waiting for a success.
    void
    testExceptionDoesNotWin()
    {
        int dc = 0;
        test_executor ex(dc);
        bool completed = false;
        size_t winner_index = 999;

        run_async(ex,
            [&](std::variant<std::error_code, size_t, size_t> v) {
                completed = true;
                winner_index = v.index();
                if(v.index() == 2)
                    BOOST_TEST_EQ(std::get<2>(v), 55u);
            },
            [](std::exception_ptr) {})(
            when_any(
                results_throws_size("boom"),
                results_success_size(55)));

        BOOST_TEST(completed);
        // Child 0 threw (discarded), child 1 won -> index 2
        BOOST_TEST_EQ(winner_index, 2u);
    }

    // Spec Row 6: All tasks throw
    // No success possible. Rethrow first exception.
    void
    testAllThrow()
    {
        int dc = 0;
        test_executor ex(dc);
        bool completed = false;
        bool caught = false;
        std::string msg;

        run_async(ex,
            [&](std::variant<std::error_code, size_t, size_t>) {
                completed = true;
            },
            [&](std::exception_ptr ep) {
                try { std::rethrow_exception(ep); }
                catch (test_exception const& e) {
                    caught = true;
                    msg = e.what();
                }
            })(when_any(
                results_throws_size("first"),
                results_throws_size("second")));

        BOOST_TEST(!completed);
        BOOST_TEST(caught);
        BOOST_TEST_EQ(msg, "second");
    }

    // Spec Row 7: Parent stop fires before any completion
    // All children cancelled. Variant holds error_code at index 0 (ECANCELED).
    void
    testCanceledAllFail()
    {
        int dc = 0;
        test_executor ex(dc);
        bool completed = false;

        run_async(ex,
            [&](std::variant<std::error_code, size_t, size_t> v) {
                completed = true;
                BOOST_TEST_EQ(v.index(), 0u);
                auto ec = std::get<0>(v);
                BOOST_TEST(ec == cond::canceled);
            },
            [](std::exception_ptr) {})(
            when_any(
                results_error_size(make_error_code(error::canceled)),
                results_error_size(make_error_code(error::canceled))));

        BOOST_TEST(completed);
    }

    // Spec Row 8: ec == eof, n == 0
    // Error. Does not win.
    void
    testEofDoesNotWin()
    {
        int dc = 0;
        test_executor ex(dc);
        bool completed = false;
        size_t winner_index = 999;

        run_async(ex,
            [&](std::variant<std::error_code, size_t, size_t> v) {
                completed = true;
                winner_index = v.index();
            },
            [](std::exception_ptr) {})(
            when_any(
                results_error_size(make_error_code(error::eof), 0),
                results_success_size(200)));

        BOOST_TEST(completed);
        // EOF didn't win; child 1 (success) won -> index 2
        BOOST_TEST_EQ(winner_index, 2u);
    }

    // Spec Row 9: Immediate completion (await_ready true)
    // Wins normally. No special treatment.
    void
    testImmediateCompletion()
    {
        int dc = 0;
        test_executor ex(dc);
        bool completed = false;

        run_async(ex,
            [&](std::variant<std::error_code, size_t, size_t> v) {
                completed = true;
                // Immediate awaitable is child 0 -> index 1
                BOOST_TEST_EQ(v.index(), 1u);
                BOOST_TEST_EQ(std::get<1>(v), 77u);
            },
            [](std::exception_ptr) {})(
            when_any(
                immediate_context_awaitable(77),
                results_pending_size()));

        BOOST_TEST(completed);
    }

    // Spec Row 10 (mixed types)
    void
    testMixedTypes()
    {
        int dc = 0;
        test_executor ex(dc);
        bool completed = false;

        run_async(ex,
            [&](std::variant<std::error_code, size_t, std::string> v) {
                completed = true;
                // First child (size_t) succeeds -> index 1
                BOOST_TEST_EQ(v.index(), 1u);
                BOOST_TEST_EQ(std::get<1>(v), 42u);
            },
            [](std::exception_ptr) {})(
            when_any(
                results_success_size(42),
                results_success_string("hello")));

        BOOST_TEST(completed);
    }

    // Single task fails -> variant at index 0
    void
    testSingleTaskError()
    {
        int dc = 0;
        test_executor ex(dc);
        bool completed = false;

        run_async(ex,
            [&](std::variant<std::error_code, size_t> v) {
                completed = true;
                BOOST_TEST_EQ(v.index(), 0u);
                BOOST_TEST(std::get<0>(v) == cond::eof);
            },
            [](std::exception_ptr) {})(
            when_any(
                results_error_size(make_error_code(error::eof))));

        BOOST_TEST(completed);
    }

    // Last failure wins. Error child runs last, so error is reported.
    void
    testLastFailureWins()
    {
        int dc = 0;
        test_executor ex(dc);
        bool completed = false;
        std::error_code result_ec;

        run_async(ex,
            [&](std::variant<std::error_code, size_t, size_t> v) {
                completed = true;
                BOOST_TEST_EQ(v.index(), 0u);
                result_ec = std::get<0>(v);
            },
            [](std::exception_ptr) {})(
            when_any(
                results_throws_size("exception"),
                results_error_size(make_error_code(error::eof))));

        BOOST_TEST(completed);
        BOOST_TEST(result_ec == cond::eof);
    }

    // Winner identification: variant.index() - 1 == winning child index
    void
    testWinnerIndex()
    {
        int dc = 0;
        test_executor ex(dc);
        bool completed = false;

        // Child 0 fails, child 1 wins -> variant.index() == 2
        run_async(ex,
            [&](std::variant<std::error_code, size_t, size_t> v) {
                completed = true;
                BOOST_TEST_EQ(v.index(), 2u);
            },
            [](std::exception_ptr) {})(
            when_any(
                results_error_size(make_error_code(error::eof)),
                results_success_size(42)));

        BOOST_TEST(completed);
    }

    void
    testStopAlreadyRequested()
    {
        // A caller stop token already requested when the (tuple)
        // when_any launches makes the launcher propagate the stop to
        // its own source. A pre-requested stop_source plus an inline
        // executor keeps this deterministic: the pending children
        // resume synchronously.
        int dc = 0;
        test_executor ex(dc);
        std::stop_source src;
        src.request_stop();
        bool done = false;

        run_async(ex, src.get_token(),
            [&](std::variant<std::error_code, size_t, size_t>) {
                done = true;
            },
            [](std::exception_ptr) {})(
            when_any(results_pending_size(), results_pending_size()));

        BOOST_TEST(done);
    }

    void
    testWinnerPayloadMoveThrows()
    {
        // The winning task's payload throws while being moved into the
        // result variant. when_any captures the exception and surfaces it
        // to the awaiter instead of a value.
        g_throwing_move_count = 0;
        int dc = 0;
        test_executor ex(dc);
        bool completed = false;
        bool threw = false;

        run_async(ex,
            [&](std::variant<std::error_code, throwing_move_payload> const&) {
                completed = true;
            },
            [&](std::exception_ptr ep) { threw = (ep != nullptr); })(
            when_any(throwing_payload_awaitable{}));

        BOOST_TEST(!completed);
        BOOST_TEST(threw);
    }

    // Runners destroyed before they start count as lost branches: the
    // parent goes with the last of them.
    void testRunnersDestroyedBeforeStartDestroyParent()
    {
        int a_down = 0, b_down = 0, parent_down = 0;
        std::queue<continuation> q;
        auto branch = [](destroy_count_t) -> task<results<>>
        {
            co_return results<>{};
        };
        auto parent = [&](destroy_count_t) -> task<void>
        {
            std::ignore = co_await when_any(
                branch(destroy_count_t(a_down)),
                branch(destroy_count_t(b_down)));
        };
        run_async(queuing_executor(q))(parent(destroy_count_t(parent_down)));
        BOOST_TEST_EQ(q.size(), 1u);
        auto start = q.front();
        q.pop();
        safe_resume(start.h);     // parent posts both runners
        BOOST_TEST_EQ(q.size(), 2u);
        auto ra = q.front();
        q.pop();
        auto rb = q.front();
        q.pop();

        // An unstarted runner owns no branch: the branches stay with the
        // join until it is unwound.
        capy::abandon(ra);
        pump(q);
        BOOST_TEST_EQ(a_down, 0);
        BOOST_TEST_EQ(b_down, 0);
        BOOST_TEST_EQ(parent_down, 0);

        capy::abandon(rb);
        pump(q);
        BOOST_TEST_EQ(a_down, 1);
        BOOST_TEST_EQ(b_down, 1);
        BOOST_TEST_EQ(parent_down, 1);
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
            -> task<results<>>
        {
            co_await park_awaitable{s};
            co_return results<>{};
        };
        auto join = [&]() -> task<void>
        {
            std::ignore = co_await when_any(
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
            -> task<results<>>
        {
            co_await park_awaitable{s};
            co_return results<>{};
        };
        auto parent = [&](destroy_count_t) -> task<void>
        {
            std::vector<task<results<>>> tasks;
            for(int i = 0; i < count; ++i)
                tasks.push_back(
                    branch(&slots[i], destroy_count_t(down[i])));
            std::ignore = co_await when_any(std::move(tasks));
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

    // A winner does not resume the parent while a sibling is still out;
    // losing that sibling destroys the parent instead.
    void testWinnerThenLostDestroysParent()
    {
        int parent_down = 0;
        bool resumed = false;
        parked sa, sb;
        auto branch = [](parked* s) -> task<results<>>
        {
            co_await park_awaitable{s};
            co_return results<>{};
        };
        auto parent = [&](destroy_count_t) -> task<void>
        {
            std::ignore = co_await when_any(branch(&sa), branch(&sb));
            resumed = true;
        };
        int n = 0;
        test_executor ex(n);
        run_async(ex)(parent(destroy_count_t(parent_down)));
        sa.resume();
        BOOST_TEST(!resumed);
        BOOST_TEST_EQ(parent_down, 0);
        capy::abandon(sb);
        BOOST_TEST(!resumed);
        BOOST_TEST_EQ(parent_down, 1);
    }

    // Both branches torn down from below: the combinator and the
    // parent go with the last one.
    void testTeardownOfAllBranchesDestroysParent()
    {
        int a_down = 0, b_down = 0, parent_down = 0;
        parked sa, sb;
        auto branch = [](parked* s, destroy_count_t)
            -> task<results<>>
        {
            co_await park_awaitable{s};
            co_return results<>{};
        };
        auto parent = [&](destroy_count_t) -> task<void>
        {
            std::ignore = co_await when_any(
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

    // A branch that finishes after a sibling was lost destroys the
    // parent instead of resuming it.
    void testLostBranchParentIsDestroyedNotResumed()
    {
        int parent_down = 0;
        bool resumed = false;
        parked sa, sb;
        auto park = [](parked* s) -> task<results<>>
        {
            co_await park_awaitable{s};
            co_return results<>{};
        };
        auto failing = [](parked* s) -> task<results<>>
        {
            co_await park_awaitable{s};
            co_return results<>{make_error_code(error::canceled)};
        };
        auto parent = [&](destroy_count_t) -> task<void>
        {
            std::ignore = co_await when_any(park(&sa), failing(&sb));
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

    // A branch that would win after its sibling was lost does not
    // resume the parent: the parent goes with the join instead.
    void testSuccessAfterLossDestroysParent()
    {
        int parent_down = 0;
        bool resumed = false;
        parked sa, sb;
        auto branch = [](parked* s) -> task<results<>>
        {
            co_await park_awaitable{s};
            co_return results<>{};
        };
        auto parent = [&](destroy_count_t) -> task<void>
        {
            std::ignore = co_await when_any(branch(&sa), branch(&sb));
            resumed = true;
        };
        int n = 0;
        test_executor ex(n);
        run_async(ex)(parent(destroy_count_t(parent_down)));
        capy::abandon(sa);
        BOOST_TEST_EQ(parent_down, 0);
        sb.resume();
        BOOST_TEST(!resumed);
        BOOST_TEST_EQ(parent_down, 1);
    }

    // Parks the when_any runner itself, so tearing it down enters
    // through the runner's promise rather than through a branch task.
    struct park_context_awaitable : park_awaitable
    {
        results<> await_resume() const noexcept { return {}; }
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
            std::ignore = co_await when_any(
                park_context_awaitable{{a}}, park_context_awaitable{{b}});
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
        auto branch = [&]() -> task<results<>>
        {
            auto tok = (co_await this_coro::environment)->stop_token;
            // Registered before the wait's, so libstdc++ runs it
            // after the wait's and relocks the stop state between.
            std::stop_callback extra(tok, [] {});
            auto [ec] = co_await ev.wait();
            co_return results<>{ec};
        };
        auto parent = [&](destroy_count_t) -> task<void>
        {
            std::ignore = co_await when_any(branch(), branch());
        };
        run_async(closing_executor{&q, &closed}, src.get_token())(
            parent(destroy_count_t(parent_down)));
        pump(q);
        BOOST_TEST_EQ(parent_down, 0);
        closed = true;
        src.request_stop();
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
            auto branch = []() -> task<results<>>
            {
                co_return results<>{};
            };
            auto join = [&]() -> task<void>
            {
                if(range)
                {
                    std::vector<task<results<>>> tasks;
                    tasks.push_back(branch());
                    tasks.push_back(branch());
                    std::ignore = co_await when_any(std::move(tasks));
                }
                else
                {
                    std::ignore = co_await when_any(branch(), branch());
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
            -> task<results<>>
        {
            if(out)
                *out = (co_await this_coro::environment)->stop_token;
            co_await park_awaitable{s};
            co_return results<>{};
        };
        auto parent = [&]() -> task<void>
        {
            std::ignore = co_await when_any(
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
        testTeardownOfAllBranchesDestroysParent();
        testLostBranchParentIsDestroyedNotResumed();
        testSuccessAfterLossDestroysParent();
        testLostBranchRequestsStop();
        testUnstartedRunnersFreeBeforeResourceOwner();
        testStopLosingLastBranch();
        testRunnersDestroyedBeforeStartDestroyParent();
        testLostTaskBranchesFreedBeforeResourceOwner();
        testRangeTeardownOfAllBranchesDestroysParent();
        testWinnerThenLostDestroysParent();
        testLostBranchesFreeBeforeAllocatorOwner();
        testWinnerPayloadMoveThrows();
        testStopAlreadyRequested();
        testFirstSuccessWins();
        testSingleTaskSuccess();
        testErrorDoesNotWin();
        testSuccessAfterFailure();
        testAllFail();
        testExceptionDoesNotWin();
        testAllThrow();
        testCanceledAllFail();
        testEofDoesNotWin();
        testImmediateCompletion();
        testMixedTypes();
        testSingleTaskError();
        testLastFailureWins();
        testWinnerIndex();
    }
};

TEST_SUITE(
    when_any_results_test,
    "boost.capy.when_any_results");

} // capy
} // boost
