//
// Copyright (c) 2026 Steve Gerbino
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Compiled fragments shown in pages/2.cpp20-coroutines/2b.syntax.adoc.
// Pages include the tagged regions; scaffolding stays outside the tags.

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
#include <boost/capy/ex/run_async.hpp>
#include <boost/capy/ex/thread_pool.hpp>

// The simple_coroutine fragment shows this include; the tag region is
// split so the directive stays at file scope.
// tag::simple_coroutine[]
#include <coroutine>
// end::simple_coroutine[]

#include <string>
#include <utility>

#include "test_suite.hpp"

namespace capy = boost::capy;

namespace {

using capy::task;

struct http_response
{
    std::string body;
};

task<http_response> http_get(std::string)
{
    co_return http_response{"<html></html>"};
}

// tag::co_await_example[]
task<std::string> fetch_page(std::string url)
{
    auto response = co_await http_get(url);  // suspends until HTTP completes
    co_return response.body;                 // continues after resumption
}
// end::co_await_example[]

// Minimal generator so the co_yield fragment compiles; the page teaches
// the keyword, not a production generator type.
template<class T>
struct generator
{
    struct promise_type
    {
        T value_;

        generator get_return_object()
        {
            return generator{
                std::coroutine_handle<promise_type>::from_promise(*this)};
        }
        std::suspend_always initial_suspend() { return {}; }
        std::suspend_always final_suspend() noexcept { return {}; }
        std::suspend_always yield_value(T v)
        {
            value_ = v;
            return {};
        }
        void return_void() {}
        void unhandled_exception() {}
    };

    std::coroutine_handle<promise_type> h_;

    explicit generator(std::coroutine_handle<promise_type> h) : h_(h) {}
    generator(generator&& other) noexcept : h_(std::exchange(other.h_, {})) {}
    ~generator() { if (h_) h_.destroy(); }

    bool next()
    {
        h_.resume();
        return !h_.done();
    }

    T value() const { return h_.promise().value_; }
};

// tag::co_yield_example[]
generator<int> count_to(int n)
{
    for (int i = 1; i <= n; ++i)
    {
        co_yield i;  // produce value, suspend, resume when next value requested
    }
}
// end::co_yield_example[]

// tag::co_return_example[]
task<int> compute()
{
    int result = 42;
    co_return result;  // completes the coroutine with value 42
}
// end::co_return_example[]

// tag::simple_coroutine[]

struct SimpleCoroutine
{
    struct promise_type
    {
        SimpleCoroutine get_return_object() { return {}; }
        std::suspend_never initial_suspend() { return {}; }
        std::suspend_never final_suspend() noexcept { return {}; }
        void return_void() {}
        void unhandled_exception() {}
    };
};

SimpleCoroutine my_first_coroutine()
{
    co_return;  // This makes it a coroutine
}
// end::simple_coroutine[]

// Never invoked: awaiting suspend_always here would leave the frame
// suspended forever. Compiling the fragment is the test.
[[maybe_unused]]
SimpleCoroutine standard_awaiters()
{
    // tag::standard_awaiters[]
    // suspend_always causes suspension at this point
    co_await std::suspend_always{};

    // suspend_never continues immediately without suspending
    co_await std::suspend_never{};
    // end::standard_awaiters[]
}

struct syntax_test
{
    void testFetchPage()
    {
        capy::thread_pool pool(1);
        capy::run_async(pool.get_executor(), [](std::string const& body) {
            BOOST_TEST(body == "<html></html>");
        })(fetch_page("https://example.com"));
        pool.join();
    }

    void testCountTo()
    {
        auto gen = count_to(3);
        int expected = 1;
        while (gen.next())
            BOOST_TEST(gen.value() == expected++);
        BOOST_TEST(expected == 4);
    }

    void testCompute()
    {
        capy::thread_pool pool(1);
        capy::run_async(pool.get_executor(), [](int result) {
            BOOST_TEST(result == 42);
        })(compute());
        pool.join();
    }

    void testFirstCoroutine()
    {
        // Runs to completion immediately: both suspend points are
        // suspend_never, so the frame is freed before this returns.
        my_first_coroutine();
    }

    void run()
    {
        testFetchPage();
        testCountTo();
        testCompute();
        testFirstCoroutine();
    }
};

} // namespace

TEST_SUITE(syntax_test, "boost.capy.doc.2b_syntax");
