//
// Copyright (c) 2026 Steve Gerbino
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Compiled fragments shown in pages/4.coroutines/4b.launching.adoc. Pages
// include the tagged regions; scaffolding stays outside the tags.

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

#include <boost/capy/task.hpp>
#include <boost/capy/ex/run.hpp>
#include <boost/capy/ex/run_async.hpp>
#include <boost/capy/ex/this_coro.hpp>
#include <boost/capy/ex/thread_pool.hpp>

#include <atomic>
#include <exception>
#include <iostream>
#include <stop_token>
#include <thread>

#include "test_suite.hpp"

namespace {

// The page teaches with unqualified names after its opening program
// shows `using namespace boost::capy;`.
using namespace boost::capy;

task<int> compute()
{
    co_return 42;
}

task<int> expensive_computation()
{
    co_return 6 * 7;
}

// tag::run_hop[]
task<int> compute_on_pool(thread_pool& pool)
{
    // This task runs on whatever executor we're already on

    // But this child task runs on the pool's executor:
    int result = co_await run(pool.get_executor())(expensive_computation());

    // After co_await, we're back on our original executor
    co_return result;
}
// end::run_hop[]

std::atomic<bool> cancellable_saw_token{false};

task<void> cancellable_task()
{
    auto token = co_await this_coro::stop_token;
    cancellable_saw_token = token.stop_possible();
}

std::atomic<int> shared_state{0};
std::atomic<std::thread::id> handler_thread_id{};

void update_shared_state(int value)
{
    shared_state = value;
    handler_thread_id = std::this_thread::get_id();
}

// Hosts for the parent() fragments: the shown code refers to a `pool`
// in scope, and the child records what its stop token observed.
struct inherit_fixture
{
    thread_pool pool{1};
    bool child_saw_stop = false;

    task<void> child_task()
    {
        auto token = co_await this_coro::stop_token;
        child_saw_stop = token.stop_requested();
    }

    // tag::inherit_token[]
    task<void> parent()
    {
        // Child automatically receives our stop token
        co_await run(pool.get_executor())(child_task());
    }
    // end::inherit_token[]
};

struct override_fixture
{
    thread_pool pool{1};
    bool child_saw_stop = true;

    task<void> child_task()
    {
        auto token = co_await this_coro::stop_token;
        child_saw_stop = token.stop_requested();
    }

    // tag::override_token[]
    task<void> parent()
    {
        std::stop_source local;
        // Child gets local's token, not our caller's
        co_await run(pool.get_executor(), local.get_token())(child_task());
    }
    // end::override_token[]
};

struct launching_test
{
    void
    testHandlerOverloads()
    {
        thread_pool pool(1);
        auto ex = pool.get_executor();
        // tag::handlers[]
        // Result handler only (an unhandled exception calls std::terminate)
        run_async(ex, [](int result) {
            std::cout << "Got: " << result << "\n";
        })(compute());

        // Separate handlers for result and exception
        run_async(ex,
            [](int result) { std::cout << "Result: " << result << "\n"; },
            [](std::exception_ptr ep) {
                try { std::rethrow_exception(ep); }
                catch (std::exception const& e) {
                    std::cout << "Error: " << e.what() << "\n";
                }
            }
        )(compute());
        // end::handlers[]
        pool.join();
    }

    void
    testRunHop()
    {
        thread_pool launch_pool(1);
        thread_pool work_pool(1);
        int result = 0;
        run_async(launch_pool.get_executor(), [&](int r) { result = r; })(
            compute_on_pool(work_pool));
        launch_pool.join();
        BOOST_TEST(result == 42);
        work_pool.join();
    }

    void
    testInjectToken()
    {
        thread_pool pool(1);
        auto ex = pool.get_executor();
        // tag::inject_token[]
        std::stop_source source;
        run_async(ex, source.get_token())(cancellable_task());

        // Later, to request cancellation:
        source.request_stop();
        // end::inject_token[]
        pool.join();
        BOOST_TEST(cancellable_saw_token);
    }

    void
    testInheritToken()
    {
        thread_pool launch_pool(1);
        inherit_fixture fx;
        // A pre-stopped source makes inheritance observable in the child
        std::stop_source source;
        source.request_stop();
        run_async(launch_pool.get_executor(), source.get_token())(
            fx.parent());
        launch_pool.join();
        BOOST_TEST(fx.child_saw_stop);
        fx.pool.join();
    }

    void
    testOverrideToken()
    {
        thread_pool launch_pool(1);
        override_fixture fx;
        // The caller's token is stopped, but the child sees the fresh
        // local token instead
        std::stop_source source;
        source.request_stop();
        run_async(launch_pool.get_executor(), source.get_token())(
            fx.parent());
        launch_pool.join();
        BOOST_TEST(!fx.child_saw_stop);
        fx.pool.join();
    }

    void
    testHandlerThreading()
    {
        thread_pool pool(4);
        // tag::handler_thread[]
        // If pool has 4 threads, the handler runs on one of those threads
        run_async(pool.get_executor(), [](int result) {
            // This runs on a pool thread, NOT the main thread
            update_shared_state(result);
        })(compute());
        // end::handler_thread[]
        pool.join();
        BOOST_TEST(shared_state == 42);
        BOOST_TEST(handler_thread_id.load() != std::this_thread::get_id());
    }

    void
    run()
    {
        testHandlerOverloads();
        testRunHop();
        testInjectToken();
        testInheritToken();
        testOverrideToken();
        testHandlerThreading();
    }
};

} // namespace

TEST_SUITE(launching_test, "boost.capy.doc.4b_launching");
