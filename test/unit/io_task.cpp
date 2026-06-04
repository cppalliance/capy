//
// Copyright (c) 2026 Klemens Morgenstern
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Test that header file is self-contained.
#include <boost/capy/io_task.hpp>

#include <boost/capy/io_result.hpp>
#include <boost/capy/test/run_blocking.hpp>

#include "test_suite.hpp"

#include <string>
#include <system_error>

namespace boost {
namespace capy {

struct io_task_yield_test
{
    //----------------------------------------------------------
    // co_yield io_result<> with no error: continues, returns
    // the payload tuple from the yield expression.
    //----------------------------------------------------------

    static io_task<int>
    yields_success_single()
    {
        auto [v] = co_yield io_result<int>{{}, 7};
        co_return io_result<int>{{}, v + 1};
    }

    void
    testYieldSuccessSingleValue()
    {
        io_result<int> result;
        test::run_blocking([&](io_result<int> r) {
            result = std::move(r);
        })(yields_success_single());

        BOOST_TEST(!result.ec);
        BOOST_TEST_EQ(std::get<0>(result.values), 8);
    }

    static io_task<int>
    yields_success_multi()
    {
        auto [a, b, c] = co_yield io_result<int, int, int>{{}, 1, 2, 3};
        co_return io_result<int>{{}, a + b + c};
    }

    void
    testYieldSuccessMultipleValues()
    {
        io_result<int> result;
        test::run_blocking([&](io_result<int> r) {
            result = std::move(r);
        })(yields_success_multi());

        BOOST_TEST(!result.ec);
        BOOST_TEST_EQ(std::get<0>(result.values), 6);
    }

    static io_task<std::string>
    yields_success_string()
    {
        auto [s] = co_yield io_result<std::string>{{}, std::string("hello")};
        co_return io_result<std::string>{{}, s + " world"};
    }

    void
    testYieldSuccessStringPayload()
    {
        io_result<std::string> result;
        test::run_blocking([&](io_result<std::string> r) {
            result = std::move(r);
        })(yields_success_string());

        BOOST_TEST(!result.ec);
        BOOST_TEST_EQ(std::get<0>(result.values), "hello world");
    }

    //----------------------------------------------------------
    // co_yield io_result<> with an error short-circuits: the
    // remainder of the coroutine after the yield must not run.
    //----------------------------------------------------------

    static io_task<int>
    yields_error(bool* after_yield_ran)
    {
        auto ec = make_error_code(std::errc::invalid_argument);
        auto [v] = co_yield io_result<int>{ec, 99};
        *after_yield_ran = true;
        co_return io_result<int>{{}, v};
    }

    void
    testYieldErrorShortCircuits()
    {
        bool after_yield_ran = false;
        io_result<int> result;

        test::run_blocking([&](io_result<int> r) {
            result = std::move(r);
        })(yields_error(&after_yield_ran));

        BOOST_TEST(!after_yield_ran);
        BOOST_TEST(result.ec == make_error_code(std::errc::invalid_argument));
    }

    //----------------------------------------------------------
    // Yielding void io_result (no payload) also short-circuits
    // on error and continues otherwise.
    //----------------------------------------------------------

    static io_task<>
    yields_void_success(bool* after_yield_ran)
    {
        co_yield io_result<>{};
        *after_yield_ran = true;
        co_return io_result<>{};
    }

    void
    testYieldVoidSuccessContinues()
    {
        bool after_yield_ran = false;
        io_result<> result{make_error_code(std::errc::invalid_argument)};

        test::run_blocking([&](io_result<> r) {
            result = std::move(r);
        })(yields_void_success(&after_yield_ran));

        BOOST_TEST(after_yield_ran);
        BOOST_TEST(!result.ec);
    }

    static io_task<>
    yields_void_error(bool* after_yield_ran)
    {
        auto ec = make_error_code(std::errc::operation_canceled);
        co_yield io_result<>{ec};
        *after_yield_ran = true;
        co_return io_result<>{};
    }

    void
    testYieldVoidErrorShortCircuits()
    {
        bool after_yield_ran = false;
        io_result<> result;

        test::run_blocking([&](io_result<> r) {
            result = std::move(r);
        })(yields_void_error(&after_yield_ran));

        BOOST_TEST(!after_yield_ran);
        BOOST_TEST(result.ec == make_error_code(std::errc::operation_canceled));
    }

    void
    run()
    {
        testYieldSuccessSingleValue();
        testYieldSuccessMultipleValues();
        testYieldSuccessStringPayload();
        testYieldErrorShortCircuits();
        testYieldVoidSuccessContinues();
        testYieldVoidErrorShortCircuits();
    }
};

TEST_SUITE(io_task_yield_test, "boost.capy.io_task");

} // namespace capy
} // namespace boost
