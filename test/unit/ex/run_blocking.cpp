//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Test that header file is self-contained.
#include <boost/capy/test/run_blocking.hpp>

#include <boost/capy/task.hpp>
#include <boost/capy/ex/this_coro.hpp>

#include "test_suite.hpp"

#include <stdexcept>
#include <string>

namespace boost {
namespace capy {
namespace test {

static_assert(Executor<blocking_executor>);
static_assert(ExecutionContext<blocking_context>);

//----------------------------------------------------------
// Test Exception Type
//----------------------------------------------------------

struct test_exception : std::runtime_error
{
    explicit test_exception(char const* msg)
        : std::runtime_error(msg)
    {
    }
};

//----------------------------------------------------------
// run_blocking Tests
//----------------------------------------------------------

struct run_blocking_test
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
    testVoidTaskNoHandler()
    {
        // Should complete without blocking forever
        run_blocking()(returns_void());
        BOOST_TEST(true);
    }

    void
    testIntTaskNoHandler()
    {
        // Result is discarded
        run_blocking()(returns_int());
        BOOST_TEST(true);
    }

    void
    testIntTaskWithHandler()
    {
        int result = 0;
        run_blocking([&](int v) { result = v; })(returns_int());
        BOOST_TEST_EQ(result, 42);
    }

    void
    testVoidTaskWithHandler()
    {
        bool called = false;
        run_blocking([&]() { called = true; })(returns_void());
        BOOST_TEST(called);
    }

    void
    testStringTaskWithHandler()
    {
        std::string result;
        run_blocking([&](std::string s) { result = std::move(s); })(
            returns_string());
        BOOST_TEST_EQ(result, "hello");
    }

    void
    testDualHandlers()
    {
        int result = 0;
        bool error_called = false;

        run_blocking(
            [&](int v) { result = v; },
            [&](std::exception_ptr) { error_called = true; }
        )(returns_int());

        BOOST_TEST_EQ(result, 42);
        BOOST_TEST(!error_called);
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

    void
    testExceptionHandler()
    {
        bool success_called = false;
        bool error_called = false;

        run_blocking(
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
    testVoidExceptionHandler()
    {
        bool success_called = false;
        bool error_called = false;

        run_blocking(
            [&]() { success_called = true; },
            [&](std::exception_ptr ep) {
                error_called = true;
                BOOST_TEST(ep != nullptr);
            }
        )(void_throws_exception());

        BOOST_TEST(!success_called);
        BOOST_TEST(error_called);
    }

    void
    testExceptionRethrown()
    {
        bool caught = false;
        try
        {
            run_blocking()(throws_exception());
        }
        catch(test_exception const& e)
        {
            caught = true;
            BOOST_TEST(std::string(e.what()) == "test error");
        }
        BOOST_TEST(caught);
    }

    void
    testOverloadedHandlerException()
    {
        bool got_value = false;
        bool got_exception = false;

        struct handler_t
        {
            bool* got_value_;
            bool* got_exception_;
            void operator()(int) { *got_value_ = true; }
            void operator()(std::exception_ptr) { *got_exception_ = true; }
        };

        run_blocking(handler_t{&got_value, &got_exception})(
            throws_exception());

        BOOST_TEST(!got_value);
        BOOST_TEST(got_exception);
    }

    //----------------------------------------------------------
    // blocking_executor Tests
    //----------------------------------------------------------

    void
    testBlockingExecutorConcept()
    {
        static_assert(Executor<blocking_executor>);
        static_assert(ExecutionContext<blocking_context>);
        BOOST_TEST(true);
    }

    //----------------------------------------------------------
    // Stop Token Tests
    //----------------------------------------------------------

    static task<bool>
    check_stop_requested()
    {
        auto token = co_await this_coro::stop_token;
        co_return token.stop_requested();
    }

    void
    testStopTokenNotRequested()
    {
        bool result = true;

        std::stop_source source;
        run_blocking(source.get_token(), [&](bool v) { result = v; })(
            check_stop_requested());

        BOOST_TEST(!result);
    }

    void
    testStopTokenRequested()
    {
        bool result = false;

        std::stop_source source;
        source.request_stop();

        run_blocking(source.get_token(), [&](bool v) { result = v; })(
            check_stop_requested());

        BOOST_TEST(result);
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
        bool result = false;
        run_blocking([&](bool v) { result = v; })(
            check_allocator_propagated());
        BOOST_TEST(result);
    }

    //----------------------------------------------------------
    // Nested Task Tests
    //----------------------------------------------------------

    static task<int>
    nested_task()
    {
        int v = co_await returns_int();
        co_return v + 1;
    }

    void
    testNestedTask()
    {
        int result = 0;
        run_blocking([&](int v) { result = v; })(nested_task());
        BOOST_TEST_EQ(result, 43);
    }

    //----------------------------------------------------------

    void
    run()
    {
        // Basic Functionality
        testVoidTaskNoHandler();
        testIntTaskNoHandler();
        testIntTaskWithHandler();
        testVoidTaskWithHandler();
        testStringTaskWithHandler();
        testDualHandlers();

        // Exception Handling
        testExceptionHandler();
        testVoidExceptionHandler();
        testExceptionRethrown();
        testOverloadedHandlerException();

        // blocking_executor
        testBlockingExecutorConcept();

        // Stop Token
        testStopTokenNotRequested();
        testStopTokenRequested();

        // Allocator Propagation
        testAllocatorPropagation();

        // Nested Tasks
        testNestedTask();
    }
};

TEST_SUITE(
    run_blocking_test,
    "boost.capy.ex.run_blocking");

} // namespace test
} // namespace capy
} // namespace boost
