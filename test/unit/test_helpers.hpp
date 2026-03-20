//
// Copyright (c) 2021 Vinnie Falco (vinnie.falco@gmail.com)
// Copyright (c) 2026 Michael Vandeberg
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_TEST_HELPERS_HPP
#define BOOST_CAPY_TEST_HELPERS_HPP

#include <boost/capy/concept/execution_context.hpp>
#include <boost/capy/concept/executor.hpp>
#include <coroutine>
#include <boost/capy/ex/execution_context.hpp>
#include <boost/capy/ex/io_env.hpp>
#include <boost/capy/task.hpp>

#include <atomic>
#include <stop_token>

#include "test_suite.hpp"

#include <chrono>
#include <cstring>
#include <queue>
#include <stdexcept>
#include <string>
#include <thread>

// Valgrind slows execution ~10-20x; scale timing-sensitive
// durations to avoid false failures.
#ifdef BOOST_NO_STRESS_TEST
inline constexpr int failsafe_scale = 20;
#else
inline constexpr int failsafe_scale = 1;
#endif

#if defined(__linux__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__APPLE__)
#include <pthread.h>
#define BOOST_CAPY_TEST_CAN_GET_THREAD_NAME 1
#elif defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#define BOOST_CAPY_TEST_CAN_GET_THREAD_NAME 1
#endif

namespace boost {
namespace capy {

//----------------------------------------------------------
// Test Utilities
//----------------------------------------------------------

class test_io_context;

inline test_io_context&
default_test_io_context() noexcept;

/** Simple synchronous executor for testing.

    Satisfies the Executor concept.
    Executes inline (returns the handle for symmetric transfer).
    Uses pointers to external storage to allow copying.
*/
struct test_executor
{
    int id_ = 0;
    int* dispatch_count_ = nullptr;
    test_io_context* ctx_ = nullptr;

    test_executor() = default;

    explicit
    test_executor(test_io_context& ctx) noexcept
        : ctx_(&ctx)
    {
    }

    explicit
    test_executor(int& count) noexcept
        : dispatch_count_(&count)
    {
    }

    test_executor(int id, int& count) noexcept
        : id_(id)
        , dispatch_count_(&count)
    {
    }

    bool
    operator==(test_executor const& other) const noexcept
    {
        return id_ == other.id_ &&
               dispatch_count_ == other.dispatch_count_ &&
               ctx_ == other.ctx_;
    }

    test_io_context&
    context() const noexcept;

    void on_work_started() const noexcept {}
    void on_work_finished() const noexcept {}

    std::coroutine_handle<>
    dispatch(continuation& c) const
    {
        if(dispatch_count_)
            ++(*dispatch_count_);
        return c.h;
    }

    void
    post(continuation& c) const
    {
        c.h.resume();
    }
};

/** Test execution context satisfying ExecutionContext.

    Provides a minimal execution context for testing that
    satisfies the ExecutionContext concept requirements.
*/
class test_io_context : public execution_context
{
public:
    using executor_type = test_executor;

    int id = 0;

    test_io_context()
        : execution_context(this)
    {
    }

    executor_type
    get_executor() noexcept
    {
        return test_executor(*this);
    }
};

inline test_io_context&
default_test_io_context() noexcept
{
    static test_io_context ctx;
    return ctx;
}

inline test_io_context&
test_executor::context() const noexcept
{
    return ctx_ ? *ctx_ : default_test_io_context();
}

static_assert(Executor<test_executor>);
static_assert(ExecutionContext<test_io_context>);


//----------------------------------------------------------
// Wait Utilities
//----------------------------------------------------------

/// Wait for a predicate to become true with timeout.
template<class Pred>
bool
wait_for(
    Pred pred,
    std::chrono::milliseconds timeout = std::chrono::milliseconds(5000))
{
    auto start = std::chrono::steady_clock::now();
    while(!pred())
    {
        if(std::chrono::steady_clock::now() - start > timeout)
            return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return true;
}

//----------------------------------------------------------
// Thread Name Utilities
//----------------------------------------------------------

#if defined(BOOST_CAPY_TEST_CAN_GET_THREAD_NAME)

/// Get current thread name into buffer, returns true on success.
inline bool
get_current_thread_name(char* buffer, std::size_t size)
{
#if defined(_WIN32)
    wchar_t* wname = nullptr;
    if(!SUCCEEDED(GetThreadDescription(GetCurrentThread(), &wname)))
        return false;
    int len = WideCharToMultiByte(
        CP_UTF8, 0, wname, -1, buffer,
        static_cast<int>(size), nullptr, nullptr);
    LocalFree(wname);
    return len > 0;
#else
    return pthread_getname_np(pthread_self(), buffer, size) == 0;
#endif
}

/// Check if current thread name matches expected value exactly.
inline bool
check_thread_name(char const* expected)
{
    char buffer[64] = {};
#if defined(_WIN32)
    wchar_t* wname = nullptr;
    if(!SUCCEEDED(GetThreadDescription(GetCurrentThread(), &wname)))
        return false;
    int len = WideCharToMultiByte(
        CP_UTF8, 0, wname, -1, buffer,
        static_cast<int>(sizeof(buffer)), nullptr, nullptr);
    LocalFree(wname);
    if(len <= 0)
        return false;
#else
    if(pthread_getname_np(pthread_self(), buffer, sizeof(buffer)) != 0)
        return false;
#endif
    return std::strcmp(buffer, expected) == 0;
}

/// Check if current thread name starts with given prefix.
inline bool
thread_name_starts_with(char const* name_prefix)
{
    char buffer[64] = {};
    if(!get_current_thread_name(buffer, sizeof(buffer)))
        return false;
    return std::strncmp(buffer, name_prefix, std::strlen(name_prefix)) == 0;
}

#endif // BOOST_CAPY_TEST_CAN_GET_THREAD_NAME

struct self_destroy_awaitable
{
    bool await_ready() {return false;}

    std::coroutine_handle<> await_suspend(std::coroutine_handle<> h, io_env const*)
    {
        // one wouldn't expect this to happen, but it should not cause UB
        h.destroy();
        return std::noop_coroutine();
    }
    void await_resume() {}
};


// test awaitable that must be stopped in order to resume.
// Uses resume_via_post to ensure the coroutine resumes on the
// executor's thread, not on whatever thread calls request_stop().
struct stop_only_awaitable
{
    stop_only_awaitable() noexcept = default;
    stop_only_awaitable(stop_only_awaitable && ) noexcept {}

    // Placement-new storage instead of std::optional to avoid a
    // data race on optional's _M_engaged flag.  The stop_callback
    // constructor synchronises with request_stop() through the
    // stop-state's atomics, but optional::emplace writes _M_engaged
    // *after* the constructor returns — outside that sync window.
    // When ~jthread calls request_stop() before join(), the
    // destructor's _M_reset (on the requesting thread) races with
    // emplace's _M_engaged write (on the registering thread).
    BOOST_CAPY_MSVC_WARNING_PUSH
    BOOST_CAPY_MSVC_WARNING_DISABLE(4324) // padded due to alignas
    alignas(stop_resume_callback)
        unsigned char stop_cb_buf_[sizeof(stop_resume_callback)]{};
    BOOST_CAPY_MSVC_WARNING_POP
    std::atomic<bool> active_{false};

    ~stop_only_awaitable()
    {
        if (active_.load(std::memory_order_acquire))
            reinterpret_cast<stop_resume_callback*>(
                stop_cb_buf_)->~stop_resume_callback();
    }

    bool await_ready() {return false;}

    std::coroutine_handle<> await_suspend(std::coroutine_handle<> h, io_env const* env)
    {
        if (env->stop_token.stop_requested())
            return h;
        ::new(stop_cb_buf_) stop_resume_callback(
            env->stop_token, env->post_resume(h));
        active_.store(true, std::memory_order_release);
        return std::noop_coroutine();
    }
    void await_resume() {}
};

//----------------------------------------------------------
// Test Exception Types
//----------------------------------------------------------

/** Standard test exception type used across test files. */
struct test_exception : std::runtime_error
{
    explicit test_exception(const char* msg)
        : std::runtime_error(msg)
    {
    }
};

/** Helper to throw test_exception with a message. */
[[noreturn]] inline void
throw_test_exception(char const* msg)
{
    throw test_exception(msg);
}

//----------------------------------------------------------
// Common Test Task Helpers
//----------------------------------------------------------

/** Returns a task that completes with an int value. */
inline task<int>
returns_int(int value)
{
    co_return value;
}

/** Returns a task that completes with a string value. */
inline task<std::string>
returns_string(std::string value)
{
    co_return value;
}

/** Returns a task that completes with void. */
inline task<void>
void_task()
{
    co_return;
}

/** Returns a task that throws an exception. */
inline task<int>
throws_exception(char const* msg)
{
    throw_test_exception(msg);
    co_return 0;
}

/** Returns a void task that throws an exception. */
inline task<void>
void_throws_exception(char const* msg)
{
    throw_test_exception(msg);
    co_return;
}

//----------------------------------------------------------
// Queuing Executor
//----------------------------------------------------------

/** Queuing executor that allows controlled interleaving of tasks.

    Unlike test_executor which runs tasks synchronously, this executor
    queues work and runs it in FIFO order when manually resumed.
    This allows tasks to observe stop requests between suspension points.
*/
struct queuing_executor
{
    std::queue<std::coroutine_handle<>>* queue_;
    test_io_context* ctx_ = nullptr;

    explicit queuing_executor(std::queue<std::coroutine_handle<>>& q)
        : queue_(&q)
    {
    }

    bool operator==(queuing_executor const& other) const noexcept
    {
        return queue_ == other.queue_;
    }

    test_io_context& context() const noexcept
    {
        return ctx_ ? *ctx_ : default_test_io_context();
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

static_assert(Executor<queuing_executor>);

//----------------------------------------------------------
// Yield Awaitable
//----------------------------------------------------------

/** Awaitable that yields to the executor, allowing other tasks to run.

    When awaited, this suspends the current coroutine and posts it back
    to the executor's queue. This creates a yield point where the task
    can be interleaved with other tasks.
*/
struct yield_awaitable
{
    continuation cont_;

    bool await_ready() const noexcept
    {
        return false;
    }

    std::coroutine_handle<> await_suspend(std::coroutine_handle<> h, io_env const* env)
    {
        // Post ourselves back to the queue
        cont_.h = h;
        env->executor.post(cont_);
        return std::noop_coroutine();
    }

    void await_resume() const noexcept
    {
    }
};

} // capy
} // boost

#endif
