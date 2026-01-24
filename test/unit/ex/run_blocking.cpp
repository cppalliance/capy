//
// Copyright (c) 2025 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Test that header file is self-contained.
#include <boost/capy/test/run_blocking.hpp>

#include <boost/capy/task.hpp>

#include "test_suite.hpp"

#include <stdexcept>
#include <string>

namespace boost {
namespace capy {
namespace test {

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
    // inline_executor Tests
    //----------------------------------------------------------

    void
    testInlineExecutorConcept()
    {
        static_assert(Executor<inline_executor>);
        BOOST_TEST(true);
    }

    void
    testInlineExecutorEquality()
    {
        inline_executor ex1;
        inline_executor ex2;
        BOOST_TEST(ex1 == ex2);
    }

    void
    testInlineExecutorPostThrows()
    {
        inline_executor ex;
        bool caught = false;
        try
        {
            ex.post(coro{});
        }
        catch(std::logic_error const& e)
        {
            caught = true;
            BOOST_TEST(std::string(e.what()).find("post") != std::string::npos);
        }
        BOOST_TEST(caught);
    }

    void
    testInlineExecutorDispatch()
    {
        inline_executor ex;
        coro dummy = std::noop_coroutine();
        coro result = ex.dispatch(dummy);
        BOOST_TEST(result == dummy);
    }

    //----------------------------------------------------------
    // Explicit Executor Tests
    //----------------------------------------------------------

    void
    testWithExplicitExecutor()
    {
        int dispatch_count = 0;

        struct counting_executor
        {
            int* count_;

            bool operator==(counting_executor const& other) const noexcept
            {
                return count_ == other.count_;
            }

            execution_context& context() const noexcept
            {
                struct local_context : public execution_context {};
                static local_context ctx;
                return ctx;
            }

            void on_work_started() const noexcept {}
            void on_work_finished() const noexcept {}

            coro dispatch(coro h) const
            {
                ++(*count_);
                return h;
            }

            void post(coro h) const
            {
                h.resume();
            }
        };

        int result = 0;
        run_blocking(
            counting_executor{&dispatch_count},
            [&](int v) { result = v; }
        )(returns_int());

        BOOST_TEST_EQ(result, 42);
        BOOST_TEST_EQ(dispatch_count, 1);
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

        // inline_executor
        testInlineExecutorConcept();
        testInlineExecutorEquality();
        testInlineExecutorPostThrows();
        testInlineExecutorDispatch();

        // Explicit Executor
        testWithExplicitExecutor();

        // Stop Token
        testStopTokenNotRequested();
        testStopTokenRequested();

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
