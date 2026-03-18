//
// Copyright (c) 2026 Michael Vandeberg
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Test that header file is self-contained.
#include <boost/capy/timeout.hpp>

#include <boost/capy/cond.hpp>
#include <boost/capy/error.hpp>
#include <boost/capy/ex/run_async.hpp>
#include <boost/capy/ex/thread_pool.hpp>
#include <boost/capy/io_task.hpp>

#include "test_helpers.hpp"
#include "test_suite.hpp"

#include <latch>
#include <string>

namespace boost {
namespace capy {

using namespace std::chrono_literals;

//----------------------------------------------------------
// Helper tasks for timeout testing
//----------------------------------------------------------

// Returns an io_result<int> immediately
inline io_task<int>
returns_io_int(int value)
{
    co_return io_result<int>{{}, value};
}

// Returns an io_result<std::string> immediately
inline io_task<std::string>
returns_io_string(std::string value)
{
    co_return io_result<std::string>{{}, std::move(value)};
}

// Returns io_result<> immediately (void equivalent)
inline io_task<>
returns_io_void()
{
    co_return io_result<>{};
}

// Returns io_result<std::size_t> after stop is requested
inline io_task<std::size_t>
slow_io_result(std::size_t n)
{
    co_await stop_only_awaitable{};
    co_return io_result<std::size_t>{{}, n};
}

// Returns io_result<int> after stop is requested
inline io_task<int>
slow_io_int(int value)
{
    co_await stop_only_awaitable{};
    co_return io_result<int>{{}, value};
}

// Returns io_result<> after stop is requested
inline io_task<>
slow_io_void()
{
    co_await stop_only_awaitable{};
    co_return io_result<>{};
}

// io_task that throws an exception immediately
inline io_task<int>
io_immediate_throw(char const* msg)
{
    throw test_exception(msg);
    co_return io_result<int>{{}, 0};
}

//----------------------------------------------------------
// Tests
//----------------------------------------------------------

struct timeout_test
{
    // Test: io_result<int> completes before timeout
    void
    testTaskCompletesBeforeTimeout()
    {
        thread_pool pool(1);
        std::latch done(1);
        io_result<int> result{};

        run_async(pool.get_executor(),
            [&](io_result<int> r) {
                result = r;
                done.count_down();
            },
            [&](std::exception_ptr) {
                done.count_down();
            })(timeout(returns_io_int(42), 5s));

        done.wait();
        BOOST_TEST(!result.ec);
        BOOST_TEST_EQ(std::get<0>(result.values), 42);
    }

    // Test: io_result<string> completes before timeout
    void
    testTaskCompletesWithString()
    {
        thread_pool pool(1);
        std::latch done(1);
        io_result<std::string> result{};

        run_async(pool.get_executor(),
            [&](io_result<std::string> r) {
                result = std::move(r);
                done.count_down();
            },
            [&](std::exception_ptr) {
                done.count_down();
            })(timeout(returns_io_string("hello"), 5s));

        done.wait();
        BOOST_TEST(!result.ec);
        BOOST_TEST_EQ(std::get<0>(result.values), "hello");
    }

    // Test: io_result<> completes before timeout
    void
    testVoidTaskCompletes()
    {
        thread_pool pool(1);
        std::latch done(1);
        io_result<> result{make_error_code(error::timeout)};

        run_async(pool.get_executor(),
            [&](io_result<> r) {
                result = r;
                done.count_down();
            },
            [&](std::exception_ptr) {
                done.count_down();
            })(timeout(returns_io_void(), 5s));

        done.wait();
        BOOST_TEST(!result.ec);
    }

    // Test: Timeout fires - io_result<size_t> path returns error::timeout
    void
    testTimeoutIoResult()
    {
        thread_pool pool(1);
        std::latch done(1);
        std::error_code ec;
        std::size_t n = 999;

        run_async(pool.get_executor(),
            [&](io_result<std::size_t> r) {
                ec = r.ec;
                n = std::get<0>(r.values);
                done.count_down();
            },
            [&](std::exception_ptr) {
                done.count_down();
            })(timeout(slow_io_result(100), 1ms));

        done.wait();
        BOOST_TEST(ec == error::timeout);
        BOOST_TEST(ec == cond::timeout);
        BOOST_TEST_EQ(n, 0u);
    }

    // Test: Timeout fires - io_result<int> reports error::timeout
    void
    testTimeoutReportsErrorForInt()
    {
        thread_pool pool(1);
        std::latch done(1);
        std::error_code ec;

        run_async(pool.get_executor(),
            [&](io_result<int> r) {
                ec = r.ec;
                done.count_down();
            },
            [&](std::exception_ptr) {
                done.count_down();
            })(timeout(slow_io_int(42), 1ms));

        done.wait();
        BOOST_TEST(ec == error::timeout);
    }

    // Test: Timeout fires - io_result<> reports error::timeout
    void
    testTimeoutReportsErrorForVoid()
    {
        thread_pool pool(1);
        std::latch done(1);
        std::error_code ec;

        run_async(pool.get_executor(),
            [&](io_result<> r) {
                ec = r.ec;
                done.count_down();
            },
            [&](std::exception_ptr) {
                done.count_down();
            })(timeout(slow_io_void(), 1ms));

        done.wait();
        BOOST_TEST(ec == error::timeout);
    }

    // Test: Zero duration times out immediately
    void
    testZeroDuration()
    {
        thread_pool pool(1);
        std::latch done(1);
        std::error_code ec;

        run_async(pool.get_executor(),
            [&](io_result<int> r) {
                ec = r.ec;
                done.count_down();
            },
            [&](std::exception_ptr) {
                done.count_down();
            })(timeout(slow_io_int(42), 0ms));

        done.wait();
        BOOST_TEST(ec == error::timeout);
    }

    // Test: cond::timeout equivalence
    void
    testCondEquivalence()
    {
        auto ec = make_error_code(error::timeout);
        BOOST_TEST(ec == cond::timeout);
        BOOST_TEST(!(ec == cond::canceled));
        BOOST_TEST(!(ec == cond::eof));

        auto cond_ec = make_error_condition(cond::timeout);
        BOOST_TEST(cond_ec.message() == "operation timed out");
    }

    // Inner task throws before delay fires.
    // Exception propagates to caller, not swallowed by timer.
    void
    testThrowPropagatesBeforeTimeout()
    {
        thread_pool pool(1);
        std::latch done(1);
        bool caught = false;
        std::string msg;

        run_async(pool.get_executor(),
            [&](io_result<int>) {
                done.count_down();
            },
            [&](std::exception_ptr ep) {
                try { std::rethrow_exception(ep); }
                catch (test_exception const& e) {
                    caught = true;
                    msg = e.what();
                }
                done.count_down();
            })(timeout(io_immediate_throw("boom"), 5s));

        done.wait();
        BOOST_TEST(caught);
        BOOST_TEST_EQ(msg, "boom");
    }

    void
    run()
    {
        testTaskCompletesBeforeTimeout();
        testTaskCompletesWithString();
        testVoidTaskCompletes();
        testTimeoutIoResult();
        testTimeoutReportsErrorForInt();
        testTimeoutReportsErrorForVoid();
        testZeroDuration();
        testCondEquivalence();
        testThrowPropagatesBeforeTimeout();
    }
};

TEST_SUITE(timeout_test, "capy.timeout");

} // capy
} // boost
