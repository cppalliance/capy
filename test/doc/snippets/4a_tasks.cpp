//
// Copyright (c) 2026 Steve Gerbino
// Copyright (c) 2026 Michael Vandeberg
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Compiled fragments shown in pages/4.coroutines/4a.tasks.adoc. Pages
// include the tagged regions; scaffolding stays outside the tags.

#include "../doc_warnings.hpp"

// tag::include_task[]
#include <boost/capy/task.hpp>
// end::include_task[]

#include <boost/capy/ex/run_async.hpp>
#include <boost/capy/ex/thread_pool.hpp>

#include <coroutine>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>

#include "test_suite.hpp"

// The page's first fragment introduces the umbrella include and the
// namespace alias that later fragments rely on for capy:: qualification.
// tag::declaring[]
// tag::include_umbrella[]
#include <boost/capy.hpp>
// end::include_umbrella[]
namespace capy = boost::capy;
// end::declaring[]

namespace {

namespace declaring {

// tag::declaring[]

capy::task<int> compute_value()
{
    co_return 42;
}

capy::task<std::string> fetch_greeting()
{
    co_return "Hello, Capy!";
}

capy::task<> do_nothing()  // task<void>
{
    co_return;
}
// end::declaring[]

} // namespace declaring

namespace returning {

// tag::returning[]
capy::task<int> add(int a, int b)
{
    int result = a + b;
    co_return result;  // Completes with value
}

capy::task<> log_message(std::string msg)
{
    std::cout << msg << "\n";
    co_return;  // Completes without value
}
// end::returning[]

} // namespace returning

namespace io_results {

// tag::io_task[]
// io_result<Ts...> holds an error code `ec` plus zero or more payload
// values. io_task<Ts...> is just an alias for task<io_result<Ts...>>.

capy::io_task<> ensure_ready(bool ready)
{
    if(! ready)
        co_return std::make_error_code(std::errc::not_connected);  // ec converts
    co_return {};                                             // success
}

capy::io_task<std::size_t> count_ready(bool ready)
{
    using result = capy::io_result<std::size_t>;
    if(! ready)
        co_return result{std::make_error_code(std::errc::not_connected), 0};
    co_return result{std::error_code(), 42};  // success, carrying a value
}

capy::task<> use_them()
{
    // io_result models the tuple protocol: ec first, then the payloads.
    auto [ec, n] = co_await count_ready(true);
    if(ec)
        co_return;  // always check ec first
    (void)n;        // n is only meaningful when ec is falsy
}
// end::io_task[]

} // namespace io_results

namespace awaiting {

// tag::awaiting[]
capy::task<int> step_one()
{
    co_return 10;
}

capy::task<int> step_two(int x)
{
    co_return x * 2;
}

capy::task<int> full_operation()
{
    int a = co_await step_one();  // Suspends until step_one completes
    int b = co_await step_two(a); // Suspends until step_two completes
    co_return b + 5;              // Final result: 25
}
// end::awaiting[]

} // namespace awaiting

namespace lazy {

// tag::lazy[]
capy::task<int> compute()
{
    std::cout << "Computing...\n";  // Not printed until awaited
    co_return 42;
}

capy::task<> example()
{
    auto t = compute();   // Task created, but "Computing..." NOT printed yet
    std::cout << "Task created\n";

    int result = co_await std::move(t);  // NOW "Computing..." is printed
    std::cout << "Result: " << result << "\n";
}
// end::lazy[]

} // namespace lazy

namespace symmetric {

capy::task<> b();
capy::task<> c();

// tag::chain[]
capy::task<> a() { co_await b(); }
capy::task<> b() { co_await c(); }
capy::task<> c() { co_return; }
// end::chain[]

} // namespace symmetric

// Host for the final_suspend awaiter excerpt; compiling is the test.
struct final_suspend_sketch
{
    std::coroutine_handle<> continuation_;

    // tag::final_suspend[]
    // Inside task's final_suspend awaiter
    std::coroutine_handle<> await_suspend(std::coroutine_handle<>) const noexcept
    {
        return continuation_;  // Transfer directly to continuation
    }
    // end::final_suspend[]
};

namespace moving {

// tag::move_only[]
capy::task<int> compute();

capy::task<> example()
{
    auto t1 = compute();
    auto t2 = std::move(t1);  // OK: ownership transferred, t1 is now empty

    // auto t3 = t2;  // Error: task is not copyable

    int result = co_await std::move(t2);
}
// end::move_only[]

capy::task<int> compute()
{
    co_return 42;
}

} // namespace moving

namespace exceptions {

// tag::exceptions[]
capy::task<int> might_fail(bool should_fail)
{
    if (should_fail)
        throw std::runtime_error("Operation failed");
    co_return 42;
}

capy::task<> example()
{
    try
    {
        int result = co_await might_fail(true);
    }
    catch (std::runtime_error const& e)
    {
        std::cout << "Caught: " << e.what() << "\n";
    }
}
// end::exceptions[]

} // namespace exceptions

struct tasks_test
{
    void
    testDeclaring()
    {
        capy::thread_pool pool(1);
        int value = 0;
        std::string greeting;
        capy::run_async(pool.get_executor(), [&](int v) { value = v; })(
            declaring::compute_value());
        capy::run_async(
            pool.get_executor(), [&](std::string s) { greeting = s; })(
            declaring::fetch_greeting());
        capy::run_async(pool.get_executor())(declaring::do_nothing());
        pool.join();
        BOOST_TEST(value == 42);
        BOOST_TEST(greeting == "Hello, Capy!");
    }

    void
    testReturning()
    {
        capy::thread_pool pool(1);
        int sum = 0;
        capy::run_async(pool.get_executor(), [&](int r) { sum = r; })(
            returning::add(2, 3));
        capy::run_async(pool.get_executor())(
            returning::log_message("logged"));
        pool.join();
        BOOST_TEST(sum == 5);
    }

    void
    testRunning()
    {
        using returning::add;
        // tag::run[]
        // You have a task; run it on an executor and observe its result.
        capy::thread_pool pool(1);
        auto ex = pool.get_executor();

        int total = 0;
        capy::run_async(ex, [&](int result) {
            std::cout << "Result: " << result << "\n";  // prints 5
            total = result;
        })(add(2, 3));

        pool.join();  // wait for the pooled task to finish
        // end::run[]
        BOOST_TEST(total == 5);
    }

    void
    testAwaiting()
    {
        capy::thread_pool pool(1);
        int result = 0;
        capy::run_async(pool.get_executor(), [&](int r) { result = r; })(
            awaiting::full_operation());
        pool.join();
        BOOST_TEST(result == 25);
    }

    void
    testLazy()
    {
        capy::thread_pool pool(1);
        capy::run_async(pool.get_executor())(lazy::example());
        pool.join();
    }

    void
    testChain()
    {
        capy::thread_pool pool(1);
        capy::run_async(pool.get_executor())(symmetric::a());
        pool.join();
    }

    void
    testMoveOnly()
    {
        capy::thread_pool pool(1);
        capy::run_async(pool.get_executor())(moving::example());
        pool.join();
    }

    void
    testExceptions()
    {
        capy::thread_pool pool(1);
        capy::run_async(pool.get_executor())(exceptions::example());
        pool.join();
    }

    void
    run()
    {
        testDeclaring();
        testReturning();
        testRunning();
        testAwaiting();
        testLazy();
        testChain();
        testMoveOnly();
        testExceptions();
    }
};

} // namespace

TEST_SUITE(tasks_test, "boost.capy.doc.4a_tasks");
