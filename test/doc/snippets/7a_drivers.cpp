//
// Copyright (c) 2026 Steve Gerbino
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Compiled fragments shown in pages/7.testing/7a.drivers.adoc.

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

using namespace boost::capy;
using namespace boost::capy::test;

task<int> compute(int x)
{
    co_return x * 2;
}

void test_compute()
{
    int result = 0;
    run_blocking([&](int v) { result = v; })(compute(21));
    BOOST_TEST(result == 42);
}
// end::run_blocking_basic[]

task<> my_task()
{
    co_return;
}

// tag::fuse_basic[]
#include <boost/capy/test/fuse.hpp>

using namespace boost::capy;
using namespace boost::capy::test;

void test_with_fuse()
{
    fuse f;
    auto r = f.armed([](fuse& f) {
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
task<void> correct_pattern(read_stream& rs, mutable_buffer buf)
{
    // tag::early_return[]
    // Correct: early return on injected error
    auto [ec, n] = co_await rs.read_some(buf);
    if(ec)
        co_return;  // fuse injected an error -- exit gracefully
    // end::early_return[]
}

task<void> wrong_pattern(read_stream& rs, mutable_buffer buf)
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

using namespace boost::capy;
using namespace boost::capy::test;

task<int> add(int a, int b)
{
    co_return a + b;
}

void test_add()
{
    fuse f;
    auto r = f.armed([&](fuse&) -> task<void> {
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
        run_blocking()(my_task());

        // Capture result; rethrow on exception
        int out = 0;
        run_blocking([&](int v) { out = v; })(compute(21));

        // Capture result; handle exception separately
        run_blocking(
            [&](int v) { out = v; },
            [](std::exception_ptr ep) { std::rethrow_exception(ep); }
        )(compute(21));

        // With a stop token (discards result)
        std::stop_source src;
        run_blocking(src.get_token())(my_task());

        // With a stop token and a result handler
        run_blocking(src.get_token(), [&](int v) { out = v; })(compute(21));

        // With a stop token and separate handlers
        run_blocking(
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

        run_blocking(src.get_token())([&]() -> task<>
        {
            read_stream rs;
            rs.provide("ignored");

            char buf[32];
            auto [ec, n] = co_await rs.read_some(make_buffer(buf));
            assert(ec == cond::canceled);   // honored the stop token
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
        fuse f;

        // Smoke test: happy path
        auto r1 = f.inert([&](fuse&) -> task<void> {
            read_stream rs(f);
            rs.provide("hello");

            char buf[8];
            auto [ec, n] = co_await rs.read_some(make_buffer(buf));
            BOOST_TEST(!ec);
            BOOST_TEST(std::string_view(buf, n) == "hello");
        });
        BOOST_TEST(r1.success);

        // Fault coverage: every error site
        auto r2 = f.armed([&](fuse&) -> task<void> {
            read_stream rs(f);
            rs.provide("hello");

            char buf[8];
            auto [ec, n] = co_await rs.read_some(make_buffer(buf));
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
        fuse f;
        auto r = f.inert([](fuse& f) {
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
        fuse f;
        auto r = f.armed([&](fuse&) -> task<void> {
            read_stream rs(f);
            rs.provide("data");
            char arr[8];
            co_await correct_pattern(rs, make_buffer(arr));
        });
        BOOST_TEST(r.success);

        // The wrong pattern asserts success, so only run it un-armed.
        fuse f2;
        auto r2 = f2.inert([&](fuse&) -> task<void> {
            read_stream rs(f2);
            rs.provide("data");
            char arr[8];
            co_await wrong_pattern(rs, make_buffer(arr));
        });
        BOOST_TEST(r2.success);
    }

    void
    testCoroutineSupport()
    {
        read_stream rs;
        rs.provide("data");
        char arr[8];
        auto buf = make_buffer(arr);
        // tag::armed_coroutine[]
        fuse f;
        auto r = f.armed([&](fuse&) -> task<void> {
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
            fuse& f_;
        public:
            explicit widget(fuse& f) : f_(f) {}

            std::error_code process()
            {
                auto ec = f_.maybe_fail();
                if(ec)
                    return ec;
                // ... actual work ...
                return {};
            }
        };

        fuse f;
        widget w(f);
        w.process();                                    // maybe_fail() returns {}

        auto r = f.armed([&](fuse&) { w.process(); });  // both branches exercised
        BOOST_TEST(r.success);
        // end::custom_fail_points[]
    }

    void
    testCustomErrorCode()
    {
        // tag::custom_error_code[]
        fuse f(std::make_error_code(std::errc::operation_canceled));
        auto r = f.armed([](fuse& f) {
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

        using namespace boost::capy;

        thread_pool pool(4);
        run_async(pool.get_executor())([]() -> task<void> {
            set_current_thread_name("test-worker-0");
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
