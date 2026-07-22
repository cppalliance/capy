//
// Copyright (c) 2026 Steve Gerbino
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Compiled fragments shown in pages/4.coroutines/4f.composition.adoc.

// tag::when_all_basic[]
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

namespace {

using namespace boost::capy;

namespace overview {

std::atomic<int> runs{0};

io_task<> task_a() { ++runs; co_return io_result<>{}; }
io_task<> task_b() { ++runs; co_return io_result<>{}; }
io_task<> task_c() { ++runs; co_return io_result<>{}; }

// tag::sequential[]
task<> sequential()
{
    co_await task_a();  // Wait for A
    co_await task_b();  // Then wait for B
    co_await task_c();  // Then wait for C
}
// end::sequential[]

// tag::concurrent[]
task<> concurrent()
{
    // Run A, B, C simultaneously
    co_await when_all(task_a(), task_b(), task_c());
}
// end::concurrent[]

} // namespace overview

namespace when_all_basics {

// tag::when_all_basic[]

io_task<int> fetch_a() { co_return io_result<int>{{}, 1}; }
io_task<int> fetch_b() { co_return io_result<int>{{}, 2}; }
io_task<std::string> fetch_c() { co_return io_result<std::string>{{}, "hello"}; }

task<> example()
{
    auto [ec, a, b, c] = co_await when_all(fetch_a(), fetch_b(), fetch_c());

    // ec == std::error_code{} (success)
    // a == 1
    // b == 2
    // c == "hello"
}
// end::when_all_basic[]

} // namespace when_all_basics

namespace void_mix {

// tag::when_all_void_mix[]
io_task<> void_task() { co_return io_result<>{}; }
io_task<int> int_task() { co_return io_result<int>{{}, 42}; }

task<> example()
{
    auto [ec, a, b, c] = co_await when_all(int_task(), void_task(), int_task());
    // a == 42       (int)
    // b == tuple<>  (from void io_task)
    // c == 42       (int)
}
// end::when_all_void_mix[]

} // namespace void_mix

namespace all_void {

io_task<> void_task_a() { co_return io_result<>{}; }
io_task<> void_task_b() { co_return io_result<>{}; }

// tag::when_all_all_void[]
task<> example()
{
    auto r = co_await when_all(void_task_a(), void_task_b());
    if (r.ec)
    {
        // handle error
    }
}
// end::when_all_all_void[]

} // namespace all_void

namespace error_handling {

io_task<int> task_a() { co_return io_result<int>{{}, 1}; }
io_task<int> task_b() { co_return io_result<int>{error::timeout, 0}; }

// tag::when_all_error[]
task<> example()
{
    auto [ec, a, b] = co_await when_all(task_a(), task_b());
    if (ec)
        std::cerr << "Error: " << ec.message() << "\n";
}
// end::when_all_error[]

} // namespace error_handling

namespace exceptions {

// tag::when_all_exception[]
io_task<int> might_throw(bool fail)
{
    if (fail)
        throw std::runtime_error("failed");
    co_return io_result<int>{{}, 42};
}

task<> example()
{
    try
    {
        co_await when_all(might_throw(true), might_throw(false));
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

io_task<> do_iteration()
{
    ++iterations;
    co_return io_result<>{};
}

io_task<> fail_fast() { co_return io_result<>{error::timeout}; }

// tag::stop_propagation[]
io_task<> long_running()
{
    auto token = co_await this_coro::stop_token;

    for (int i = 0; i < 1000; ++i)
    {
        if (token.stop_requested())
            co_return io_result<>{};  // Exit early when sibling fails

        co_await do_iteration();
    }
    co_return io_result<>{};
}
// end::stop_propagation[]

} // namespace stop_prop

namespace any_basic {

io_task<int> fetch_int() { co_return io_result<int>{{}, 7}; }
io_task<std::string> fetch_string() { co_return io_result<std::string>{{}, "s"}; }

// tag::when_any_basic[]

task<> example()
{
    auto result = co_await when_any(
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

io_task<> inner() { co_return io_result<>{error::canceled}; }

// tag::wrap_translate_error[]
// canceled is benign here: translate it to success so when_any picks this child.
io_task<> wrapped()
{
    auto [ec] = co_await inner();
    if (ec == cond::canceled)
        co_return io_result<>{};   // success: when_any sees a winner
    co_return io_result<>{ec};     // propagate other errors unchanged
}
// end::wrap_translate_error[]

} // namespace wrap_translate

namespace wrap_lift {

io_task<> inner() { co_return io_result<>{error::timeout}; }

// tag::wrap_lift_error[]
// Always succeeds; the winner's payload carries the original ec.
io_task<std::error_code> wrapped()
{
    auto [ec] = co_await inner();
    co_return io_result<std::error_code>{{}, ec};
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

io_task<std::string> fetch_header(std::string url)
{
    co_return io_result<std::string>{{}, url + ":header"};
}

io_task<std::string> fetch_body(std::string url)
{
    co_return io_result<std::string>{{}, url + ":body"};
}

io_task<std::string> fetch_sidebar(std::string url)
{
    co_return io_result<std::string>{{}, url + ":sidebar"};
}

// tag::parallel_fetch[]
io_task<page_data> fetch_page_data(std::string url)
{
    auto [ec, header, body, sidebar] = co_await when_all(
        fetch_header(url),
        fetch_body(url),
        fetch_sidebar(url)
    );
    if (ec)
        co_return io_result<page_data>{ec, {}};

    co_return io_result<page_data>{{}, {
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
io_task<int> process_item(item const& i);

task<int> process_all(std::vector<item> const& items)
{
    std::vector<io_task<int>> tasks;
    for (auto const& item : items)
        tasks.push_back(process_item(item));

    auto [ec, results] = co_await when_all(std::move(tasks));
    if (ec)
        co_return 0;

    int total = 0;
    for (auto v : results)
        total += v;
    co_return total;
}
// end::fan_out[]

io_task<int> process_item(item const& i)
{
    co_return io_result<int>{{}, i.value};
}

} // namespace fanout

struct composition_test
{
    void testOverview()
    {
        overview::runs = 0;
        test::run_blocking()(overview::sequential());
        BOOST_TEST_EQ(overview::runs.load(), 3);
        test::run_blocking()(overview::concurrent());
        BOOST_TEST_EQ(overview::runs.load(), 6);
    }

    void testWhenAllBasic()
    {
        test::run_blocking()(when_all_basics::example());

        bool checked = false;
        auto check = [&]() -> task<>
        {
            auto [ec, a, b, c] = co_await when_all(
                when_all_basics::fetch_a(),
                when_all_basics::fetch_b(),
                when_all_basics::fetch_c());
            BOOST_TEST(!ec);
            BOOST_TEST_EQ(a, 1);
            BOOST_TEST_EQ(b, 2);
            BOOST_TEST(c == "hello");
            checked = true;
        };
        test::run_blocking()(check());
        BOOST_TEST(checked);
    }

    void testVoidMix()
    {
        test::run_blocking()(void_mix::example());

        bool checked = false;
        auto check = [&]() -> task<>
        {
            auto [ec, a, b, c] = co_await when_all(
                void_mix::int_task(),
                void_mix::void_task(),
                void_mix::int_task());
            static_assert(std::is_same_v<decltype(b), std::tuple<>>);
            BOOST_TEST(!ec);
            BOOST_TEST_EQ(a, 42);
            BOOST_TEST_EQ(c, 42);
            checked = true;
        };
        test::run_blocking()(check());
        BOOST_TEST(checked);
    }

    void testAllVoid()
    {
        test::run_blocking()(all_void::example());

        bool checked = false;
        auto check = [&]() -> task<>
        {
            auto r = co_await when_all(
                all_void::void_task_a(),
                all_void::void_task_b());
            BOOST_TEST(!r.ec);
            checked = true;
        };
        test::run_blocking()(check());
        BOOST_TEST(checked);
    }

    void testErrorHandling()
    {
        test::run_blocking()(error_handling::example());

        bool checked = false;
        auto check = [&]() -> task<>
        {
            auto [ec, a, b] = co_await when_all(
                error_handling::task_a(),
                error_handling::task_b());
            BOOST_TEST(ec == cond::timeout);
            checked = true;
        };
        test::run_blocking()(check());
        BOOST_TEST(checked);
    }

    void testException()
    {
        test::run_blocking()(exceptions::example());

        bool checked = false;
        auto check = [&]() -> task<>
        {
            bool caught = false;
            try
            {
                co_await when_all(
                    exceptions::might_throw(true),
                    exceptions::might_throw(false));
            }
            catch (std::runtime_error const&)
            {
                caught = true;
            }
            BOOST_TEST(caught);

            auto [ec, x, y] = co_await when_all(
                exceptions::might_throw(false),
                exceptions::might_throw(false));
            BOOST_TEST(!ec);
            BOOST_TEST_EQ(x, 42);
            BOOST_TEST_EQ(y, 42);
            checked = true;
        };
        test::run_blocking()(check());
        BOOST_TEST(checked);
    }

    void testStopPropagation()
    {
        stop_prop::iterations = 0;

        bool checked = false;
        auto check = [&]() -> task<>
        {
            // fail_fast is posted first on the single-threaded test
            // context, so its error requests stop before long_running
            // starts iterating.
            auto r = co_await when_all(
                stop_prop::fail_fast(),
                stop_prop::long_running());
            BOOST_TEST(r.ec == cond::timeout);
            checked = true;
        };
        test::run_blocking()(check());
        BOOST_TEST(checked);
        BOOST_TEST_EQ(stop_prop::iterations.load(), 0);
    }

    void testWhenAnyBasic()
    {
        test::run_blocking()(any_basic::example());

        bool checked = false;
        auto check = [&]() -> task<>
        {
            auto result = co_await when_any(
                any_basic::fetch_int(),
                any_basic::fetch_string());
            static_assert(std::is_same_v<decltype(result),
                std::variant<std::error_code, int, std::string>>);
            BOOST_TEST_EQ(result.index(), 1u);
            BOOST_TEST_EQ(std::get<1>(result), 7);
            checked = true;
        };
        test::run_blocking()(check());
        BOOST_TEST(checked);
    }

    void testWrapTranslate()
    {
        bool checked = false;
        auto check = [&]() -> task<>
        {
            auto [ec] = co_await wrap_translate::wrapped();
            BOOST_TEST(!ec);

            auto r = co_await when_any(wrap_translate::wrapped());
            BOOST_TEST_EQ(r.index(), 1u);
            checked = true;
        };
        test::run_blocking()(check());
        BOOST_TEST(checked);
    }

    void testWrapLift()
    {
        bool checked = false;
        auto check = [&]() -> task<>
        {
            auto r = co_await when_any(wrap_lift::wrapped());
            BOOST_TEST_EQ(r.index(), 1u);
            BOOST_TEST(std::get<1>(r) == cond::timeout);
            checked = true;
        };
        test::run_blocking()(check());
        BOOST_TEST(checked);
    }

    void testParallelFetch()
    {
        bool checked = false;
        auto check = [&]() -> task<>
        {
            auto [ec, pd] = co_await parallel::fetch_page_data("url");
            BOOST_TEST(!ec);
            BOOST_TEST(pd.header == "url:header");
            BOOST_TEST(pd.body == "url:body");
            BOOST_TEST(pd.sidebar == "url:sidebar");
            checked = true;
        };
        test::run_blocking()(check());
        BOOST_TEST(checked);
    }

    void testFanOut()
    {
        std::vector<fanout::item> items = {{1}, {2}, {3}};
        int total = 0;
        test::run_blocking([&](int v) { total = v; })(
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
