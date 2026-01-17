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
#include <stdexcept>

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
        // Test fuse()(...) inline usage with operator()
        int iterations = 0;
        int fail_points_hit = 0;

        auto r = fuse()([&](fuse& f) {
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

        BOOST_TEST(r.success);
        // Phase 1 (error codes): 5 iterations (n=0,1,2,3 trigger, n=4 completes)
        // Phase 2 (exceptions): 5 iterations
        BOOST_TEST(iterations == 10);
        // Error code phase: 4 triggers (n=0,1,2,3)
        BOOST_TEST(fail_points_hit == 4);
    }

    void
    testNamedUsage()
    {
        // Test fuse f; f.armed() named usage
        fuse f;
        int iterations = 0;

        auto r = f.armed([&](fuse& fu) {
            ++iterations;
            auto ec = fu.maybe_fail();
            if(ec.failed())
                return;
        });

        BOOST_TEST(r.success);
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

        auto r = fuse(custom_ec)([&](fuse& f) {
            auto ec = f.maybe_fail();
            if(ec.failed())
            {
                captured_ec = ec;
                return;
            }
        });

        BOOST_TEST(r.success);
        BOOST_TEST(captured_ec == custom_ec);
    }

    void
    testDefaultErrorCode()
    {
        system::error_code captured_ec;

        auto r = fuse()([&](fuse& f) {
            auto ec = f.maybe_fail();
            if(ec.failed())
            {
                captured_ec = ec;
                return;
            }
        });

        BOOST_TEST(r.success);
        BOOST_TEST(captured_ec == error::test_failure);
    }

    void
    testBothPhases()
    {
        // Verify both error code and exception phases run
        int error_code_fails = 0;
        int exception_fails = 0;

        auto r = fuse()([&](fuse& f) {
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

        BOOST_TEST(r.success);
        // 2 maybe_fail calls: n=0,1,2 trigger = 3 each
        BOOST_TEST(error_code_fails == 3);
        BOOST_TEST(exception_fails == 3);
    }

    void
    testFail()
    {
        // Test that fail() causes immediate return with failed result
        int iterations = 0;

        auto r = fuse()([&](fuse& f) {
            ++iterations;
            if(iterations == 2)
            {
                f.fail();
                return;
            }
            auto ec = f.maybe_fail();
            if(ec.failed())
                return;
        });

        BOOST_TEST(!r.success);
        BOOST_TEST(iterations == 2);
    }

    void
    testStrayException()
    {
        // Test that stray exceptions cause failed result
        auto r = fuse()([](fuse& f) {
            auto ec = f.maybe_fail();
            if(ec.failed())
                return;
            throw std::runtime_error("stray");
        });

        BOOST_TEST(!r.success);
    }

    void
    testWrongExceptionCode()
    {
        // Test that wrong error code in exception causes failed result
        auto expected_ec = make_error_code(error::test_failure);
        auto wrong_ec = make_error_code(
            boost::system::errc::operation_canceled);

        int iterations = 0;

        auto r = fuse(expected_ec)([&](fuse& f) {
            ++iterations;
            // In exception phase, throw wrong error code
            auto ec = f.maybe_fail();
            if(ec.failed())
                return;
            // After error code phase succeeds, we enter exception phase
            // Force a wrong exception to be thrown
            throw system::system_error(wrong_ec);
        });

        BOOST_TEST(!r.success);
    }

    void
    testImmediateCompletion()
    {
        // Test that completes on first call (never calls maybe_fail)
        int iterations = 0;

        auto r = fuse()([&](fuse&) {
            ++iterations;
        });

        BOOST_TEST(r.success);
        // Phase 1: 1 iteration, Phase 2: 1 iteration
        BOOST_TEST(iterations == 2);
    }

    void
    testSingleFailPoint()
    {
        int iterations = 0;
        int failures = 0;

        auto r = fuse()([&](fuse& f) {
            ++iterations;
            auto ec = f.maybe_fail();
            if(ec.failed())
            {
                ++failures;
                return;
            }
        });

        BOOST_TEST(r.success);
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

        auto r = fuse()([&](fuse& f) {
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

        BOOST_TEST(r.success);
        // 2 maybe_fail calls with shared state:
        // Error mode: n=2,3 get past first maybe_fail = 2 increments
        // Exception mode: n=3 gets past first (n=2 throws on second) = 1 increment
        BOOST_TEST(call_count == 3);
    }

    void
    testResultBoolConversion()
    {
        // Test that result converts to bool
        fuse f;
        auto r = f([](fuse& fu) {
            auto ec = fu.maybe_fail();
            if(ec.failed())
                return;
        });

        // Test explicit bool conversion
        if(r)
            BOOST_TEST(r.success);
        else
            BOOST_TEST(!r.success);

        BOOST_TEST(static_cast<bool>(r) == r.success);
    }

    void
    testSourceLocationOnMaybeFail()
    {
        // Test that source location is captured on maybe_fail
        fuse f;
        auto r = f([](fuse& fu) {
            auto ec = fu.maybe_fail();
            if(ec.failed())
                return;
            // Force a stray exception to get a failed result
            throw std::runtime_error("test");
        });

        BOOST_TEST(!r.success);
        // Verify location was captured (file should contain "fuse.cpp")
        BOOST_TEST(r.loc.line() > 0);
    }

    void
    testSourceLocationOnFail()
    {
        // Test that source location is captured on fail()
        fuse f;
        int line_of_fail = 0;

        auto r = f([&](fuse& fu) {
            auto ec = fu.maybe_fail();
            if(ec.failed())
                return;
            line_of_fail = __LINE__ + 1;
            fu.fail();
        });

        BOOST_TEST(!r.success);
        BOOST_TEST(r.loc.line() == line_of_fail);
    }

    void
    testFailWithExceptionPtr()
    {
        // Test that fail(exception_ptr) captures the exception
        fuse f;

        auto r = f([](fuse& fu) {
            auto ec = fu.maybe_fail();
            if(ec.failed())
                return;
            try
            {
                throw std::runtime_error("test exception");
            }
            catch(...)
            {
                fu.fail(std::current_exception());
                return;
            }
        });

        BOOST_TEST(!r.success);
        BOOST_TEST(r.ep != nullptr);

        // Verify we can rethrow and inspect
        bool caught = false;
        try
        {
            std::rethrow_exception(r.ep);
        }
        catch(std::runtime_error const& e)
        {
            caught = true;
            BOOST_TEST(std::string(e.what()) == "test exception");
        }
        BOOST_TEST(caught);
    }

    void
    testOperatorCall()
    {
        // Test that operator() is equivalent to armed()
        fuse f1;
        fuse f2;
        int iterations1 = 0;
        int iterations2 = 0;

        auto r1 = f1.armed([&](fuse& f) {
            ++iterations1;
            auto ec = f.maybe_fail();
            if(ec.failed())
                return;
        });

        auto r2 = f2([&](fuse& f) {
            ++iterations2;
            auto ec = f.maybe_fail();
            if(ec.failed())
                return;
        });

        BOOST_TEST(r1.success);
        BOOST_TEST(r2.success);
        BOOST_TEST(iterations1 == iterations2);
    }

    void
    testInertNeverTriggers()
    {
        // Test that inert() mode never triggers maybe_fail
        fuse f;
        int maybe_fail_calls = 0;
        int fail_count = 0;

        auto r = f.inert([&](fuse& fu) {
            for(int i = 0; i < 10; ++i)
            {
                ++maybe_fail_calls;
                auto ec = fu.maybe_fail();
                if(ec.failed())
                    ++fail_count;
            }
        });

        BOOST_TEST(r.success);
        BOOST_TEST(maybe_fail_calls == 10);
        BOOST_TEST(fail_count == 0);
    }

    void
    testInertFailStillWorks()
    {
        // Test that fail() works in inert mode
        fuse f;
        int line_of_fail = 0;

        auto r = f.inert([&](fuse& fu) {
            auto ec = fu.maybe_fail();
            BOOST_TEST(!ec.failed());

            line_of_fail = __LINE__ + 1;
            fu.fail();
        });

        BOOST_TEST(!r.success);
        BOOST_TEST(r.loc.line() == line_of_fail);
    }

    void
    testInertRunsOnce()
    {
        // Test that inert() runs exactly once
        fuse f;
        int iterations = 0;

        auto r = f.inert([&](fuse& fu) {
            ++iterations;
            auto ec = fu.maybe_fail();
            (void)ec;
        });

        BOOST_TEST(r.success);
        BOOST_TEST(iterations == 1);
    }

    void
    testInertWithException()
    {
        // Test that exceptions in inert mode cause failure
        fuse f;

        auto r = f.inert([](fuse&) {
            throw std::runtime_error("test exception");
        });

        BOOST_TEST(!r.success);
        BOOST_TEST(r.ep != nullptr);

        bool caught = false;
        try
        {
            std::rethrow_exception(r.ep);
        }
        catch(std::runtime_error const& e)
        {
            caught = true;
            BOOST_TEST(std::string(e.what()) == "test exception");
        }
        BOOST_TEST(caught);
    }

    void
    testInertFailWithExceptionPtr()
    {
        // Test that fail(exception_ptr) works in inert mode
        fuse f;

        auto r = f.inert([](fuse& fu) {
            try
            {
                throw std::runtime_error("captured exception");
            }
            catch(...)
            {
                fu.fail(std::current_exception());
                return;
            }
        });

        BOOST_TEST(!r.success);
        BOOST_TEST(r.ep != nullptr);

        bool caught = false;
        try
        {
            std::rethrow_exception(r.ep);
        }
        catch(std::runtime_error const& e)
        {
            caught = true;
            BOOST_TEST(std::string(e.what()) == "captured exception");
        }
        BOOST_TEST(caught);
    }

    void
    testInertInlineUsage()
    {
        // Test fuse().inert() inline usage
        int iterations = 0;

        auto r = fuse().inert([&](fuse& f) {
            ++iterations;
            auto ec = f.maybe_fail();
            BOOST_TEST(!ec.failed());
        });

        BOOST_TEST(r.success);
        BOOST_TEST(iterations == 1);
    }

    void
    testStandaloneMaybeFailIsNoOp()
    {
        // Test that maybe_fail() returns {} outside armed/inert
        fuse f;
        int fail_count = 0;

        for(int i = 0; i < 10; ++i)
        {
            auto ec = f.maybe_fail();
            if(ec.failed())
                ++fail_count;
        }

        BOOST_TEST(fail_count == 0);
    }

    void
    testStandaloneAfterArmed()
    {
        // Test that fuse returns to no-op after armed() completes
        fuse f;

        auto r = f.armed([](fuse& fu) {
            auto ec = fu.maybe_fail();
            if(ec.failed())
                return;
        });

        BOOST_TEST(r.success);

        // After armed(), should be back to no-op
        int fail_count = 0;
        for(int i = 0; i < 10; ++i)
        {
            auto ec = f.maybe_fail();
            if(ec.failed())
                ++fail_count;
        }

        BOOST_TEST(fail_count == 0);
    }

    void
    testStandaloneAfterInert()
    {
        // Test that fuse returns to no-op after inert() completes
        fuse f;

        auto r = f.inert([](fuse& fu) {
            auto ec = fu.maybe_fail();
            (void)ec;
        });

        BOOST_TEST(r.success);

        // After inert(), should still be no-op
        int fail_count = 0;
        for(int i = 0; i < 10; ++i)
        {
            auto ec = f.maybe_fail();
            if(ec.failed())
                ++fail_count;
        }

        BOOST_TEST(fail_count == 0);
    }

    void
    testDependencyInjectionPattern()
    {
        // Simulate a class that uses fuse for dependency injection
        struct Service
        {
            fuse& f_;
            int work_count = 0;

            explicit Service(fuse& f) : f_(f) {}

            system::error_code do_work()
            {
                auto ec = f_.maybe_fail();
                if(ec.failed())
                    return ec;
                ++work_count;
                return {};
            }
        };

        fuse f;
        Service svc(f);

        // Production usage - fuse is no-op
        for(int i = 0; i < 5; ++i)
        {
            auto ec = svc.do_work();
            BOOST_TEST(!ec.failed());
        }
        BOOST_TEST(svc.work_count == 5);

        // Test usage - failures are injected
        svc.work_count = 0;
        int iterations = 0;

        auto r = f.armed([&](fuse&) {
            ++iterations;
            auto ec = svc.do_work();
            if(ec.failed())
                return;
        });

        BOOST_TEST(r.success);
        // armed() runs multiple iterations testing failure paths
        BOOST_TEST(iterations > 1);

        // After armed(), back to no-op
        svc.work_count = 0;
        for(int i = 0; i < 5; ++i)
        {
            auto ec = svc.do_work();
            BOOST_TEST(!ec.failed());
        }
        BOOST_TEST(svc.work_count == 5);
    }

    void
    run()
    {
        testInlineUsage();
        testNamedUsage();
        testCustomErrorCode();
        testDefaultErrorCode();
        testBothPhases();
        testFail();
        testStrayException();
        testWrongExceptionCode();
        testImmediateCompletion();
        testSingleFailPoint();
        testSharedState();
        testResultBoolConversion();
        testSourceLocationOnMaybeFail();
        testSourceLocationOnFail();
        testFailWithExceptionPtr();
        testOperatorCall();
        testInertNeverTriggers();
        testInertFailStillWorks();
        testInertRunsOnce();
        testInertWithException();
        testInertFailWithExceptionPtr();
        testInertInlineUsage();
        testStandaloneMaybeFailIsNoOp();
        testStandaloneAfterArmed();
        testStandaloneAfterInert();
        testDependencyInjectionPattern();
    }
};

TEST_SUITE(fuse_test, "boost.capy.test.fuse");

} // test
} // capy
} // boost
