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
#include <boost/capy/task.hpp>

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

// Returns an int after a brief delay (via stop_only_awaitable pattern)
inline task<int>
slow_int(int value)
{
    // Yield to allow stop to be observed
    co_await stop_only_awaitable{};
    co_return value;
}

// Returns io_result after a brief delay
inline io_task<std::size_t>
slow_io_result(std::size_t n)
{
    co_await stop_only_awaitable{};
    co_return io_result<std::size_t>{{}, n};
}

// Void task that waits for stop
inline task<void>
slow_void_task()
{
    co_await stop_only_awaitable{};
    co_return;
}

// Task that throws an exception immediately
inline task<int>
immediate_throw(char const* msg)
{
    throw test_exception(msg);
    co_return 0;
}

//----------------------------------------------------------
// Tests
//----------------------------------------------------------

struct timeout_test
{
    // Test: Task completes immediately, well within timeout
    void
    testTaskCompletesBeforeTimeout()
    {
        thread_pool pool(1);
        std::latch done(1);
        int result = 0;

        run_async(pool.get_executor(),
            [&](int v) {
                result = v;
                done.count_down();
            },
            [&](std::exception_ptr) {
                done.count_down();
            })(timeout(returns_int(42), 5s));

        done.wait();
        BOOST_TEST_EQ(result, 42);
    }

    // Test: Task completes immediately with string
    void
    testTaskCompletesWithString()
    {
        thread_pool pool(1);
        std::latch done(1);
        std::string result;

        run_async(pool.get_executor(),
            [&](std::string v) {
                result = std::move(v);
                done.count_down();
            },
            [&](std::exception_ptr) {
                done.count_down();
            })(timeout(returns_string("hello"), 5s));

        done.wait();
        BOOST_TEST_EQ(result, "hello");
    }

    // Test: Void task completes before timeout
    void
    testVoidTaskCompletes()
    {
        thread_pool pool(1);
        std::latch done(1);
        bool completed = false;

        run_async(pool.get_executor(),
            [&]() {
                completed = true;
                done.count_down();
            },
            [&](std::exception_ptr) {
                done.count_down();
            })(timeout(void_task(), 5s));

        done.wait();
        BOOST_TEST(completed);
    }

    // Test: Timeout fires - io_result path returns error::timeout
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
                n = r.t1;
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

    // Test: Timeout fires - non-io_result throws system_error
    void
    testTimeoutThrowsForNonIoResult()
    {
        thread_pool pool(1);
        std::latch done(1);
        bool got_timeout = false;

        run_async(pool.get_executor(),
            [&](int) {
                done.count_down();
            },
            [&](std::exception_ptr ep) {
                try
                {
                    std::rethrow_exception(ep);
                }
                catch(std::system_error const& e)
                {
                    got_timeout = (e.code() == error::timeout);
                }
                catch(...)
                {
                }
                done.count_down();
            })(timeout(slow_int(42), 1ms));

        done.wait();
        BOOST_TEST(got_timeout);
    }

    // Test: Timeout fires - void task throws system_error
    void
    testTimeoutThrowsForVoid()
    {
        thread_pool pool(1);
        std::latch done(1);
        bool got_timeout = false;

        run_async(pool.get_executor(),
            [&]() {
                done.count_down();
            },
            [&](std::exception_ptr ep) {
                try
                {
                    std::rethrow_exception(ep);
                }
                catch(std::system_error const& e)
                {
                    got_timeout = (e.code() == error::timeout);
                }
                catch(...)
                {
                }
                done.count_down();
            })(timeout(slow_void_task(), 1ms));

        done.wait();
        BOOST_TEST(got_timeout);
    }

    // Test: Task exception propagates (not converted to timeout)
    void
    testExceptionPropagates()
    {
        thread_pool pool(1);
        std::latch done(1);
        bool got_test_exception = false;

        run_async(pool.get_executor(),
            [&](int) {
                done.count_down();
            },
            [&](std::exception_ptr ep) {
                try
                {
                    std::rethrow_exception(ep);
                }
                catch(test_exception const&)
                {
                    got_test_exception = true;
                }
                catch(...)
                {
                }
                done.count_down();
            })(timeout(immediate_throw("test error"), 5s));

        done.wait();
        BOOST_TEST(got_test_exception);
    }

    // Test: Zero duration times out immediately
    void
    testZeroDuration()
    {
        thread_pool pool(1);
        std::latch done(1);
        bool got_timeout = false;

        run_async(pool.get_executor(),
            [&](int) {
                done.count_down();
            },
            [&](std::exception_ptr ep) {
                try
                {
                    std::rethrow_exception(ep);
                }
                catch(std::system_error const& e)
                {
                    got_timeout = (e.code() == error::timeout);
                }
                catch(...)
                {
                }
                done.count_down();
            })(timeout(slow_int(42), 0ms));

        done.wait();
        BOOST_TEST(got_timeout);
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

    void
    run()
    {
        testTaskCompletesBeforeTimeout();
        testTaskCompletesWithString();
        testVoidTaskCompletes();
        testTimeoutIoResult();
        testTimeoutThrowsForNonIoResult();
        testTimeoutThrowsForVoid();
        testExceptionPropagates();
        testZeroDuration();
        testCondEquivalence();
    }
};

TEST_SUITE(timeout_test, "capy.timeout");

} // capy
} // boost
