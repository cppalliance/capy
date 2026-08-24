//
// Copyright (c) 2026 Steve Gerbino
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Compiled fragments shown in pages/4.coroutines/4d.io-awaitable.adoc.

// Fragments deliberately leave results and bindings unused; the pages
// explain the values in prose instead.
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wunused-value"
#pragma GCC diagnostic ignored "-Wunused-result"
#pragma GCC diagnostic ignored "-Wunused-function"
// gcc 15 with sanitizers misattributes coroutine frame delete paths
#pragma GCC diagnostic ignored "-Wmismatched-new-delete"
#endif
#if defined(__clang__)
#pragma clang diagnostic ignored "-Wunused-lambda-capture"
#pragma clang diagnostic ignored "-Wunused-private-field"
#endif
#if defined(_MSC_VER)
#pragma warning(disable: 4834) // discarding [[nodiscard]] return value
#pragma warning(disable: 4189) // local variable initialized but not referenced
#pragma warning(disable: 4100) // unreferenced formal parameter
#pragma warning(disable: 4101) // unreferenced local variable
#pragma warning(disable: 4456) // declaration hides previous local declaration
#pragma warning(disable: 4457) // declaration hides function parameter
#pragma warning(disable: 4458) // declaration hides class member
#pragma warning(disable: 4459) // declaration hides global declaration
#endif

#include <boost/capy/concept/io_awaitable.hpp>
#include <boost/capy/continuation.hpp>
#include <boost/capy/ex/io_env.hpp>
#include <boost/capy/ex/run_async.hpp>
#include <boost/capy/ex/thread_pool.hpp>
#include <boost/capy/task.hpp>

#include <coroutine>
#include <functional>
#include <optional>
#include <stop_token>
#include <thread>
#include <utility>

#include "test_suite.hpp"

namespace capy = boost::capy;

namespace {

using namespace boost::capy;

// The page shows the three standard await_suspend forms side by
// side; a single type cannot declare all three, so each lives in
// its own struct contributing a region of the same tag.
struct std_awaiter_void
{
    // tag::std_await_suspend[]
    void await_suspend(std::coroutine_handle<> h);
    // end::std_await_suspend[]
};

struct std_awaiter_bool
{
    // tag::std_await_suspend[]
    // or
    bool await_suspend(std::coroutine_handle<> h);
    // end::std_await_suspend[]
};

struct std_awaiter_handle
{
    // tag::std_await_suspend[]
    // or
    std::coroutine_handle<> await_suspend(std::coroutine_handle<> h);
    // end::std_await_suspend[]
};

struct io_awaiter_signature
{
    // tag::two_arg_await_suspend[]
    auto await_suspend(std::coroutine_handle<> h, io_env const* env);
    // end::two_arg_await_suspend[]
};

// The real definition lives in <boost/capy/concept/io_awaitable.hpp>;
// the sketch is checked below against types that satisfy (and fail)
// the real concept.
namespace concept_sketch {

// tag::io_awaitable_concept[]
template<typename A>
concept IoAwaitable =
    requires(A a, std::coroutine_handle<> h, io_env const* env) {
        a.await_suspend(h, env);
    };
// end::io_awaitable_concept[]

} // namespace concept_sketch

static_assert(concept_sketch::IoAwaitable<task<int>>);
static_assert(capy::IoAwaitable<task<int>>);
static_assert(!concept_sketch::IoAwaitable<std_awaiter_void>);
static_assert(!capy::IoAwaitable<std_awaiter_void>);

struct caller_promise
{
    io_env const* env_ = nullptr;

    io_env const* environment() const noexcept { return env_; }
};

template<class Awaitable>
struct transform_awaiter_sketch
{
    Awaitable awaitable_;
    caller_promise& promise_;

    // tag::context_flow[]
    template<class Promise>
    auto await_suspend(std::coroutine_handle<Promise> h)
    {
        // Forward caller's context to child
        return awaitable_.await_suspend(h, promise_.environment());
    }
    // end::context_flow[]
};

struct context_flow_child
{
    std::coroutine_handle<> await_suspend(
        std::coroutine_handle<>, io_env const*)
    {
        return std::noop_coroutine();
    }
};

// Instantiates the sketch; never called.
[[maybe_unused]] std::coroutine_handle<> instantiate_context_flow(
    transform_awaiter_sketch<context_flow_child>& ta,
    std::coroutine_handle<caller_promise> h)
{
    return ta.await_suspend(h);
}

using result_type = int;

// Stand-ins for the async machinery the page leaves unspecified.
void start_operation() {}
void start_async_operation() {}
void store_result() {}

// tag::my_awaitable[]
struct my_awaitable
{
    io_env const* env_ = nullptr;
    continuation cont_;
    result_type result_;

    bool await_ready() const noexcept
    {
        return false;  // Or true if result is immediately available
    }

    std::coroutine_handle<> await_suspend(std::coroutine_handle<> h, io_env const* env)
    {
        // Store pointer to environment, never copy
        env_ = env;
        // Wrap the caller's handle in a continuation we own, so it stays
        // at a stable address until the executor resumes it.
        cont_.h = h;

        // Start async operation...
        start_operation();

        // Return noop to suspend
        return std::noop_coroutine();
    }

    result_type await_resume()
    {
        return result_;
    }

private:
    void on_completion()
    {
        // Resume the caller on its executor. post() takes the
        // continuation by reference and queues it; never resume inline
        // from a completion callback (it may run on the wrong thread).
        env_->executor.post(cont_);
    }
};
// end::my_awaitable[]

// tag::runnable_awaitable[]
// A complete IoAwaitable following the pattern above. It produces a
// value, then resumes the caller on the caller's own executor by posting
// its continuation. A real one would do this from an async completion
// callback; here the "operation" finishes immediately.
struct add_awaitable
{
    int a_;
    int b_;
    io_env const* env_ = nullptr;
    // Defaulted, so the call site supplies only a_ and b_.
    continuation cont_{};
    result_type result_{};

    bool await_ready() const noexcept { return false; }

    std::coroutine_handle<> await_suspend(std::coroutine_handle<> h, io_env const* env)
    {
        env_ = env;
        cont_.h = h;
        result_ = a_ + b_;             // the operation produced a value
        env_->executor.post(cont_);    // resume the caller on its executor
        return std::noop_coroutine();
    }

    result_type await_resume() { return result_; }
};

// A task awaits the custom IoAwaitable and returns what it delivered.
task<int> add_via_awaitable()
{
    co_return co_await add_awaitable{2, 3};
}
// end::runnable_awaitable[]

// tag::stoppable_awaitable[]
struct stoppable_awaitable
{
    mutable continuation cont_;
    std::optional<std::stop_callback<std::function<void()>>> stop_cb_;

    bool await_ready() { return false; }

    std::coroutine_handle<> await_suspend(
        std::coroutine_handle<> h, io_env const* env)
    {
        if (env->stop_token.stop_requested())
            return h;  // Already cancelled, resume immediately

        // Post through executor when stop is requested
        cont_.h = h;
        auto ex = env->executor;
        stop_cb_.emplace(env->stop_token,
            [this, ex]() mutable noexcept { ex.post(cont_); });

        start_async_operation();
        return std::noop_coroutine();
    }

    void await_resume() { /* ... */ }
};
// end::stoppable_awaitable[]

// Compiles on purpose: the page shows this to warn that the broken
// pattern is not caught by the compiler.
struct wrong_stop_callback_demo
{
    std::optional<std::stop_callback<std::function<void()>>> stop_cb_;

    void emplace_wrong(std::coroutine_handle<> h, io_env const* env)
    {
        // tag::wrong_stop_callback[]
        // WRONG: resumes coroutine on the calling thread
        stop_cb_.emplace(env->stop_token, h);  // h is a raw coroutine_handle
        // end::wrong_stop_callback[]
    }
};

// Mirrors the shape of task.hpp's transform_awaitable; the shown
// static_assert sits in the branch taken for non-IoAwaitables.
template<class A>
void await_transform_sketch()
{
    if constexpr (capy::IoAwaitable<A>)
    {
    }
    else
    {
        // tag::reject_plain[]
        // In task.hpp, when the awaited type is not an IoAwaitable:
        static_assert(sizeof(A) == 0, "requires IoAwaitable");
        // end::reject_plain[]
    }
}

template void await_transform_sketch<task<int>>();

// The "foreign runtime" is played by a plain thread that invokes the
// completion callback, exactly like a callback-based C library would.
std::thread foreign_thread;

void start_foreign_op(std::function<void()> on_complete)
{
    foreign_thread = std::thread(std::move(on_complete));
}

// tag::foreign_bridge[]
// Bridge a foreign async operation into a Capy coroutine.
struct foreign_bridge
{
    io_env const* env_ = nullptr;
    continuation cont_;
    result_type result_;

    bool await_ready() const noexcept { return false; }

    std::coroutine_handle<> await_suspend(
        std::coroutine_handle<> h, io_env const* env)
    {
        env_ = env;
        cont_.h = h;   // stable address; the executor links continuations intrusively

        // Start the foreign operation. Its completion callback may run on
        // ANY thread, so it must not resume h directly. Instead it posts
        // the continuation back through the caller's executor, restoring
        // the same-executor invariant.
        start_foreign_op([this]() noexcept {
            store_result();
            env_->executor.post(cont_);
        });

        return std::noop_coroutine();
    }

    result_type await_resume() { return std::move(result_); }
};
// end::foreign_bridge[]

task<int> use_bridge()
{
    co_return co_await foreign_bridge{};
}

struct io_awaitable_test
{
    void testForeignBridge()
    {
        thread_pool pool(1);
        bool done = false;
        run_async(pool.get_executor(), [&done](int) {
            done = true;
        })(use_bridge());
        pool.join();
        if (foreign_thread.joinable())
            foreign_thread.join();
        BOOST_TEST(done);
    }

    void testRunning()
    {
        // tag::run_awaitable[]
        // Run the task on a thread pool and observe the value the
        // custom IoAwaitable delivered to the completion handler.
        thread_pool pool(1);
        int result = 0;
        run_async(pool.get_executor(), [&result](int value) {
            result = value;   // the awaitable delivered 5
        })(add_via_awaitable());
        pool.join();
        // end::run_awaitable[]
        BOOST_TEST(result == 5);
    }

    void run()
    {
        testRunning();
        testForeignBridge();
    }
};

} // namespace

TEST_SUITE(io_awaitable_test, "boost.capy.doc.4d_io_awaitable");
