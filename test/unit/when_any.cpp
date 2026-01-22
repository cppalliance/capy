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

#include "test_suite.hpp"

#include <atomic>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

namespace boost {
namespace capy {

// Static assertions for result type
static_assert(std::is_same_v<
    when_any_result_type<int>,
    std::pair<std::size_t, std::variant<int>>>);

static_assert(std::is_same_v<
    when_any_result_type<int, std::string>,
    std::pair<std::size_t, std::variant<int, std::string>>>);

// Void becomes monostate in the variant
static_assert(std::is_same_v<
    when_any_result_type<void>,
    std::pair<std::size_t, std::variant<std::monostate>>>);

static_assert(std::is_same_v<
    when_any_result_type<int, void, std::string>,
    std::pair<std::size_t, std::variant<int, std::monostate, std::string>>>);

// Duplicate types are deduplicated (variant requires unique types)
static_assert(std::is_same_v<
    when_any_result_type<int, int, int>,
    std::pair<std::size_t, std::variant<int>>>);

static_assert(std::is_same_v<
    when_any_result_type<int, std::string, int>,
    std::pair<std::size_t, std::variant<int, std::string>>>);

// All void tasks deduplicate to single monostate
static_assert(std::is_same_v<
    when_any_result_type<void, void, void>,
    std::pair<std::size_t, std::variant<std::monostate>>>);

// Verify when_any returns task which satisfies awaitable protocols
static_assert(IoAwaitable<
    task<when_any_result_type<int, int>>,
    executor_ref>);

// Minimal test context
class test_context : public execution_context
{
};

static test_context default_test_ctx_;

/** Simple synchronous executor for testing.
*/
struct test_executor
{
    int* dispatch_count_;
    test_context* ctx_ = nullptr;

    /**
     * @brief Constructs a test executor that records dispatch calls.
     *
     * The executor will increment the provided counter each time its `dispatch`
     * method is invoked.
     *
     * @param count Reference to an integer used to track the number of dispatches.
     */
    explicit test_executor(int& count)
        : dispatch_count_(&count)
    {
    }

    /**
     * @brief Checks whether two test_executor instances refer to the same dispatch counter.
     *
     * @param other The other executor to compare against.
     * @return true if both executors share the same `dispatch_count_` pointer, false otherwise.
     */
    bool operator==(test_executor const& other) const noexcept
    {
        return dispatch_count_ == other.dispatch_count_;
    }

    /**
     * @brief Obtain the execution context used by this executor.
     *
     * Returns the executor's stored execution_context if one was provided; otherwise returns the global default_test_ctx_.
     *
     * @return execution_context& Reference to the active execution context for this executor.
     */
    execution_context& context() const noexcept
    {
        return ctx_ ? *ctx_ : default_test_ctx_;
    }

    /**
 * @brief Notifies the executor that a unit of work has started.
 *
 * This implementation is a no-op; it exists to satisfy the executor interface.
 */
void on_work_started() const noexcept {}
    /**
 * @brief Notifies that a unit of work has finished on the executor.
 *
 * This executor implementation does not track active work, so the hook is a no-op.
 */
void on_work_finished() const noexcept {}

    /**
     * @brief Marks a coroutine as dispatched by incrementing the dispatch counter and returning the handle.
     *
     * Increments the executor's internal dispatch counter to record a dispatch operation.
     *
     * @param h Coroutine handle to be dispatched.
     * @return coro The same coroutine handle `h`.
     */
    coro dispatch(coro h) const
    {
        ++(*dispatch_count_);
        return h;
    }

    /**
     * @brief Immediately resumes the provided coroutine handle.
     *
     * @param h Coroutine handle to resume; the function will call `resume()` on it.
     */
    void post(coro h) const
    {
        h.resume();
    }
};

static_assert(Executor<test_executor>);

struct test_exception : std::runtime_error
{
    /**
     * @brief Constructs a test_exception with the specified message.
     *
     * @param msg Null-terminated error message stored in the exception.
     */
    explicit test_exception(const char* msg)
        : std::runtime_error(msg)
    {
    }
};

/**
 * @brief Throws a test_exception constructed with the provided message.
 *
 * @param msg Message forwarded to the thrown test_exception.
 * @throws test_exception Always throws a test_exception initialized with `msg`.
 */
[[noreturn]] inline void
throw_test_exception(char const* msg)
{
    throw test_exception(msg);
}

//----------------------------------------------------------
// Shared helper tasks for all when_any tests
/**
 * @brief Creates a task that completes with the specified integer.
 *
 * @param value Integer to be returned by the task.
 * @return task<int> A task whose result is the provided `value`.
 */

inline task<int>
returns_int(int value)
{
    co_return value;
}

/**
 * @brief Creates a task that completes with the provided string.
 *
 * @param value The string value that the task will produce.
 * @return std::string The provided string value.
 */
inline task<std::string>
returns_string(std::string value)
{
    co_return value;
}

/**
 * @brief Creates a task that completes immediately without producing a value.
 *
 * @return task<void> A task that is already completed and yields no value.
 */
inline task<void>
void_task()
{
    co_return;
}

/**
 * @brief Coroutine that immediately throws a test_exception with the provided message.
 *
 * This task does not produce a normal integer result; it signals failure by throwing
 * a test_exception constructed from `msg`.
 *
 * @param msg Human-readable message used to construct the thrown test_exception.
 * @return task<int> An awaitable task which, when resumed, will propagate the thrown test_exception.
 * @throws test_exception Always thrown with the given `msg`.
 */
inline task<int>
throws_exception(char const* msg)
{
    throw_test_exception(msg);
    co_return 0;
}

/**
 * @brief Returns a task that immediately throws a test_exception with the given message.
 *
 * The returned coroutine does not produce a value; awaiting it will propagate a
 * test_exception constructed from `msg`.
 *
 * @param msg Null-terminated message used for the thrown test_exception.
 */
inline task<void>
void_throws_exception(char const* msg)
{
    throw_test_exception(msg);
    co_return;
}

//----------------------------------------------------------
// Shared executors and awaitables for all when_any tests
//----------------------------------------------------------

/** Queuing executor that allows controlled interleaving of tasks.

    Unlike test_executor which runs tasks synchronously, this executor
    queues work and runs it in FIFO order when run_one() is called.
    This allows tasks to observe stop requests between suspension points.
*/
struct queuing_executor
{
    std::vector<coro>* queue_;
    test_context* ctx_ = nullptr;

    /**
     * @brief Constructs a queuing_executor that uses the provided coroutine queue.
     *
     * The executor enqueues dispatched or posted coroutine handles into the supplied vector.
     *
     * @param q Vector used as the FIFO queue for coroutine handles; must outlive the executor.
     */
    explicit queuing_executor(std::vector<coro>& q)
        : queue_(&q)
    {
    }

    /**
     * @brief Compares two queuing_executor instances for equality.
     *
     * @returns `true` if both executors refer to the same underlying queue, `false` otherwise.
     */
    bool operator==(queuing_executor const& other) const noexcept
    {
        return queue_ == other.queue_;
    }

    /**
     * @brief Obtain the execution context used by this executor.
     *
     * Returns the executor's stored execution_context if one was provided; otherwise returns the global default_test_ctx_.
     *
     * @return execution_context& Reference to the active execution context for this executor.
     */
    execution_context& context() const noexcept
    {
        return ctx_ ? *ctx_ : default_test_ctx_;
    }

    /**
 * @brief Notifies the executor that a unit of work has started.
 *
 * This implementation is a no-op; it exists to satisfy the executor interface.
 */
void on_work_started() const noexcept {}
    /**
 * @brief Notifies that a unit of work has finished on the executor.
 *
 * This executor implementation does not track active work, so the hook is a no-op.
 */
void on_work_finished() const noexcept {}

    /**
     * @brief Enqueues a coroutine handle for later execution by this executor.
     *
     * @param h Coroutine handle to append to the executor's internal queue.
     * @return coro A no-op coroutine handle (`std::noop_coroutine`) offered back to the caller.
     */
    coro dispatch(coro h) const
    {
        queue_->push_back(h);
        return std::noop_coroutine();
    }

    /**
     * @brief Enqueues a coroutine handle onto the executor's queue for later execution.
     *
     * @param h Coroutine handle to append to the queue.
     */
    void post(coro h) const
    {
        queue_->push_back(h);
    }
};

static_assert(Executor<queuing_executor>);

/** Awaitable that yields to the executor, allowing other tasks to run.

    When awaited, this suspends the current coroutine and posts it back
    to the executor's queue. This creates a yield point where the task
    can be interleaved with other tasks.
*/
struct yield_awaitable
{
    /**
     * @brief Indicates that the awaitable will always suspend.
     *
     * @return `false` indicating the awaitable is not ready and the coroutine should suspend.
     */
    bool await_ready() const noexcept
    {
        return false;
    }

    template<typename Ex>
    /**
     * @brief Suspends the awaiting coroutine and re-posts it to the given executor's queue.
     *
     * Posts the coroutine handle back to the executor via ex.post(h) and yields by returning
     * a no-op coroutine handle.
     *
     * @tparam Ex Executor type providing a `post(coro)` overload.
     * @param h The awaiting coroutine handle to be re-scheduled.
     * @param ex The executor used to re-post the coroutine.
     * @param st Stop token passed by the caller (unused).
     * @return coro A no-op coroutine handle used to complete suspension.
     */
    coro await_suspend(coro h, Ex const& ex, std::stop_token)
    {
        // Post ourselves back to the queue
        ex.post(h);
        return std::noop_coroutine();
    }

    /**
     * @brief No-op resume step invoked when the awaitable resumes.
     *
     * This function intentionally performs no action and exists only to satisfy the awaitable resume hook.
     */
    void await_resume() const noexcept
    {
    }
};

struct when_any_test
{
    //----------------------------------------------------------
    // Basic functionality tests
    //----------------------------------------------------------

    /**
     * @brief Verifies that a single-task when_any completes immediately with the expected index and value.
     *
     * Runs a when_any over a single coroutine that returns 42 and asserts that the callback is invoked,
     * the reported winner index is 0, and the extracted result is 42.
     */
    void
    testSingleTask()
    {
        int dispatch_count = 0;
        test_executor ex(dispatch_count);
        bool completed = false;
        int result = 0;
        std::size_t winner_index = 999;

        run_async(ex,
            [&](when_any_result_type<int> r) {
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

        // Note: when_any_result_type<int, int> deduplicates to variant<int>
        run_async(ex,
            [&](when_any_result_type<int, int> r) {
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

    /**
     * @brief Verifies when_any reports the correct winner and value for mixed result types.
     *
     * Runs when_any on three tasks producing `int`, `std::string`, and `int`, captures the winner
     * index and the variant-held result, and asserts that a completion occurred, the winner index
     * is 0, 1, or 2, and the variant contains the corresponding value for the reported winner.
     */
    void
    testMixedTypes()
    {
        int dispatch_count = 0;
        test_executor ex(dispatch_count);
        bool completed = false;
        std::size_t winner_index = 999;
        std::variant<int, std::string> result_value;

        run_async(ex,
            [&](when_any_result_type<int, std::string, int> r) {
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
            [&](when_any_result_type<void, int> r) {
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

    /**
     * @brief Verifies that when_any with only void tasks reports a valid winner index and yields a monostate result.
     *
     * Runs three void-returning tasks in a when_any and asserts the operation completes, the reported winner index is 0, 1, or 2, and the returned variant holds a `std::monostate`.
     */
    void
    testAllVoidTasks()
    {
        int dispatch_count = 0;
        test_executor ex(dispatch_count);
        bool completed = false;
        std::size_t winner_index = 999;
        std::variant<std::monostate> result_value;

        run_async(ex,
            [&](when_any_result_type<void, void, void> r) {
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

    /**
     * @brief Verifies that an exception thrown by the single task passed to when_any is propagated to the exception handler.
     *
     * @details Invokes when_any with a single coroutine that throws a test_exception and installs handlers that set flags
     * indicating normal completion or exception capture. Asserts that the normal completion handler is not called,
     * the exception handler is invoked, and the captured exception message equals "test error".
     */
    void
    testSingleTaskException()
    {
        int dispatch_count = 0;
        test_executor ex(dispatch_count);
        bool completed = false;
        bool caught_exception = false;
        std::string error_msg;

        run_async(ex,
            [&](when_any_result_type<int>) { completed = true; },
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

    /**
     * @brief Verifies that a thrown exception from one participant of when_any is reported as the winning completion.
     *
     * Sets up a synchronous test executor and runs when_any with one coroutine that throws and one that returns an int.
     * Confirms the exception path is invoked and the propagated exception carries the expected message.
     */
    void
    testExceptionWinsRace()
    {
        int dispatch_count = 0;
        test_executor ex(dispatch_count);
        bool caught_exception = false;
        std::string error_msg;

        run_async(ex,
            [](when_any_result_type<int, int>) {},
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

    /**
     * @brief Verifies that an exception thrown by a void task in when_any is propagated to the awaiter.
     *
     * Runs a void task that throws and a concurrent int-returning task under a test executor,
     * awaits their when_any composition, and asserts the thrown test_exception is caught with the expected message.
     */
    void
    testVoidTaskException()
    {
        int dispatch_count = 0;
        test_executor ex(dispatch_count);
        bool caught_exception = false;
        std::string error_msg;

        run_async(ex,
            [](when_any_result_type<void, int>) {},
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

    /**
     * @brief Verifies that when_any delivers the exception from the task that completes first when multiple tasks throw.
     *
     * Runs three tasks that each throw a test_exception and asserts that an exception is observed
     * and that the delivered exception message matches one of the thrown messages.
     */
    void
    testMultipleExceptionsFirstWins()
    {
        int dispatch_count = 0;
        test_executor ex(dispatch_count);
        bool caught_exception = false;
        std::string error_msg;

        run_async(ex,
            [](when_any_result_type<int, int, int>) {},
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

    /**
     * @brief Verifies that completing the winning awaitable triggers stop requests for the others
     *
     * Runs three synchronous tasks with when_any and checks that the when_any completion
     * causes stop to be requested on the remaining awaitables while all tasks still run
     * to completion in the synchronous executor scenario.
     */
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
            [&](when_any_result_type<int, int, int>) {
                completed = true;
            },
            [](std::exception_ptr) {})(
            when_any(counting_task(), counting_task(), counting_task()));

        BOOST_TEST(completed);
        // All three tasks should run to completion
        // (stop is requested, but synchronous tasks complete anyway)
        BOOST_TEST_EQ(completion_count.load(), 3);
    }

    /**
     * @brief Verifies that all participating tasks complete for cleanup even after a winner is determined.
     *
     * Runs a four-task `when_any` on a synchronous test executor, asserts the reported winner index is 0,
     * and verifies that all four tasks have completed (cleanup semantics).
     */
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
            [&](when_any_result_type<int, int, int, int> r) {
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

    /**
     * @brief Verifies that long-lived tasks are cancelled when a faster task wins a when_any race.
     *
     * Constructs a when_any composed of one immediately completing task and two multi-step tasks
     * that cooperatively observe the stop token. Asserts that the fast task is reported as the
     * winner (index 0) with the expected value, that only the fast task completed normally,
     * and that the slow tasks saw stop requests and were cancelled.
     *
     * Expected assertions:
     * - winner index is 0 and winner value is 42.
     * - completed_normally_count == 1.
     * - cancelled_count == 2.
     */
    void
    testLongLivedTasksCancelledOnWinner()
    {
        std::vector<coro> work_queue;
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
                auto token = co_await get_stop_token();
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
            [&](when_any_result_type<int, int, int> r) {
                when_any_completed = true;
                winner_index = r.first;
                winner_value = std::get<int>(r.second);
            },
            [](std::exception_ptr) {})(
            when_any(fast_task(), slow_task(100, 10), slow_task(200, 10)));

        // Process work queue until empty
        while (!work_queue.empty()) {
            auto h = work_queue.front();
            work_queue.erase(work_queue.begin());
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

    /**
     * @brief Verifies that a slower task can still win a when_any race if it completes first.
     *
     * Sets up three cooperative tasks with differing step counts, runs them on a queuing executor
     * with FIFO scheduling, and asserts that the task which finishes first is reported as the winner,
     * that its value is propagated, and that the remaining tasks observe a stop request and are cancelled.
     *
     * The test specifically checks:
     * - the reported winner index and value match the task that completed first,
     * - exactly one task completed normally,
     * - the other tasks were cancelled.
     */
    void
    testSlowTaskCanWin()
    {
        std::vector<coro> work_queue;
        queuing_executor ex(work_queue);

        std::atomic<int> cancelled_count{0};
        std::atomic<int> completed_normally_count{0};
        bool when_any_completed = false;
        std::size_t winner_index = 999;
        int winner_value = 0;

        // A task that does a few steps then completes
        auto medium_task = [&](int id, int steps) -> task<int> {
            for (int i = 0; i < steps; ++i) {
                auto token = co_await get_stop_token();
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
            [&](when_any_result_type<int, int, int> r) {
                when_any_completed = true;
                winner_index = r.first;
                winner_value = std::get<int>(r.second);
            },
            [](std::exception_ptr) {})(
            when_any(medium_task(10, 3), medium_task(20, 1), medium_task(30, 4)));

        // Process work queue until empty
        while (!work_queue.empty()) {
            auto h = work_queue.front();
            work_queue.erase(work_queue.begin());
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

    /**
     * @brief Ensures non-cooperative tasks complete even if another task wins a when_any race.
     *
     * Sets up a queuing executor with one immediately-completing task and two tasks that
     * ignore stop tokens and yield repeatedly. Verifies the fast task wins the when_any
     * race and that all tasks (including the non-cooperative ones) run to completion.
     */
    void
    testNonCooperativeTasksStillComplete()
    {
        std::vector<coro> work_queue;
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
            [&](when_any_result_type<int, int, int> r) {
                when_any_completed = true;
                BOOST_TEST_EQ(r.first, 0u);  // fast_task wins
            },
            [](std::exception_ptr) {})(
            when_any(fast_task(), non_cooperative_task(100, 3), non_cooperative_task(200, 3)));

        // Process work queue until empty
        while (!work_queue.empty()) {
            auto h = work_queue.front();
            work_queue.erase(work_queue.begin());
            h.resume();
        }

        BOOST_TEST(when_any_completed);

        // All three tasks complete (non-cooperative tasks run to completion)
        BOOST_TEST_EQ(completion_count.load(), 3);
    }

    /**
     * @brief Verifies when_any behavior with a mix of cooperative and non-cooperative tasks.
     *
     * Runs three tasks on a queuing executor: a fast winning task, a cooperative slow task that checks the stop token,
     * and a non-cooperative slow task that does not observe cancellation. Asserts that the fast task wins (reported index 0),
     * the cooperative task observes cancellation and increments its cancelled counter, and the non-cooperative task still
     * runs to completion. Also asserts the winner ran once and the when_any completion callback executed.
     */
    void
    testMixedCooperativeAndNonCooperativeTasks()
    {
        std::vector<coro> work_queue;
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
                auto token = co_await get_stop_token();
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
            [&](when_any_result_type<int, int, int> r) {
                when_any_completed = true;
                BOOST_TEST_EQ(r.first, 0u);
            },
            [](std::exception_ptr) {})(
            when_any(fast_task(), cooperative_slow(5), non_cooperative_slow(5)));

        while (!work_queue.empty()) {
            auto h = work_queue.front();
            work_queue.erase(work_queue.begin());
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

    /**
     * @brief Verifies that a nested when_any correctly reports the winning task and its value.
     *
     * Creates two inner when_any operations that each yield an `int`, runs an outer when_any
     * over those inner tasks, and asserts the reported winner index and the integer value
     * returned by the winning inner task.
     */
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
            [&](when_any_result_type<int, int> r) {
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

    /**
     * @brief Verifies that a when_any combinator nested inside a when_all correctly reports winners.
     *
     * Executes two independent when_any races inside a when_all and asserts that the combined
     * awaiter receives the winner values from each race and that the overall operation completes.
     *
     * The test checks that the first result is either 1 or 2 and the second result is either 3 or 4,
     * and that the completion callback is invoked.
     */
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

    /**
     * @brief Verifies that a when_any composed of two tasks that each await when_all
     * correctly reports the winning task and its combined integer result.
     *
     * Spawns two tasks where each task awaits when_all on two int-returning tasks and returns their sum.
     * Executes when_any over these tasks and asserts that the completion callback observes a valid
     * winner index (0 or 1) and the corresponding summed value (3 for the first task, 7 for the second).
     */
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
            [&](when_any_result_type<int, int> r) {
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

    /**
     * @brief Verifies when_any with many integer-returning tasks reports the correct winner.
     *
     * Runs when_any over eight tasks that return integers 1..8 and asserts that a winner
     * is reported (completed == true), the winner index is within range, and the returned
     * integer matches the mapping index -> value (index 0 -> 1, index 1 -> 2, etc.).
     */
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

    /**
     * @brief Creates a task that accumulates two awaited integer results onto an initial value.
     *
     * @param start Initial integer value to accumulate onto.
     * @return int Final accumulated value: start + 1 + 2.
     */
    static task<int>
    multi_step_task(int start)
    {
        int value = start;
        value += co_await returns_int(1);
        value += co_await returns_int(2);
        co_return value;
    }

    /**
     * @brief Tests that when_any correctly selects the first-completing task across multi-step tasks.
     *
     * Runs two multi-step tasks (producing ints) via when_any and verifies that a winner index of 0 or 1
     * is reported and that the associated value matches the expected sum for the winning task.
     *
     * Expected outcomes:
     * - winner index is either 0 or 1.
     * - if winner index == 0, reported value is 13 (10 + 1 + 2).
     * - if winner index == 1, reported value is 23 (20 + 1 + 2).
     */
    void
    testTasksWithMultipleSteps()
    {
        int dispatch_count = 0;
        test_executor ex(dispatch_count);
        bool completed = false;
        std::size_t winner_index = 999;
        int result_value = 0;

        run_async(ex,
            [&](when_any_result_type<int, int> r) {
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

    /**
     * @brief Verifies that a `when_any` awaitable can be move-constructed and still be awaited.
     *
     * Creates a `when_any` awaitable for two int-returning tasks, move-constructs it, awaits
     * the moved awaitable via the test executor, and asserts that a valid winner index and
     * corresponding int value are produced.
     */
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
            [&](when_any_result_type<int, int> r) {
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

    /**
     * @brief Verifies that a when_any awaitable can be stored and awaited at a later time.
     *
     * Creates two integer-returning tasks, stores their combined when_any awaitable, then
     * passes it to a delayed awaiter. Asserts that the awaiter observes a valid winner
     * index (0 or 1) and that the contained integer matches the corresponding task value.
     */
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
            [&](when_any_result_type<int, int> r) {
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
    // Protocol compliance tests
    //----------------------------------------------------------

    /**
     * @brief Ensures compile-time conformance of when_any results to the IoAwaitable concept.
     *
     * Performs static assertions that `task<when_any_result_type<...>>` satisfies
     * `IoAwaitable<..., executor_ref>` for representative type combinations
     * (homogeneous, heterogeneous, and `void`-containing results).
     */
    void
    testIoAwaitableConcept()
    {
        static_assert(IoAwaitable<
            task<when_any_result_type<int, int>>,
            executor_ref>);

        static_assert(IoAwaitable<
            task<when_any_result_type<int, std::string>>,
            executor_ref>);

        static_assert(IoAwaitable<
            task<when_any_result_type<void>>,
            executor_ref>);

        static_assert(IoAwaitable<
            task<when_any_result_type<void, int, void>>,
            executor_ref>);
    }

    //----------------------------------------------------------
    // Variant access tests
    //----------------------------------------------------------

    /**
     * @brief Verifies that when_any populates the correct variant alternative for deduplicated result types.
     *
     * This test invokes when_any with tasks producing `int`, `std::string`, and `int` (duplicate `int`).
     * It asserts that duplicate types are deduplicated in the resulting variant, the winner index is 0
     * for a synchronous executor, and the variant holds the expected `int` value (42).
     */
    void
    testVariantAlternativePopulated()
    {
        int dispatch_count = 0;
        test_executor ex(dispatch_count);
        bool completed = false;

        // Note: <int, string, int> deduplicates to variant<int, string>
        run_async(ex,
            [&](when_any_result_type<int, std::string, int> r) {
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

    /**
     * @brief Verifies that when_any's result variant holds the correct alternative for int and string tasks.
     *
     * Runs two tasks (one returning `int`, one returning `std::string`) with `when_any` and asserts that:
     * - the combinator completes,
     * - the reported winner index is either 0 or 1,
     * - the variant contains the corresponding value (`int` when index 0, `std::string` when index 1).
     */
    void
    testVariantVisit()
    {
        int dispatch_count = 0;
        test_executor ex(dispatch_count);
        bool completed = false;
        std::size_t winner_index = 999;
        std::variant<int, std::string> result_value;

        run_async(ex,
            [&](when_any_result_type<int, std::string> r) {
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

    /**
     * @brief Verifies that a parent stop token already requested before starting when_any
     *
     * Tests that when a parent stop token is requested prior to invoking when_any, each
     * child task observes the stop request (via get_stop_token) on its first suspension
     * and the when_any operation completes with a reported winner index.
     *
     * The test asserts that when_any completes and that all launched tasks saw the
     * stop token as requested.
     */
    void
    testParentStopAlreadyRequested()
    {
        std::vector<coro> work_queue;
        queuing_executor ex(work_queue);

        std::atomic<int> saw_stop_count{0};
        bool when_any_completed = false;
        std::size_t winner_index = 999;

        // A task that checks stop token on first suspension
        auto check_stop_task = [&](int id) -> task<int> {
            auto token = co_await get_stop_token();
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
            [&](when_any_result_type<int, int, int> r) {
                when_any_completed = true;
                winner_index = r.first;
            },
            [](std::exception_ptr) {})(
            when_any(check_stop_task(1), check_stop_task(2), check_stop_task(3)));

        while (!work_queue.empty()) {
            auto h = work_queue.front();
            work_queue.erase(work_queue.begin());
            h.resume();
        }

        BOOST_TEST(when_any_completed);
        // All tasks should have seen the stop token as requested
        // (inherited from parent)
        BOOST_TEST_EQ(saw_stop_count.load(), 3);
    }

    /**
     * @brief Verifies that a parent stop request during task execution cancels all child tasks and completes when_any.
     *
     * Spawns two cooperative long-running tasks via when_any on a queuing executor, starts a few scheduling iterations,
     * then requests stop from the parent stop_source. Observes that the when_any completion callback runs and that both
     * tasks observe the stop request and increment the cancellation counter.
     *
     * Observable effects:
     * - Sets the external flag indicating when_any completed.
     * - Increments the provided atomic cancellation counter for each task that observes the stop.
     */
    void
    testParentStopDuringExecution()
    {
        std::vector<coro> work_queue;
        queuing_executor ex(work_queue);

        std::atomic<int> cancelled_count{0};
        bool when_any_completed = false;

        auto slow_task = [&](int id, int steps) -> task<int> {
            for (int i = 0; i < steps; ++i) {
                auto token = co_await get_stop_token();
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
            [&](when_any_result_type<int, int>) {
                when_any_completed = true;
            },
            [](std::exception_ptr) {})(
            when_any(slow_task(1, 10), slow_task(2, 10)));

        // Run a few iterations, then request parent stop
        for (int i = 0; i < 3 && !work_queue.empty(); ++i) {
            auto h = work_queue.front();
            work_queue.erase(work_queue.begin());
            h.resume();
        }

        // Request stop from parent
        parent_stop.request_stop();

        // Finish processing
        while (!work_queue.empty()) {
            auto h = work_queue.front();
            work_queue.erase(work_queue.begin());
            h.resume();
        }

        BOOST_TEST(when_any_completed);
        // Both tasks should have been cancelled by parent stop
        BOOST_TEST_EQ(cancelled_count.load(), 2);
    }

    //----------------------------------------------------------
    // Interleaved exception tests
    //----------------------------------------------------------

    /**
     * @brief Verifies that when_any reports the first exception produced when multiple tasks throw.
     *
     * Schedules three tasks that each throw a test_exception after a configurable number of yields,
     * runs them on a queuing executor until completion, and asserts that the first-thrown exception
     * is observed and carries the expected message ("error_2").
     */
    void
    testInterleavedExceptions()
    {
        std::vector<coro> work_queue;
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
            [](when_any_result_type<int, int, int>) {},
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
            work_queue.erase(work_queue.begin());
            h.resume();
        }

        BOOST_TEST(caught_exception);
        // Task 2 throws first (after 1 yield)
        BOOST_TEST_EQ(error_msg, "error_2");
    }

    //----------------------------------------------------------
    // Nested stop propagation tests
    //----------------------------------------------------------

    /**
     * @brief Tests that stop requests propagate to a nested when_any so the outer task is cancelled before launching the inner work.
     *
     * Verifies that when one branch of a when_any completes quickly, other branches observe the stop request:
     * the fast task should win the race, the when_any completion callback should record the winner index 0,
     * and the nested branch should observe the stop token and increment the cancellation counter instead of running inner logic.
     */
    void
    testNestedStopPropagationOuterCancelled()
    {
        std::vector<coro> work_queue;
        queuing_executor ex(work_queue);

        std::atomic<int> outer_cancelled{0};
        bool when_any_completed = false;
        std::size_t winner_index = 999;

        auto fast_task = [&]() -> task<int> {
            co_return 42;
        };

        // A task that checks stop before launching inner when_any
        auto nested_when_any_task = [&]() -> task<int> {
            auto token = co_await get_stop_token();
            if (token.stop_requested()) {
                ++outer_cancelled;
                co_return -1;
            }
            // Won't reach here if stopped
            co_return 100;
        };

        run_async(ex,
            [&](when_any_result_type<int, int> r) {
                when_any_completed = true;
                winner_index = r.first;
            },
            [](std::exception_ptr) {})(
            when_any(fast_task(), nested_when_any_task()));

        while (!work_queue.empty()) {
            auto h = work_queue.front();
            work_queue.erase(work_queue.begin());
            h.resume();
        }

        BOOST_TEST(when_any_completed);
        BOOST_TEST_EQ(winner_index, 0u);  // fast_task wins
        // The nested task should see stop and exit early
        BOOST_TEST_EQ(outer_cancelled.load(), 1);
    }

    /**
     * @brief Tests that stop requests propagate into a nested when_any so its children observe cancellation.
     *
     * Sets up a nested when_any inside a task and races it against a fast yielding task using a queuing_executor.
     * Observes the resulting winner and verifies cancellation/completion counts on the nested children:
     * - If the outer fast task wins, both inner tasks must observe a stop request (inner_cancelled == 2, inner_completed == 0).
     * - If the nested when_any wins, one inner task completes and the other is cancelled (inner_completed == 1, inner_cancelled == 1).
     *
     * The test also asserts that the outer when_any completes and reports a valid winner index (0 or 1).
     */
    void
    testNestedStopPropagationInnerCancelled()
    {
        std::vector<coro> work_queue;
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
                auto token = co_await get_stop_token();
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
            [&](when_any_result_type<int, int> r) {
                when_any_completed = true;
                winner_index = r.first;
            },
            [](std::exception_ptr) {})(
            when_any(yielding_fast_task(), nested_when_any_task()));

        while (!work_queue.empty()) {
            auto h = work_queue.front();
            work_queue.erase(work_queue.begin());
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

    /**
     * @brief Verifies accessing a when_any result variant by the reported index.
     *
     * Confirms that the index in a when_any_result_type<int, std::string, double>
     * correctly identifies which variant alternative is active and that the stored
     * value matches the expected value for each alternative.
     */
    void
    testVariantAccessByIndex()
    {
        int dispatch_count = 0;
        test_executor ex(dispatch_count);
        bool completed = false;
        bool correct_access = false;

        run_async(ex,
            [&](when_any_result_type<int, std::string, double> r) {
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

    /**
     * @brief Tests that when_any deduplicates identical result types in the variant
     * while preserving the task index to disambiguate which task completed.
     *
     * Sets up three int-returning tasks passed to when_any and awaits the result.
     * Verifies the returned variant holds an `int` (deduplicated alternative) and
     * the accompanying index identifies which of the original tasks completed.
     * Also verifies completion and the expected value for a synchronous executor
     * (the first task should win).
     */
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
            [&](when_any_result_type<int, int, int> r) {
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

    /**
     * @brief Executes the complete when_any test suite.
     *
     * Invokes every individual test case in a fixed sequence to validate
     * functionality, exception handling, stop-token propagation, cancellation,
     * nested combinators, edge cases, awaitable lifecycle, protocol compliance,
     * and variant access behaviors.
     */
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

        // Protocol compliance
        testIoAwaitableConcept();

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

    /**
     * @brief Tests when_any with a vector of int-returning tasks and verifies the reported winner.
     *
     * @details Constructs three tasks that return 10, 20, and 30 respectively, runs them with a
     * test executor via when_any(std::vector<task<int>>), and asserts that:
     * - the combinator completes,
     * - the reported winner index is within the vector range,
     * - the returned value matches the expected mapping (index 0 -> 10, index 1 -> 20, index 2 -> 30).
     */
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

    /**
     * @brief Verifies that calling when_any with an empty vector of tasks results in an `std::invalid_argument` exception.
     *
     * Constructs an empty `std::vector<task<int>>`, invokes `when_any` with it via `run_async`, and asserts that an
     * `std::invalid_argument` is propagated to the error handler.
     */
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

    /**
     * @brief Tests that when_any on a vector of void tasks completes and reports a valid winner.
     *
     * Runs when_any over three void-returning tasks and verifies the continuation is invoked
     * and the reported winner index is within the bounds of the task vector (0..2).
     */
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

    /**
     * @brief Verifies that an exception thrown by a task in a vector passed to `when_any` is propagated to the awaiter.
     *
     * Schedules a single `task<int>` that throws `test_exception`, awaits `when_any` on a vector containing that task,
     * and asserts the awaiter receives the exception with the expected message.
     */
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

    /**
     * @brief Verifies that an exception from a vector task wins the when_any race and is propagated.
     *
     * Constructs a vector of three tasks (one that throws a test_exception and two that return ints),
     * runs a vector-based when_any, and asserts that the thrown test_exception is observed and its
     * message equals "winner".
     */
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

    /**
     * @brief Verifies exception propagation for a void-task in a vector-based when_any.
     *
     * Confirms that when_any executed on a vector of void tasks forwards an exception
     * thrown by one of the tasks to the provided exception handler and that the
     * original exception message is preserved.
     *
     * @details The test asserts that the exception handler is invoked and that the
     * caught test_exception's what() equals "void vector error".
     */
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

    /**
     * @brief Verifies that all tasks in a vector are allowed to complete for cleanup after a winning task is selected.
     *
     * Runs a vector-based when_any where each task increments a shared counter on start and then returns.
     * Asserts that the when_any completion handler runs and that every task in the vector has executed
     * (completion counter equals the number of tasks).
     */
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
        std::vector<coro> work_queue;
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
                auto token = co_await get_stop_token();
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
            work_queue.erase(work_queue.begin());
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

    /**
     * @brief Verifies when_any over a vector of int tasks reports a valid winner and correct value mapping.
     *
     * Runs when_any on 20 tasks that return the integers 1 through 20 using a test_executor,
     * and asserts that the operation completes, the reported winner index is within range,
     * and the returned value equals `winner_index + 1`.
     */
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

    /**
     * @brief Verifies nested vector-based when_any correctly reports the winning task's result.
     *
     * Creates two inner tasks that use when_any on a vector of int-producing tasks, then runs
     * an outer when_any over those inner tasks and asserts the outer result completes and
     * yields either 10 or 20.
     */
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

    /**
     * @brief Verifies that a vector-based when_any used inside when_all produces valid winners and values.
     *
     * Launches two identical asynchronous "race" tasks (each performing a when_any on a vector of int tasks)
     * inside a when_all and asserts that each race completes with a winner whose value is either 1 or 2,
     * and that the outer when_all completion handler is invoked.
     */
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

    /**
     * @brief Tests mixing variadic and vector overloads of `when_any` to verify winner selection and result mapping.
     *
     * This test launches two coroutines: one that races two `task<int>` instances using the variadic
     * `when_any` (producing 1 or 2) and one that races a `std::vector<task<int>>` using the vector
     * `when_any` (producing 3 or 4). It then races those two coroutines with an outer `when_any`
     * and asserts that the reported winner index identifies which inner race completed and that the
     * returned integer matches the expected value set for that inner race. The test also verifies
     * the outer completion callback is invoked.
     */
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
            [&](when_any_result_type<int, int> r) {
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

    /**
     * @brief Executes the complete suite of vector-based when_any tests.
     *
     * Runs every test in when_any_vector_test in a fixed sequence, covering basic
     * functionality, exception handling, stop-token propagation, long-lived task
     * cancellation, large-vector behavior, nested combinators, and mixed variadic
     * + vector scenarios.
     */
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