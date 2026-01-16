//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Test that header file is self-contained.
#include <boost/capy/test/fuse.hpp>

#include <boost/capy/error.hpp>
#include <boost/system/errc.hpp>

#include "test_suite.hpp"

namespace boost {
namespace capy {
namespace test {

class fuse_test
{
public:
    void
    testInlineUsage()
    {
        // Test fuse().check() inline usage
        int iterations = 0;
        int fail_points_hit = 0;

        bool ok = fuse().check([&](fuse& f) {
            ++iterations;

            auto ec = f.maybe_fail();
            if(ec.failed())
            {
                ++fail_points_hit;
                return;
            }

            ec = f.maybe_fail();
            if(ec.failed())
            {
                ++fail_points_hit;
                return;
            }

            ec = f.maybe_fail();
            if(ec.failed())
            {
                ++fail_points_hit;
                return;
            }
        });

        BOOST_TEST(ok);
        // Phase 1 (error codes): 5 iterations (n=0,1,2,3 trigger, n=4 completes)
        // Phase 2 (exceptions): 5 iterations
        BOOST_TEST(iterations == 10);
        // Error code phase: 4 triggers (n=0,1,2,3)
        BOOST_TEST(fail_points_hit == 4);
    }

    void
    testNamedUsage()
    {
        // Test fuse f; f.check() named usage
        fuse f;
        int iterations = 0;

        bool ok = f.check([&](fuse& fu) {
            ++iterations;
            auto ec = fu.maybe_fail();
            if(ec.failed())
                return;
        });

        BOOST_TEST(ok);
        // Phase 1: 3 iterations (n=0,1 trigger, n=2 completes)
        // Phase 2: 3 iterations
        BOOST_TEST(iterations == 6);
    }

    void
    testCustomErrorCode()
    {
        auto custom_ec = make_error_code(
            boost::system::errc::operation_canceled);

        system::error_code captured_ec;

        bool ok = fuse(custom_ec).check([&](fuse& f) {
            auto ec = f.maybe_fail();
            if(ec.failed())
            {
                captured_ec = ec;
                return;
            }
        });

        BOOST_TEST(ok);
        BOOST_TEST(captured_ec == custom_ec);
    }

    void
    testDefaultErrorCode()
    {
        system::error_code captured_ec;

        bool ok = fuse().check([&](fuse& f) {
            auto ec = f.maybe_fail();
            if(ec.failed())
            {
                captured_ec = ec;
                return;
            }
        });

        BOOST_TEST(ok);
        BOOST_TEST(captured_ec == error::test_failure);
    }

    void
    testBothPhases()
    {
        // Verify both error code and exception phases run
        int error_code_fails = 0;
        int exception_fails = 0;

        bool ok = fuse().check([&](fuse& f) {
            try
            {
                auto ec = f.maybe_fail();
                if(ec.failed())
                {
                    ++error_code_fails;
                    return;
                }

                ec = f.maybe_fail();
                if(ec.failed())
                {
                    ++error_code_fails;
                    return;
                }
            }
            catch(system::system_error const&)
            {
                ++exception_fails;
                throw;
            }
        });

        BOOST_TEST(ok);
        // 2 maybe_fail calls: n=0,1,2 trigger = 3 each
        BOOST_TEST(error_code_fails == 3);
        BOOST_TEST(exception_fails == 3);
    }

    void
    testFailStop()
    {
        // Test that fail_stop causes immediate return false
        int iterations = 0;

        bool ok = fuse().check([&](fuse& f) {
            ++iterations;
            if(iterations == 2)
            {
                f.fail_stop();
                return;
            }
            auto ec = f.maybe_fail();
            if(ec.failed())
                return;
        });

        BOOST_TEST(!ok);
        BOOST_TEST(iterations == 2);
    }

    void
    testStrayException()
    {
        // Test that stray exceptions cause return false
        bool ok = fuse().check([](fuse& f) {
            auto ec = f.maybe_fail();
            if(ec.failed())
                return;
            throw std::runtime_error("stray");
        });

        BOOST_TEST(!ok);
    }

    void
    testWrongExceptionCode()
    {
        // Test that wrong error code in exception causes return false
        auto expected_ec = make_error_code(error::test_failure);
        auto wrong_ec = make_error_code(
            boost::system::errc::operation_canceled);

        int iterations = 0;

        bool ok = fuse(expected_ec).check([&](fuse& f) {
            ++iterations;
            // In exception phase, throw wrong error code
            auto ec = f.maybe_fail();
            if(ec.failed())
                return;
            // After error code phase succeeds, we enter exception phase
            // Force a wrong exception to be thrown
            throw system::system_error(wrong_ec);
        });

        BOOST_TEST(!ok);
    }

    void
    testImmediateCompletion()
    {
        // Test that completes on first call (never calls maybe_fail)
        int iterations = 0;

        bool ok = fuse().check([&](fuse&) {
            ++iterations;
        });

        BOOST_TEST(ok);
        // Phase 1: 1 iteration, Phase 2: 1 iteration
        BOOST_TEST(iterations == 2);
    }

    void
    testSingleFailPoint()
    {
        int iterations = 0;
        int failures = 0;

        bool ok = fuse().check([&](fuse& f) {
            ++iterations;
            auto ec = f.maybe_fail();
            if(ec.failed())
            {
                ++failures;
                return;
            }
        });

        BOOST_TEST(ok);
        // Phase 1: 3 iterations (n=0,1 trigger, n=2 completes)
        // Phase 2: 3 iterations
        BOOST_TEST(iterations == 6);
        // Error code phase: 2 triggers (n=0,1)
        BOOST_TEST(failures == 2);
    }

    void
    testSharedState()
    {
        int call_count = 0;

        bool ok = fuse().check([&](fuse& f) {
            fuse f2 = f; // Copy shares state

            auto ec = f.maybe_fail();
            if(ec.failed())
                return;

            // f2 shares state with f, so this is the 2nd call
            ec = f2.maybe_fail();
            ++call_count;
            if(ec.failed())
                return;
        });

        BOOST_TEST(ok);
        // 2 maybe_fail calls with shared state:
        // Error mode: n=2,3 get past first maybe_fail = 2 increments
        // Exception mode: n=3 gets past first (n=2 throws on second) = 1 increment
        BOOST_TEST(call_count == 3);
    }

    void
    run()
    {
        testInlineUsage();
        testNamedUsage();
        testCustomErrorCode();
        testDefaultErrorCode();
        testBothPhases();
        testFailStop();
        testStrayException();
        testWrongExceptionCode();
        testImmediateCompletion();
        testSingleFailPoint();
        testSharedState();
    }
};

TEST_SUITE(fuse_test, "boost.capy.test.fuse");

} // test
} // capy
} // boost
