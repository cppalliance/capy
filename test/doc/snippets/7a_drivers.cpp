//
// Copyright (c) 2026 Steve Gerbino
// Copyright (c) 2026 Michael Vandeberg
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Compiled fragments shown in pages/7.testing/7a.drivers.adoc.

#include "../doc_warnings.hpp"

#include <boost/capy/buffers/make_buffer.hpp>
#include <boost/capy/cond.hpp>
#include <boost/capy/ex/run_async.hpp>
#include <boost/capy/ex/thread_pool.hpp>
#include <boost/capy/task.hpp>
#include <boost/capy/test/fuse.hpp>
#include <boost/capy/test/read_stream.hpp>
#include <boost/capy/test/run_blocking.hpp>
#include <boost/capy/test/thread_name.hpp>

#include <cassert>
#include <exception>
#include <stop_token>
#include <string_view>
#include <system_error>

#include "test_suite.hpp"

namespace {

// The #include directives inside the tags expand to nothing here (the
// headers are already included above); they are kept for the page text.

// tag::run_blocking_basic[]
#include <boost/capy/task.hpp>
#include <boost/capy/test/run_blocking.hpp>

namespace capy = boost::capy;

capy::task<int> compute(int x)
{
    co_return x * 2;
}

void test_compute()
{
    int result = 0;
    capy::test::run_blocking([&](int v) { result = v; })(compute(21));
    BOOST_TEST(result == 42);
}
// end::run_blocking_basic[]

capy::task<> my_task()
{
    co_return;
}

// tag::fuse_basic[]
#include <boost/capy/test/fuse.hpp>

namespace capy = boost::capy;

void test_with_fuse()
{
    capy::test::fuse f;
    auto r = f.armed([](capy::test::fuse& f) {
        auto ec = f.maybe_fail();
        if(ec)
            return;  // injected error: exit gracefully

        ec = f.maybe_fail();
        if(ec)
            return;
    });
    BOOST_TEST(r.success);
}
// end::fuse_basic[]

// The two patterns redeclare [ec, n], so each needs its own scope.
capy::task<void> correct_pattern(
    capy::test::read_stream& rs, capy::mutable_buffer buf)
{
    // tag::early_return[]
    // Correct: early return on injected error
    auto [ec, n] = co_await rs.read_some(buf);
    if(ec)
        co_return;  // fuse injected an error -- exit gracefully
    // end::early_return[]
}

capy::task<void> wrong_pattern(
    capy::test::read_stream& rs, capy::mutable_buffer buf)
{
    // tag::early_return[]

    // Wrong: asserting success unconditionally
    auto [ec, n] = co_await rs.read_some(buf);
    BOOST_TEST(!ec);  // fails when fuse injects an error
    // end::early_return[]
}

// tag::canonical_skeleton[]
#include <boost/capy/task.hpp>
#include <boost/capy/test/fuse.hpp>

namespace capy = boost::capy;

capy::task<int> add(int a, int b)
{
    co_return a + b;
}

void test_add()
{
    capy::test::fuse f;
    auto r = f.armed([&](capy::test::fuse&) -> capy::task<void> {
        auto sum = co_await add(3, 4);
        BOOST_TEST(sum == 7);
    });
    BOOST_TEST(r.success);
}
// end::canonical_skeleton[]

struct drivers_test
{
    void
    testRunBlockingBasic()
    {
        test_compute();
    }

    void
    testRunBlockingOverloads()
    {
        // tag::run_blocking_overloads[]
        // Discard result; rethrow on exception
        capy::test::run_blocking()(my_task());

        // Capture result; rethrow on exception
        int out = 0;
        capy::test::run_blocking([&](int v) { out = v; })(compute(21));

        // Capture result; handle exception separately
        capy::test::run_blocking(
            [&](int v) { out = v; },
            [](std::exception_ptr ep) { std::rethrow_exception(ep); }
        )(compute(21));

        // With a stop token (discards result)
        std::stop_source src;
        capy::test::run_blocking(src.get_token())(my_task());

        // With a stop token and a result handler
        capy::test::run_blocking(
            src.get_token(), [&](int v) { out = v; })(compute(21));

        // With a stop token and separate handlers
        capy::test::run_blocking(
            src.get_token(),
            [&](int v) { out = v; },
            [](std::exception_ptr ep) { std::rethrow_exception(ep); }
        )(compute(21));
        // end::run_blocking_overloads[]
        BOOST_TEST(out == 42);
    }

    void
    testExercisingCancellation()
    {
        // tag::run_blocking_cancellation[]
        std::stop_source src;
        src.request_stop();

        capy::test::run_blocking(src.get_token())([&]() -> capy::task<>
        {
            capy::test::read_stream rs;
            rs.provide("ignored");

            char buf[32];
            auto [ec, n] = co_await rs.read_some(capy::make_buffer(buf));
            assert(ec == capy::cond::canceled);   // honored the stop token
        }());
        // end::run_blocking_cancellation[]
    }

    void
    testFuseBasic()
    {
        test_with_fuse();
    }

    void
    testInertVsArmed()
    {
        // tag::inert_vs_armed[]
        capy::test::fuse f;

        // Smoke test: happy path
        auto r1 = f.inert([&](capy::test::fuse&) -> capy::task<void> {
            capy::test::read_stream rs(f);
            rs.provide("hello");

            char buf[8];
            auto [ec, n] = co_await rs.read_some(capy::make_buffer(buf));
            BOOST_TEST(!ec);
            BOOST_TEST(std::string_view(buf, n) == "hello");
        });
        BOOST_TEST(r1.success);

        // Fault coverage: every error site
        auto r2 = f.armed([&](capy::test::fuse&) -> capy::task<void> {
            capy::test::read_stream rs(f);
            rs.provide("hello");

            char buf[8];
            auto [ec, n] = co_await rs.read_some(capy::make_buffer(buf));
            if(ec)
                co_return;  // fuse injected an error; exit gracefully
            BOOST_TEST(std::string_view(buf, n) == "hello");
        });
        BOOST_TEST(r2.success);
        // end::inert_vs_armed[]
    }

    void
    testInertFail()
    {
        // A const bool with a constant initializer is readable inside
        // the capture-less lambda the page shows.
        bool const some_condition_failed = false;
        // tag::inert_fail[]
        capy::test::fuse f;
        auto r = f.inert([](capy::test::fuse& f) {
            auto ec = f.maybe_fail();  // always returns {}
            assert(!ec);

            if(some_condition_failed)
                f.fail();  // the only way to signal failure in inert mode
        });
        BOOST_TEST(r.success);
        // end::inert_fail[]
    }

    void
    testEarlyReturn()
    {
        capy::test::fuse f;
        auto r = f.armed([&](capy::test::fuse&) -> capy::task<void> {
            capy::test::read_stream rs(f);
            rs.provide("data");
            char arr[8];
            co_await correct_pattern(rs, capy::make_buffer(arr));
        });
        BOOST_TEST(r.success);

        // The wrong pattern asserts success, so only run it un-armed.
        capy::test::fuse f2;
        auto r2 = f2.inert([&](capy::test::fuse&) -> capy::task<void> {
            capy::test::read_stream rs(f2);
            rs.provide("data");
            char arr[8];
            co_await wrong_pattern(rs, capy::make_buffer(arr));
        });
        BOOST_TEST(r2.success);
    }

    void
    testCoroutineSupport()
    {
        capy::test::read_stream rs;
        rs.provide("data");
        char arr[8];
        auto buf = capy::make_buffer(arr);
        // tag::armed_coroutine[]
        capy::test::fuse f;
        auto r = f.armed([&](capy::test::fuse&) -> capy::task<void> {
            auto ec = f.maybe_fail();
            if(ec)
                co_return;

            auto [ec2, n] = co_await rs.read_some(buf);
            if(ec2)
                co_return;
        });
        BOOST_TEST(r.success);
        // end::armed_coroutine[]
    }

    void
    testCustomFailPoints()
    {
        // tag::custom_fail_points[]
        class widget
        {
            capy::test::fuse& f_;
        public:
            explicit widget(capy::test::fuse& f) : f_(f) {}

            std::error_code process()
            {
                auto ec = f_.maybe_fail();
                if(ec)
                    return ec;
                // ... actual work ...
                return {};
            }
        };

        capy::test::fuse f;
        widget w(f);
        w.process();                                    // maybe_fail() returns {}

        // both branches exercised
        auto r = f.armed([&](capy::test::fuse&) { w.process(); });
        BOOST_TEST(r.success);
        // end::custom_fail_points[]
    }

    void
    testCustomErrorCode()
    {
        // tag::custom_error_code[]
        capy::test::fuse f(
            std::make_error_code(std::errc::operation_canceled));
        auto r = f.armed([](capy::test::fuse& f) {
            auto ec = f.maybe_fail();
            if(ec)
            {
                assert(ec == std::errc::operation_canceled);
                return;
            }
        });
        BOOST_TEST(r.success);
        // end::custom_error_code[]
    }

    void
    testThreadName()
    {
        // tag::thread_name[]
        #include <boost/capy/ex/run_async.hpp>
        #include <boost/capy/ex/thread_pool.hpp>
        #include <boost/capy/task.hpp>
        #include <boost/capy/test/thread_name.hpp>

        namespace capy = boost::capy;

        capy::thread_pool pool(4);
        capy::run_async(pool.get_executor())([]() -> capy::task<void> {
            capy::set_current_thread_name("test-worker-0");
            // ... test work runs here; name appears in gdb thread list
            co_return;
        }());
        pool.join();
        // end::thread_name[]
    }

    void
    testCanonicalSkeleton()
    {
        test_add();
    }

    void
    run()
    {
        testRunBlockingBasic();
        testRunBlockingOverloads();
        testExercisingCancellation();
        testFuseBasic();
        testInertVsArmed();
        testInertFail();
        testEarlyReturn();
        testCoroutineSupport();
        testCustomFailPoints();
        testCustomErrorCode();
        testThreadName();
        testCanonicalSkeleton();
    }
};

} // namespace

TEST_SUITE(drivers_test, "boost.capy.doc.7a_drivers");
