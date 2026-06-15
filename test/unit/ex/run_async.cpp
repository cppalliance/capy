//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Test that header file is self-contained.
#include <boost/capy/ex/run_async.hpp>

#include <boost/capy/task.hpp>
#include <boost/capy/ex/execution_context.hpp>
#include <boost/capy/ex/this_coro.hpp>

#include "test_helpers.hpp"

#include <atomic>
#include <cstddef>
#include <cstdio>
#include <memory>
#include <queue>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#if !defined(_WIN32)
#include <unistd.h>
#include <sys/wait.h>
#endif

/*
    Implementation Notes for run_async
    ==================================

    run_async launches lazy task<T> coroutines for execution. It uses a
    trampoline coroutine allocated BEFORE the task (via C++17 postfix
    evaluation) and a type-erased invoke_fn to bridge handler invocation
    without knowing T at trampoline creation time.

    Design Constraints:

 1. Internal parent coroutine - Trampoline serves as the parent
 2. Creation order - Trampoline allocated before task (C++17 postfix)
 3. Control flow - Task final_suspends → trampoline regains control
 4. Handler invocation - Via type-erased invoke_fn
 5. Destruction order - LIFO: task destroyed first, trampoline last
 6. Dispatcher resumption - Task resumed via ex_(task_h)
 7. Allocator ignored - Stored but unused (placeholder for future)
 8. Stop token propagation - Passed to task's promise
*/

namespace boost {
namespace capy {

//----------------------------------------------------------
// Test Executors
//----------------------------------------------------------

/// Synchronous executor - executes inline.
struct sync_executor
{
    int* dispatch_count_ = nullptr;
    test_io_context* ctx_ = nullptr;
    static test_io_context default_ctx_;

    sync_executor() = default;

    explicit sync_executor(int& count)
        : dispatch_count_(&count)
    {
    }

    bool operator==(sync_executor const& other) const noexcept
    {
        return dispatch_count_ == other.dispatch_count_;
    }

    execution_context& context() const noexcept
    {
        return ctx_ ? *ctx_ : default_ctx_;
    }

    void on_work_started() const noexcept {}
    void on_work_finished() const noexcept {}

    std::coroutine_handle<> dispatch(continuation& c) const
    {
        if(dispatch_count_)
            ++(*dispatch_count_);
        return c.h;
    }

    void post(continuation& c) const
    {
        c.h.resume();
    }
};

test_io_context sync_executor::default_ctx_;

static_assert(Executor<sync_executor>);

/// Queuing executor - queues for manual execution.
struct queue_executor
{
    std::queue<std::coroutine_handle<>>* queue_;
    test_io_context* ctx_ = nullptr;
    static test_io_context default_ctx_;

    explicit queue_executor(std::queue<std::coroutine_handle<>>& q)
        : queue_(&q)
    {
    }

    bool operator==(queue_executor const& other) const noexcept
    {
        return queue_ == other.queue_;
    }

    execution_context& context() const noexcept
    {
        return ctx_ ? *ctx_ : default_ctx_;
    }

    void on_work_started() const noexcept {}
    void on_work_finished() const noexcept {}

    std::coroutine_handle<> dispatch(continuation& c) const
    {
        queue_->push(c.h);
        return std::noop_coroutine();
    }

    void post(continuation& c) const
    {
        queue_->push(c.h);
    }
};

test_io_context queue_executor::default_ctx_;

static_assert(Executor<queue_executor>);

} // namespace capy
} // namespace boost

#include "test/unit/custom_task.hpp"

namespace boost {
namespace capy {

using test::custom_task;

//----------------------------------------------------------
// run_async Tests
//----------------------------------------------------------

struct run_async_test
{
    //----------------------------------------------------------
    // Basic Functionality
    //----------------------------------------------------------

    static task<int>
    returns_int()
    {
        co_return 42;
    }

    static task<void>
    returns_void()
    {
        co_return;
    }

    static task<std::string>
    returns_string()
    {
        co_return "hello";
    }

    void
    testNoHandlers()
    {
        // Fire and forget - result discarded
        int dispatch_count = 0;
        sync_executor d(dispatch_count);
        
        run_async(d)(returns_int());
        BOOST_TEST_EQ(dispatch_count, 1);
    }

    void
    testResultHandler()
    {
        int dispatch_count = 0;
        sync_executor d(dispatch_count);
        int result = 0;

        run_async(d, [&](int v) { result = v; })(returns_int());

        BOOST_TEST_EQ(result, 42);
        BOOST_TEST_EQ(dispatch_count, 1);
    }

    void
    testValueAllocator()
    {
        // The value-type allocator overload wraps the allocator in a
        // frame_memory_resource and uses the allocating trampoline.
        int dispatch_count = 0;
        sync_executor d(dispatch_count);
        int result = 0;

        run_async(d, std::allocator<std::byte>{},
            [&](int v) { result = v; })(returns_int());

        BOOST_TEST_EQ(result, 42);
        BOOST_TEST_EQ(dispatch_count, 1);
    }

    void
    testValueAllocatorException()
    {
        // Value-allocator trampoline routing a task exception to the
        // error handler.
        int dispatch_count = 0;
        sync_executor d(dispatch_count);
        bool error_called = false;

        run_async(d, std::allocator<std::byte>{},
            [&](int) { },
            [&](std::exception_ptr ep) {
                error_called = true;
                BOOST_TEST(ep != nullptr);
            })(throws_exception());

        BOOST_TEST(error_called);
    }

    void
    testVoidTaskResultHandler()
    {
        int dispatch_count = 0;
        sync_executor d(dispatch_count);
        bool called = false;

        run_async(d, [&]() { called = true; })(returns_void());

        BOOST_TEST(called);
        BOOST_TEST_EQ(dispatch_count, 1);
    }

    void
    testDualHandlers()
    {
        int dispatch_count = 0;
        sync_executor d(dispatch_count);
        int result = 0;
        bool error_called = false;

        run_async(d,
            [&](int v) { result = v; },
            [&](std::exception_ptr) { error_called = true; }
        )(returns_int());

        BOOST_TEST_EQ(result, 42);
        BOOST_TEST(!error_called);
    }

    void
    testOverloadedHandler()
    {
        int dispatch_count = 0;
        sync_executor d(dispatch_count);
        int result = 0;
        bool exception_handled = false;

        // Handler that can accept both int and exception_ptr
        auto handler = [&](auto v) {
            if constexpr(std::is_same_v<decltype(v), int>)
                result = v;
            else if constexpr(std::is_same_v<decltype(v), std::exception_ptr>)
                exception_handled = true;
        };

        run_async(d, handler)(returns_int());

        BOOST_TEST_EQ(result, 42);
        BOOST_TEST(!exception_handled);
    }

    //----------------------------------------------------------
    // Exception Handling
    //----------------------------------------------------------

    static task<int>
    throws_exception()
    {
        throw test_exception("test error");
        co_return 0;
    }

    static task<void>
    void_throws_exception()
    {
        throw test_exception("void task error");
        co_return;
    }

    // Note: a task that throws with no error handler calls std::terminate
    // (the trampoline's unhandled_exception). That path is fatal and not
    // unit-testable here; pass an error handler to observe exceptions.

#if !defined(_WIN32)
    // Death test: an exception escaping a handler must call std::terminate.
    // Run each scenario in a forked child (the child aborts); the parent
    // verifies the child did not exit normally. POSIX-only.
    void
    testTerminateOnUnhandled()
    {
        auto terminates = [](auto fn) {
            ::pid_t pid = ::fork();
            BOOST_TEST(pid >= 0);
            if(pid == 0)
            {
                // Hush the abort message; the binding satisfies freopen's
                // warn_unused_result.
                [[maybe_unused]] auto* f =
                    std::freopen("/dev/null", "w", stderr);
                fn();
                _exit(0); // reached only if no terminate happened
            }
            int status = 0;
            ::waitpid(pid, &status, 0);
            return !(WIFEXITED(status) && WEXITSTATUS(status) == 0);
        };

        // No handler + throwing task: default handler rethrows -> terminate.
        BOOST_TEST(terminates([] {
            int dc = 0;
            sync_executor d(dc);
            run_async(d)(throws_exception());
        }));

        // Error handler rethrows -> escapes the handler -> terminate.
        BOOST_TEST(terminates([] {
            int dc = 0;
            sync_executor d(dc);
            run_async(d,
                [](int) {},
                [](std::exception_ptr ep) { std::rethrow_exception(ep); }
            )(throws_exception());
        }));
    }
#endif

    void
    testErrorHandlerReceivesException()
    {
        int dispatch_count = 0;
        sync_executor d(dispatch_count);
        bool success_called = false;
        bool error_called = false;

        run_async(d,
            [&](int) { success_called = true; },
            [&](std::exception_ptr ep) {
                error_called = true;
                BOOST_TEST(ep != nullptr);
            }
        )(throws_exception());

        BOOST_TEST(!success_called);
        BOOST_TEST(error_called);
    }

    void
    testOverloadedHandlerException()
    {
        int dispatch_count = 0;
        sync_executor d(dispatch_count);
        bool got_value = false;
        bool got_exception = false;

        // Overloaded handler
        struct {
            bool* got_value_;
            bool* got_exception_;
            void operator()(int) { *got_value_ = true; }
            void operator()(std::exception_ptr) { *got_exception_ = true; }
        } handler{&got_value, &got_exception};

        run_async(d, handler)(throws_exception());

        BOOST_TEST(!got_value);
        BOOST_TEST(got_exception);
    }

    //----------------------------------------------------------
    // Stop Token
    //----------------------------------------------------------

    static task<bool>
    check_stop_requested()
    {
        auto token = co_await this_coro::stop_token;
        co_return token.stop_requested();
    }

    void
    testStopTokenPropagation()
    {
        int dispatch_count = 0;
        sync_executor d(dispatch_count);
        bool result = true;

        std::stop_source source;
        // Don't request stop - token should not be in stopped state
        
        run_async(d, source.get_token(), [&](bool v) { result = v; })(
            check_stop_requested());

        BOOST_TEST(!result);
    }

    void
    testCancellationVisible()
    {
        int dispatch_count = 0;
        sync_executor d(dispatch_count);
        bool result = false;

        std::stop_source source;
        source.request_stop();
        
        run_async(d, source.get_token(), [&](bool v) { result = v; })(
            check_stop_requested());

        BOOST_TEST(result);
    }

    void
    testScopedCancellation()
    {
        // Three tasks on the same executor: one with a scoped stop token,
        // two with the default (empty) token. Cancelling the scoped token
        // should only affect that task, not the others.
        std::queue<std::coroutine_handle<>> queue;
        queue_executor d(queue);

        bool default_1_stopped = true;
        bool scoped_stopped = false;
        bool default_2_stopped = true;

        std::stop_source scoped_source;

        run_async(d, [&](bool v) { default_1_stopped = v; })(
            check_stop_requested());
        run_async(d, scoped_source.get_token(),
            [&](bool v) { scoped_stopped = v; })(
            check_stop_requested());
        run_async(d, [&](bool v) { default_2_stopped = v; })(
            check_stop_requested());

        BOOST_TEST_EQ(queue.size(), 3u);

        // Cancel the scoped source before draining
        scoped_source.request_stop();

        while(!queue.empty())
        {
            auto h = queue.front();
            queue.pop();
            h.resume();
        }

        BOOST_TEST(!default_1_stopped);
        BOOST_TEST(scoped_stopped);
        BOOST_TEST(!default_2_stopped);
    }

    //----------------------------------------------------------
    // Allocator Propagation
    //----------------------------------------------------------

    static task<bool>
    check_allocator_propagated()
    {
        auto* alloc = co_await this_coro::frame_allocator;
        co_return alloc != nullptr;
    }

    void
    testAllocatorPropagation()
    {
        int dispatch_count = 0;
        sync_executor d(dispatch_count);
        bool result = false;

        run_async(d, [&](bool v) { result = v; })(
            check_allocator_propagated());

        BOOST_TEST(result);
    }

    //----------------------------------------------------------
    // Sync Dispatcher
    //----------------------------------------------------------

    void
    testSyncDispatcherBasic()
    {
        int dispatch_count = 0;
        sync_executor d(dispatch_count);
        int result = 0;

        run_async(d, [&](int v) { result = v; })(returns_int());

        BOOST_TEST_EQ(result, 42);
        BOOST_TEST_EQ(dispatch_count, 1);
    }

    static task<int>
    nested_task()
    {
        int v = co_await returns_int();
        co_return v + 1;
    }

    void
    testSyncDispatcherNested()
    {
        int dispatch_count = 0;
        sync_executor d(dispatch_count);
        int result = 0;

        run_async(d, [&](int v) { result = v; })(nested_task());

        BOOST_TEST_EQ(result, 43);
    }

    void
    testSyncDispatcherException()
    {
        int dispatch_count = 0;
        sync_executor d(dispatch_count);
        bool error_called = false;

        run_async(d,
            [](int) {},
            [&](std::exception_ptr) { error_called = true; }
        )(throws_exception());

        BOOST_TEST(error_called);
    }

    //----------------------------------------------------------
    // Async Dispatcher (using queue)
    //----------------------------------------------------------

    void
    testAsyncDispatcherBasic()
    {
        std::queue<std::coroutine_handle<>> queue;
        queue_executor d(queue);
        int result = 0;

        run_async(d, [&](int v) { result = v; })(returns_int());

        // Not called yet
        BOOST_TEST_EQ(result, 0);
        BOOST_TEST_EQ(queue.size(), 1u);

        // Execute the queued work
        while(!queue.empty())
        {
            auto h = queue.front();
            queue.pop();
            h.resume();
        }

        BOOST_TEST_EQ(result, 42);
    }

    void
    testAsyncDispatcherMultiple()
    {
        std::queue<std::coroutine_handle<>> queue;
        queue_executor d(queue);
        int sum = 0;

        run_async(d, [&](int v) { sum += v; })(returns_int());
        run_async(d, [&](int v) { sum += v; })(returns_int());
        run_async(d, [&](int v) { sum += v; })(returns_int());

        BOOST_TEST_EQ(sum, 0);
        BOOST_TEST_EQ(queue.size(), 3u);

        // Execute all
        while(!queue.empty())
        {
            auto h = queue.front();
            queue.pop();
            h.resume();
        }

        BOOST_TEST_EQ(sum, 126);
    }

    //----------------------------------------------------------
    // Handler Types
    //----------------------------------------------------------

    static void
    free_function_handler(int v)
    {
        (void)v;
    }

    void
    testLambdaHandlers()
    {
        int dispatch_count = 0;
        sync_executor d(dispatch_count);
        int result = 0;

        auto lambda = [&result](int v) { result = v; };
        run_async(d, lambda)(returns_int());

        BOOST_TEST_EQ(result, 42);
    }

    void
    testGenericLambda()
    {
        int dispatch_count = 0;
        sync_executor d(dispatch_count);
        int result = 0;

        run_async(d, [&result](auto v) {
            if constexpr(std::is_same_v<decltype(v), int>)
                result = v;
        })(returns_int());

        BOOST_TEST_EQ(result, 42);
    }

    void
    testStatefulHandlers()
    {
        int dispatch_count = 0;
        sync_executor d(dispatch_count);
        
        struct counter
        {
            int count = 0;
            void operator()(int v) { count += v; }
        };

        counter c;
        run_async(d, std::ref(c))(returns_int());

        BOOST_TEST_EQ(c.count, 42);
    }

    //----------------------------------------------------------
    // Edge Cases
    //----------------------------------------------------------

    static task<int>
    immediate_return()
    {
        co_return 99;
    }

    void
    testImmediateCompletion()
    {
        int dispatch_count = 0;
        sync_executor d(dispatch_count);
        int result = 0;

        run_async(d, [&](int v) { result = v; })(immediate_return());

        BOOST_TEST_EQ(result, 99);
    }

    void
    testEmptyStopToken()
    {
        int dispatch_count = 0;
        sync_executor d(dispatch_count);
        int result = 0;

        // Default-constructed stop_token
        run_async(d, std::stop_token{}, [&](int v) { result = v; })(
            returns_int());

        BOOST_TEST_EQ(result, 42);
    }

    //----------------------------------------------------------
    // Custom Task Type (proves IoRunnable genericity)
    //----------------------------------------------------------

    static custom_task<int>
    custom_returns_int()
    {
        co_return 123;
    }

    static custom_task<void>
    custom_returns_void()
    {
        co_return;
    }

    void
    testCustomTaskType()
    {
        // Proves run_async works with any IoRunnable, not just capy::task
        int dispatch_count = 0;
        sync_executor d(dispatch_count);
        int result = 0;

        run_async(d, [&](int v) { result = v; })(custom_returns_int());

        BOOST_TEST_EQ(result, 123);
        BOOST_TEST_EQ(dispatch_count, 1);
    }

    void
    testCustomTaskTypeVoid()
    {
        // Proves run_async works with void custom tasks
        int dispatch_count = 0;
        sync_executor d(dispatch_count);
        bool called = false;

        run_async(d, [&]() { called = true; })(custom_returns_void());

        BOOST_TEST(called);
        BOOST_TEST_EQ(dispatch_count, 1);
    }

    //----------------------------------------------------------

    void
    run()
    {
        // Basic Functionality
        testNoHandlers();
        testValueAllocator();
        testValueAllocatorException();
        testResultHandler();
        testVoidTaskResultHandler();
        testDualHandlers();
        testOverloadedHandler();

        // Exception Handling
#if !defined(_WIN32)
        testTerminateOnUnhandled();
#endif
        testErrorHandlerReceivesException();
        testOverloadedHandlerException();

        // Stop Token
        testStopTokenPropagation();
        testCancellationVisible();
        testScopedCancellation();

        // Allocator Propagation
        testAllocatorPropagation();

        // Sync Dispatcher
        testSyncDispatcherBasic();
        testSyncDispatcherNested();
        testSyncDispatcherException();

        // Async Dispatcher
        testAsyncDispatcherBasic();
        testAsyncDispatcherMultiple();

        // Handler Types
        testLambdaHandlers();
        testGenericLambda();
        testStatefulHandlers();

        // Edge Cases
        testImmediateCompletion();
        testEmptyStopToken();

        // Custom Task Type
        testCustomTaskType();
        testCustomTaskTypeVoid();
    }
};

TEST_SUITE(
    run_async_test,
    "boost.capy.ex.run_async");

} // namespace capy
} // namespace boost
