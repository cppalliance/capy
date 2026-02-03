//
// Copyright (c) 2025 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#include <boost/capy/task.hpp>

#include <boost/capy/test/run_blocking.hpp>
#include "test_suite.hpp"

#include <stdexcept>
#include <string>

namespace boost {
namespace capy {

struct test_exception : std::runtime_error
{
    explicit test_exception(char const* msg)
        : std::runtime_error(msg)
    {
    }
};

struct run_blocking_test
{
    //----------------------------------------------------------
    // Return value tests
    //----------------------------------------------------------

    static task<int>
    returns_int()
    {
        co_return 42;
    }

    static task<std::string>
    returns_string()
    {
        co_return "hello";
    }

    void
    testReturnInt()
    {
        int result = 0;
        test::run_blocking([&](int v) { result = v; })(returns_int());
        BOOST_TEST_EQ(result, 42);
    }

    void
    testReturnString()
    {
        std::string result;
        test::run_blocking([&](std::string v) { result = std::move(v); })(returns_string());
        BOOST_TEST_EQ(result, "hello");
    }

    //----------------------------------------------------------
    // Void task tests
    //----------------------------------------------------------

    static task<void>
    void_task_basic()
    {
        co_return;
    }

    static task<void>
    void_task_with_side_effect(bool& flag)
    {
        flag = true;
        co_return;
    }

    void
    testVoidTask()
    {
        test::run_blocking()(void_task_basic());
        // No exception means success
        BOOST_TEST(true);
    }

    void
    testVoidTaskSideEffect()
    {
        bool flag = false;
        test::run_blocking()(void_task_with_side_effect(flag));
        BOOST_TEST(flag);
    }

    //----------------------------------------------------------
    // Exception tests
    //----------------------------------------------------------

    static task<int>
    throws_exception()
    {
        throw test_exception("test error");
        co_return 0;
    }

    static task<int>
    throws_std_exception()
    {
        throw std::runtime_error("runtime error");
        co_return 0;
    }

    static task<void>
    void_task_throws()
    {
        throw test_exception("void task error");
        co_return;
    }

    void
    testExceptionPropagation()
    {
        BOOST_TEST_THROWS(test::run_blocking()(throws_exception()), test_exception);
    }

    void
    testStdExceptionPropagation()
    {
        BOOST_TEST_THROWS(test::run_blocking()(throws_std_exception()), std::runtime_error);
    }

    void
    testVoidTaskException()
    {
        BOOST_TEST_THROWS(test::run_blocking()(void_task_throws()), test_exception);
    }

    //----------------------------------------------------------
    // Nested task tests
    //----------------------------------------------------------

    static task<int>
    inner_returns_value()
    {
        co_return 100;
    }

    static task<int>
    outer_awaits_inner()
    {
        int v = co_await inner_returns_value();
        co_return v + 1;
    }

    static task<int>
    inner_throws()
    {
        throw test_exception("inner exception");
        co_return 0;
    }

    static task<int>
    outer_awaits_throwing_inner()
    {
        int v = co_await inner_throws();
        co_return v + 1;
    }

    static task<int>
    outer_catches_inner_exception()
    {
        try
        {
            (void)co_await inner_throws();
            co_return -1;
        }
        catch (test_exception const&)
        {
            co_return 999;
        }
    }

    void
    testNestedTaskValue()
    {
        int result = 0;
        test::run_blocking([&](int v) { result = v; })(outer_awaits_inner());
        BOOST_TEST_EQ(result, 101);
    }

    void
    testNestedTaskException()
    {
        BOOST_TEST_THROWS(test::run_blocking()(outer_awaits_throwing_inner()), test_exception);
    }

    void
    testNestedTaskCatchException()
    {
        int result = 0;
        test::run_blocking([&](int v) { result = v; })(outer_catches_inner_exception());
        BOOST_TEST_EQ(result, 999);
    }

    //----------------------------------------------------------
    // Chained task tests (3+ levels)
    //----------------------------------------------------------

    static task<int>
    level3()
    {
        co_return 1;
    }

    static task<int>
    level2()
    {
        int v = co_await level3();
        co_return v + 10;
    }

    static task<int>
    level1()
    {
        int v = co_await level2();
        co_return v + 100;
    }

    void
    testChainedTasks()
    {
        int result = 0;
        test::run_blocking([&](int v) { result = v; })(level1());
        BOOST_TEST_EQ(result, 111);
    }

    static task<int>
    deeply_nested()
    {
        auto l4 = []() -> task<int> { co_return 1; };
        auto l3 = [l4]() -> task<int> { co_return co_await l4() + 10; };
        auto l2 = [l3]() -> task<int> { co_return co_await l3() + 100; };
        auto l1 = [l2]() -> task<int> { co_return co_await l2() + 1000; };
        co_return co_await l1();
    }

    void
    testDeeplyNestedTasks()
    {
        int result = 0;
        test::run_blocking([&](int v) { result = v; })(deeply_nested());
        BOOST_TEST_EQ(result, 1111);
    }

    //----------------------------------------------------------
    // Void task chain tests
    //----------------------------------------------------------

    static task<void>
    void_inner()
    {
        co_return;
    }

    static task<void>
    void_outer()
    {
        co_await void_inner();
        co_return;
    }

    static task<void>
    void_chain(int& counter)
    {
        ++counter;
        co_await void_inner();
        ++counter;
        co_await void_inner();
        ++counter;
        co_return;
    }

    void
    testVoidTaskChain()
    {
        test::run_blocking()(void_outer());
        BOOST_TEST(true);
    }

    void
    testVoidTaskChainWithCounter()
    {
        int counter = 0;
        test::run_blocking()(void_chain(counter));
        BOOST_TEST_EQ(counter, 3);
    }

    //----------------------------------------------------------
    // Mixed value and void task tests
    //----------------------------------------------------------

    static task<void>
    void_awaits_value()
    {
        int v = co_await returns_int();
        (void)v;
        co_return;
    }

    static task<int>
    value_awaits_void()
    {
        co_await void_task_basic();
        co_return 42;
    }

    void
    testVoidAwaitsValue()
    {
        test::run_blocking()(void_awaits_value());
        BOOST_TEST(true);
    }

    void
    testValueAwaitsVoid()
    {
        int result = 0;
        test::run_blocking([&](int v) { result = v; })(value_awaits_void());
        BOOST_TEST_EQ(result, 42);
    }

    //----------------------------------------------------------
    // Multiple sequential calls
    //----------------------------------------------------------

    void
    testMultipleCalls()
    {
        int a = 0, b = 0, c = 0;
        test::run_blocking([&](int v) { a = v; })(returns_int());
        test::run_blocking([&](int v) { b = v; })(returns_int());
        test::run_blocking([&](int v) { c = v; })(returns_int());
        BOOST_TEST_EQ(a + b + c, 126);
    }

    void
    testMixedCalls()
    {
        int a = 0;
        std::string s;
        int b = 0;

        test::run_blocking([&](int v) { a = v; })(returns_int());
        test::run_blocking()(void_task_basic());
        test::run_blocking([&](std::string v) { s = std::move(v); })(returns_string());
        test::run_blocking([&](int v) { b = v; })(outer_awaits_inner());

        BOOST_TEST_EQ(a, 42);
        BOOST_TEST_EQ(s, "hello");
        BOOST_TEST_EQ(b, 101);
    }

    //----------------------------------------------------------

    void
    run()
    {
        // Return value tests
        testReturnInt();
        testReturnString();

        // Void task tests
        testVoidTask();
        testVoidTaskSideEffect();

        // Exception tests
        testExceptionPropagation();
        testStdExceptionPropagation();
        testVoidTaskException();

        // Nested task tests
        testNestedTaskValue();
        testNestedTaskException();
        testNestedTaskCatchException();

        // Chained task tests
        testChainedTasks();
        testDeeplyNestedTasks();

        // Void task chain tests
        testVoidTaskChain();
        testVoidTaskChainWithCounter();

        // Mixed value and void task tests
        testVoidAwaitsValue();
        testValueAwaitsVoid();

        // Multiple sequential calls
        testMultipleCalls();
        testMixedCalls();
    }
};

TEST_SUITE(
    run_blocking_test,
    "boost.capy.test.run_blocking");

} // namespace capy
} // namespace boost
