//
// Copyright (c) 2026 Steve Gerbino
// Copyright (c) 2026 Michael Vandeberg
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Compiled fragments shown in pages/4.coroutines/4f.composition.adoc.

#include "../doc_warnings.hpp"

// tag::when_all_basic[]
#include <boost/capy/when_all.hpp>
// end::when_all_basic[]
// tag::when_any_basic[]
#include <boost/capy/when_any.hpp>
// end::when_any_basic[]

#include <boost/capy/cond.hpp>
#include <boost/capy/error.hpp>
#include <boost/capy/io_task.hpp>
#include <boost/capy/ex/this_coro.hpp>
#include <boost/capy/test/run_blocking.hpp>

#include <atomic>
#include <iostream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "test_suite.hpp"

// GCC gives false positive -Wmaybe-uninitialized on structured bindings
// via the tuple protocol inside coroutine frames.
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif

namespace capy = boost::capy;

namespace {

namespace overview {

std::atomic<int> runs{0};

capy::io_task<> task_a() { ++runs; co_return capy::io_result<>{}; }
capy::io_task<> task_b() { ++runs; co_return capy::io_result<>{}; }
capy::io_task<> task_c() { ++runs; co_return capy::io_result<>{}; }

// tag::sequential[]
capy::task<> sequential()
{
    co_await task_a();  // Wait for A
    co_await task_b();  // Then wait for B
    co_await task_c();  // Then wait for C
}
// end::sequential[]

// tag::concurrent[]
capy::task<> concurrent()
{
    // Run A, B, C simultaneously
    co_await capy::when_all(task_a(), task_b(), task_c());
}
// end::concurrent[]

} // namespace overview

namespace when_all_basics {

// tag::when_all_basic[]

capy::io_task<int> fetch_a()
{
    co_return capy::io_result<int>{std::error_code(), 1};
}

capy::io_task<int> fetch_b()
{
    co_return capy::io_result<int>{std::error_code(), 2};
}

capy::io_task<std::string> fetch_c()
{
    co_return capy::io_result<std::string>{std::error_code(), "hello"};
}

capy::task<> example()
{
    auto [ec, a, b, c] = co_await capy::when_all(
        fetch_a(), fetch_b(), fetch_c());

    // ec == std::error_code{} (success)
    // a == 1
    // b == 2
    // c == "hello"
}
// end::when_all_basic[]

} // namespace when_all_basics

namespace void_mix {

// tag::when_all_void_mix[]
capy::io_task<> void_task() { co_return capy::io_result<>{}; }

capy::io_task<int> int_task()
{
    co_return capy::io_result<int>{std::error_code(), 42};
}

capy::task<> example()
{
    auto [ec, a, b, c] = co_await capy::when_all(
        int_task(), void_task(), int_task());
    // a == 42       (int)
    // b == tuple<>  (from void io_task)
    // c == 42       (int)
}
// end::when_all_void_mix[]

} // namespace void_mix

namespace all_void {

capy::io_task<> void_task_a() { co_return capy::io_result<>{}; }
capy::io_task<> void_task_b() { co_return capy::io_result<>{}; }

// tag::when_all_all_void[]
capy::task<> example()
{
    auto r = co_await capy::when_all(void_task_a(), void_task_b());
    if (std::get<0>(r))
    {
        // handle error
    }
}
// end::when_all_all_void[]

} // namespace all_void

namespace error_handling {

capy::io_task<int> task_a()
{
    co_return capy::io_result<int>{std::error_code(), 1};
}

capy::io_task<int> task_b()
{
    co_return capy::io_result<int>{capy::error::timeout, 0};
}

// tag::when_all_error[]
capy::task<> example()
{
    auto [ec, a, b] = co_await capy::when_all(task_a(), task_b());
    if (ec)
        std::cerr << "Error: " << ec.message() << "\n";
}
// end::when_all_error[]

} // namespace error_handling

namespace exceptions {

// tag::when_all_exception[]
capy::io_task<int> might_throw(bool fail)
{
    if (fail)
        throw std::runtime_error("failed");
    co_return capy::io_result<int>{std::error_code(), 42};
}

capy::task<> example()
{
    try
    {
        co_await capy::when_all(might_throw(true), might_throw(false));
    }
    catch (std::runtime_error const& e)
    {
        // Catches the exception from the failing task
    }
}
// end::when_all_exception[]

} // namespace exceptions

namespace stop_prop {

std::atomic<int> iterations{0};

capy::io_task<> do_iteration()
{
    ++iterations;
    co_return capy::io_result<>{};
}

capy::io_task<> fail_fast()
{
    co_return capy::io_result<>{capy::error::timeout};
}

// tag::stop_propagation[]
capy::io_task<> long_running()
{
    auto token = co_await capy::this_coro::stop_token;

    for (int i = 0; i < 1000; ++i)
    {
        if (token.stop_requested())
            co_return capy::io_result<>{};  // Exit early when sibling fails

        co_await do_iteration();
    }
    co_return capy::io_result<>{};
}
// end::stop_propagation[]

} // namespace stop_prop

namespace any_basic {

capy::io_task<int> fetch_int()
{
    co_return capy::io_result<int>{std::error_code(), 7};
}

capy::io_task<std::string> fetch_string()
{
    co_return capy::io_result<std::string>{std::error_code(), "s"};
}

// tag::when_any_basic[]

capy::task<> example()
{
    auto result = co_await capy::when_any(
        fetch_int(),     // io_task<int>
        fetch_string()   // io_task<std::string>
    );
    // result is std::variant<std::error_code, int, std::string>
    // index 0: all tasks failed (error_code)
    // index 1: fetch_int won
    // index 2: fetch_string won
}
// end::when_any_basic[]

} // namespace any_basic

namespace wrap_translate {

capy::io_task<> inner() { co_return capy::io_result<>{capy::error::canceled}; }

// tag::wrap_translate_error[]
// canceled is benign here: translate it to success so when_any picks this child.
capy::io_task<> wrapped()
{
    auto [ec] = co_await inner();
    if (ec == capy::cond::canceled)
        co_return capy::io_result<>{};   // success: when_any sees a winner
    co_return capy::io_result<>{ec};     // propagate other errors unchanged
}
// end::wrap_translate_error[]

} // namespace wrap_translate

namespace wrap_lift {

capy::io_task<> inner() { co_return capy::io_result<>{capy::error::timeout}; }

// tag::wrap_lift_error[]
// Always succeeds; the winner's payload carries the original ec.
capy::io_task<std::error_code> wrapped()
{
    auto [ec] = co_await inner();
    co_return capy::io_result<std::error_code>{std::error_code(), ec};
}

// when_any(wrapped(), ...) -> variant<error_code, std::error_code, ...>
//   index 0: every child failed
//   index i: child i won; std::get<i>(result) is its original ec
// end::wrap_lift_error[]

} // namespace wrap_lift

namespace parallel {

struct page_data
{
    std::string header;
    std::string body;
    std::string sidebar;
};

capy::io_task<std::string> fetch_header(std::string url)
{
    co_return capy::io_result<std::string>{std::error_code(), url + ":header"};
}

capy::io_task<std::string> fetch_body(std::string url)
{
    co_return capy::io_result<std::string>{std::error_code(), url + ":body"};
}

capy::io_task<std::string> fetch_sidebar(std::string url)
{
    co_return capy::io_result<std::string>{
        std::error_code(), url + ":sidebar"};
}

// tag::parallel_fetch[]
capy::io_task<page_data> fetch_page_data(std::string url)
{
    auto [ec, header, body, sidebar] = co_await capy::when_all(
        fetch_header(url),
        fetch_body(url),
        fetch_sidebar(url)
    );
    if (ec)
        co_return capy::io_result<page_data>{ec, {}};

    co_return capy::io_result<page_data>{std::error_code(), {
        std::move(header),
        std::move(body),
        std::move(sidebar)
    }};
}
// end::parallel_fetch[]

} // namespace parallel

namespace fanout {

struct item
{
    int value;
};

// tag::fan_out[]
capy::io_task<int> process_item(item i);

capy::task<int> process_all(std::vector<item> const& items)
{
    std::vector<capy::io_task<int>> tasks;
    for (auto const& item : items)
        tasks.push_back(process_item(item));

    auto [ec, results] = co_await capy::when_all(std::move(tasks));
    if (ec)
        co_return 0;

    int total = 0;
    for (auto v : results)
        total += v;
    co_return total;
}
// end::fan_out[]

capy::io_task<int> process_item(item i)
{
    co_return capy::io_result<int>{std::error_code(), i.value};
}

} // namespace fanout

struct composition_test
{
    void testOverview()
    {
        overview::runs = 0;
        capy::test::run_blocking()(overview::sequential());
        BOOST_TEST_EQ(overview::runs.load(), 3);
        capy::test::run_blocking()(overview::concurrent());
        BOOST_TEST_EQ(overview::runs.load(), 6);
    }

    void testWhenAllBasic()
    {
        capy::test::run_blocking()(when_all_basics::example());

        bool checked = false;
        auto check = [&]() -> capy::task<>
        {
            auto [ec, a, b, c] = co_await capy::when_all(
                when_all_basics::fetch_a(),
                when_all_basics::fetch_b(),
                when_all_basics::fetch_c());
            BOOST_TEST(!ec);
            BOOST_TEST_EQ(a, 1);
            BOOST_TEST_EQ(b, 2);
            BOOST_TEST(c == "hello");
            checked = true;
        };
        capy::test::run_blocking()(check());
        BOOST_TEST(checked);
    }

    void testVoidMix()
    {
        capy::test::run_blocking()(void_mix::example());

        bool checked = false;
        auto check = [&]() -> capy::task<>
        {
            auto [ec, a, b, c] = co_await capy::when_all(
                void_mix::int_task(),
                void_mix::void_task(),
                void_mix::int_task());
            static_assert(std::is_same_v<decltype(b), std::tuple<>>);
            BOOST_TEST(!ec);
            BOOST_TEST_EQ(a, 42);
            BOOST_TEST_EQ(c, 42);
            checked = true;
        };
        capy::test::run_blocking()(check());
        BOOST_TEST(checked);
    }

    void testAllVoid()
    {
        capy::test::run_blocking()(all_void::example());

        bool checked = false;
        auto check = [&]() -> capy::task<>
        {
            auto r = co_await capy::when_all(
                all_void::void_task_a(),
                all_void::void_task_b());
            BOOST_TEST(!std::get<0>(r));
            checked = true;
        };
        capy::test::run_blocking()(check());
        BOOST_TEST(checked);
    }

    void testErrorHandling()
    {
        capy::test::run_blocking()(error_handling::example());

        bool checked = false;
        auto check = [&]() -> capy::task<>
        {
            auto [ec, a, b] = co_await capy::when_all(
                error_handling::task_a(),
                error_handling::task_b());
            BOOST_TEST(ec == capy::cond::timeout);
            checked = true;
        };
        capy::test::run_blocking()(check());
        BOOST_TEST(checked);
    }

    void testException()
    {
        capy::test::run_blocking()(exceptions::example());

        bool checked = false;
        auto check = [&]() -> capy::task<>
        {
            bool caught = false;
            try
            {
                co_await capy::when_all(
                    exceptions::might_throw(true),
                    exceptions::might_throw(false));
            }
            catch (std::runtime_error const&)
            {
                caught = true;
            }
            BOOST_TEST(caught);

            auto [ec, x, y] = co_await capy::when_all(
                exceptions::might_throw(false),
                exceptions::might_throw(false));
            BOOST_TEST(!ec);
            BOOST_TEST_EQ(x, 42);
            BOOST_TEST_EQ(y, 42);
            checked = true;
        };
        capy::test::run_blocking()(check());
        BOOST_TEST(checked);
    }

    void testStopPropagation()
    {
        stop_prop::iterations = 0;

        bool checked = false;
        auto check = [&]() -> capy::task<>
        {
            // fail_fast is posted first on the single-threaded test
            // context, so its error requests stop before long_running
            // starts iterating.
            auto r = co_await capy::when_all(
                stop_prop::fail_fast(),
                stop_prop::long_running());
            BOOST_TEST(std::get<0>(r) == capy::cond::timeout);
            checked = true;
        };
        capy::test::run_blocking()(check());
        BOOST_TEST(checked);
        BOOST_TEST_EQ(stop_prop::iterations.load(), 0);
    }

    void testWhenAnyBasic()
    {
        capy::test::run_blocking()(any_basic::example());

        bool checked = false;
        auto check = [&]() -> capy::task<>
        {
            auto result = co_await capy::when_any(
                any_basic::fetch_int(),
                any_basic::fetch_string());
            static_assert(std::is_same_v<decltype(result),
                std::variant<std::error_code, int, std::string>>);
            BOOST_TEST_EQ(result.index(), 1u);
            BOOST_TEST_EQ(std::get<1>(result), 7);
            checked = true;
        };
        capy::test::run_blocking()(check());
        BOOST_TEST(checked);
    }

    void testWrapTranslate()
    {
        bool checked = false;
        auto check = [&]() -> capy::task<>
        {
            auto [ec] = co_await wrap_translate::wrapped();
            BOOST_TEST(!ec);

            auto r = co_await capy::when_any(wrap_translate::wrapped());
            BOOST_TEST_EQ(r.index(), 1u);
            checked = true;
        };
        capy::test::run_blocking()(check());
        BOOST_TEST(checked);
    }

    void testWrapLift()
    {
        bool checked = false;
        auto check = [&]() -> capy::task<>
        {
            auto r = co_await capy::when_any(wrap_lift::wrapped());
            BOOST_TEST_EQ(r.index(), 1u);
            BOOST_TEST(std::get<1>(r) == capy::cond::timeout);
            checked = true;
        };
        capy::test::run_blocking()(check());
        BOOST_TEST(checked);
    }

    void testParallelFetch()
    {
        bool checked = false;
        auto check = [&]() -> capy::task<>
        {
            auto [ec, pd] = co_await parallel::fetch_page_data("url");
            BOOST_TEST(!ec);
            BOOST_TEST(pd.header == "url:header");
            BOOST_TEST(pd.body == "url:body");
            BOOST_TEST(pd.sidebar == "url:sidebar");
            checked = true;
        };
        capy::test::run_blocking()(check());
        BOOST_TEST(checked);
    }

    void testFanOut()
    {
        std::vector<fanout::item> items = {{1}, {2}, {3}};
        int total = 0;
        capy::test::run_blocking([&](int v) { total = v; })(
            fanout::process_all(items));
        BOOST_TEST_EQ(total, 6);
    }

    void run()
    {
        testOverview();
        testWhenAllBasic();
        testVoidMix();
        testAllVoid();
        testErrorHandling();
        testException();
        testStopPropagation();
        testWhenAnyBasic();
        testWrapTranslate();
        testWrapLift();
        testParallelFetch();
        testFanOut();
    }
};

} // namespace

TEST_SUITE(composition_test, "boost.capy.doc.4f_composition");
