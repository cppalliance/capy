//
// Copyright (c) 2025 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Test that header file is self-contained.
#include <boost/capy/async_result.hpp>

#ifdef BOOST_CAPY_HAS_CORO

#include <boost/capy/task.hpp>
#include <boost/system/error_code.hpp>

#include "test_suite.hpp"

#include <stdexcept>
#include <string>

namespace boost {
namespace capy {

template<class T>
T run_task(task<T>& t)
{
    while (!t.handle().done())
        t.handle().resume();
    return t.await_resume();
}

template<>
void run_task<void>(task<void>& t)
{
    while (!t.handle().done())
        t.handle().resume();
    t.await_resume();
}

struct async_test_exception : std::runtime_error
{
    explicit async_test_exception(const char* msg)
        : std::runtime_error(msg)
    {
    }
};

struct result_with_error
{
    int value;
    boost::system::error_code ec;

    result_with_error() = default;

    result_with_error(int v, boost::system::error_code e = {})
        : value(v)
        , ec(e)
    {
    }
};

struct async_result_test
{
    static async_result<int>
    async_int_value()
    {
        return make_async_result<int>(
            [](auto cb) {
                cb(42);
            });
    }

    static async_result<std::string>
    async_string_value()
    {
        return make_async_result<std::string>(
            [](auto cb) {
                cb("hello async");
            });
    }

    static task<int>
    task_awaiting_int()
    {
        int v = co_await async_int_value();
        co_return v;
    }

    static task<std::string>
    task_awaiting_string()
    {
        std::string s = co_await async_string_value();
        co_return s;
    }

    void
    testBasicValue()
    {
        // async_result returning int
        {
            auto t = task_awaiting_int();
            BOOST_TEST_EQ(run_task(t), 42);
        }

        // async_result returning string
        {
            auto t = task_awaiting_string();
            BOOST_TEST_EQ(run_task(t), "hello async");
        }
    }

    static async_result<result_with_error>
    async_returns_success()
    {
        return make_async_result<result_with_error>(
            [](auto cb) {
                cb(100, boost::system::error_code{});
            });
    }

    static async_result<result_with_error>
    async_returns_error()
    {
        return make_async_result<result_with_error>(
            [](auto cb) {
                cb(0, boost::system::errc::make_error_code(
                    boost::system::errc::invalid_argument));
            });
    }

    static task<result_with_error>
    task_awaits_success()
    {
        auto r = co_await async_returns_success();
        co_return r;
    }

    static task<result_with_error>
    task_awaits_error()
    {
        auto r = co_await async_returns_error();
        co_return r;
    }

    static task<int>
    task_checks_error_and_returns()
    {
        auto r = co_await async_returns_error();
        if (r.ec)
            co_return -1;
        co_return r.value;
    }

    void
    testErrorHandling()
    {
        // async_result with success
        {
            auto t = task_awaits_success();
            auto r = run_task(t);
            BOOST_TEST_EQ(r.value, 100);
            BOOST_TEST(!r.ec);
        }

        // async_result with error
        {
            auto t = task_awaits_error();
            auto r = run_task(t);
            BOOST_TEST_EQ(r.value, 0);
            BOOST_TEST(r.ec);
            BOOST_TEST_EQ(r.ec, boost::system::errc::invalid_argument);
        }

        // task checks error and returns appropriate value
        {
            auto t = task_checks_error_and_returns();
            BOOST_TEST_EQ(run_task(t), -1);
        }
    }

    static async_result<int>
    async_value_1()
    {
        return make_async_result<int>(
            [](auto cb) { cb(10); });
    }

    static async_result<int>
    async_value_2()
    {
        return make_async_result<int>(
            [](auto cb) { cb(20); });
    }

    static async_result<int>
    async_value_3()
    {
        return make_async_result<int>(
            [](auto cb) { cb(30); });
    }

    static task<int>
    task_awaits_multiple()
    {
        int v1 = co_await async_value_1();
        int v2 = co_await async_value_2();
        int v3 = co_await async_value_3();
        co_return v1 + v2 + v3;
    }

    void
    testMultipleAwaits()
    {
        auto t = task_awaits_multiple();
        BOOST_TEST_EQ(run_task(t), 60);
    }

    void
    testAwaitReady()
    {
        auto ar = async_int_value();
        BOOST_TEST(!ar.await_ready());
    }

    void
    testMoveOperations()
    {
        // async_result is move constructible
        {
            auto ar1 = async_int_value();
            auto ar2 = std::move(ar1);
            (void)ar2;
        }

        // async_result is move assignable
        {
            auto ar1 = async_int_value();
            auto ar2 = async_string_value();
            (void)ar1;
            (void)ar2;
        }
    }

    static async_result<int>
    async_with_captured_state(int multiplier)
    {
        return make_async_result<int>(
            [multiplier](auto cb) {
                cb(10 * multiplier);
            });
    }

    static task<int>
    task_awaits_with_state()
    {
        int v1 = co_await async_with_captured_state(2);
        int v2 = co_await async_with_captured_state(3);
        co_return v1 + v2;
    }

    void
    testCapturedState()
    {
        auto t = task_awaits_with_state();
        BOOST_TEST_EQ(run_task(t), 50);
    }

    struct complex_result
    {
        int id;
        std::string name;
        double value;

        complex_result() = default;
        complex_result(int i, std::string n, double v)
            : id(i)
            , name(std::move(n))
            , value(v)
        {
        }
    };

    static async_result<complex_result>
    async_complex()
    {
        return make_async_result<complex_result>(
            [](auto cb) {
                cb(1, "test", 3.14);
            });
    }

    static task<complex_result>
    task_awaits_complex()
    {
        auto r = co_await async_complex();
        co_return r;
    }

    void
    testComplexResult()
    {
        auto t = task_awaits_complex();
        auto r = run_task(t);
        BOOST_TEST_EQ(r.id, 1);
        BOOST_TEST_EQ(r.name, "test");
        BOOST_TEST_EQ(r.value, 3.14);
    }

    static task<int>
    inner_task_with_async()
    {
        int v = co_await async_int_value();
        co_return v * 2;
    }

    static task<int>
    outer_task_with_both()
    {
        int v1 = co_await async_value_1();
        int v2 = co_await inner_task_with_async();
        co_return v1 + v2;
    }

    void
    testTaskChaining()
    {
        auto t = outer_task_with_both();
        BOOST_TEST_EQ(run_task(t), 94);
    }

    //----------------------------------------------------------
    // async_result<void> tests
    //----------------------------------------------------------

    static async_result<void>
    async_void_basic()
    {
        return make_async_result<void>(
            [](auto on_done) {
                on_done();
            });
    }

    static task<void>
    task_awaits_void_async()
    {
        co_await async_void_basic();
        co_return;
    }

    void
    testVoidAsyncBasic()
    {
        bool done = false;
        auto t = task_awaits_void_async();
        t.handle().promise().on_done = [&done]{ done = true; };
        t.handle().resume();
        BOOST_TEST(done);
    }

    static async_result<void>
    async_void_step()
    {
        return make_async_result<void>(
            [](auto on_done) {
                on_done();
            });
    }

    static task<int>
    task_awaits_void_then_value()
    {
        co_await async_void_step();
        int v = co_await async_int_value();
        co_await async_void_step();
        co_return v;
    }

    void
    testVoidAsyncWithValue()
    {
        auto t = task_awaits_void_then_value();
        BOOST_TEST_EQ(run_task(t), 42);
    }

    static task<void>
    task_awaits_multiple_void()
    {
        co_await async_void_step();
        co_await async_void_step();
        co_await async_void_step();
        co_return;
    }

    void
    testVoidAsyncChain()
    {
        bool done = false;
        auto t = task_awaits_multiple_void();
        t.handle().promise().on_done = [&done]{ done = true; };
        t.handle().resume();
        BOOST_TEST(done);
    }

    void
    testVoidAsyncAwaitReady()
    {
        auto ar = async_void_basic();
        BOOST_TEST(!ar.await_ready());
    }

    void
    testVoidAsyncMove()
    {
        auto ar1 = async_void_basic();
        auto ar2 = std::move(ar1);
        (void)ar2;
    }

    static async_result<void>
    async_void_deferred()
    {
        return make_async_result<void>(
            [](auto on_done) {
                // Simulate deferred completion
                on_done();
            });
    }

    static task<int>
    task_with_deferred_void()
    {
        co_await async_void_deferred();
        co_return 999;
    }

    void
    testVoidAsyncDeferred()
    {
        auto t = task_with_deferred_void();
        BOOST_TEST_EQ(run_task(t), 999);
    }

    void
    run()
    {
        testBasicValue();
        testErrorHandling();
        testMultipleAwaits();
        testAwaitReady();
        testMoveOperations();
        testCapturedState();
        testComplexResult();
        testTaskChaining();

        // async_result<void> tests
        testVoidAsyncBasic();
        testVoidAsyncWithValue();
        testVoidAsyncChain();
        testVoidAsyncAwaitReady();
        testVoidAsyncMove();
        testVoidAsyncDeferred();
    }
};

TEST_SUITE(
    async_result_test,
    "boost.capy.async_result");

} // capy
} // boost

#endif
