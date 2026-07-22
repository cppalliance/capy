//
// Copyright (c) 2026 Steve Gerbino
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Compiled fragments shown in pages/3.concurrency/3a.foundations.adoc.

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

#include <atomic>
#include <thread>

#include "test_suite.hpp"

namespace {

std::atomic<bool> work_done{false};

void do_work()
{
    work_done = true;
}

std::atomic<bool> background_finished{false};

// Signals completion so the test can wait for the detached thread
void background_task()
{
    background_finished = true;
}

void some_function()
{
}

struct foundations_test
{
    void
    testJoin()
    {
        // tag::join[]
        std::thread t(do_work);
        // ... do other things ...
        t.join();  // wait for do_work to finish
        // end::join[]
        BOOST_TEST(work_done);
    }

    void
    testDetach()
    {
        // tag::detach[]
        std::thread t(background_task);
        t.detach();  // thread runs independently
        // t is now "empty"—no longer associated with a thread
        // end::detach[]
        BOOST_TEST(!t.joinable());
        // The detached thread must not outlive the test binary
        while (!background_finished)
            std::this_thread::yield();
    }

    void
    testJoinable()
    {
        // tag::joinable[]
        std::thread t(some_function);

        if (t.joinable())
        {
            t.join();
        }
        // end::joinable[]
        BOOST_TEST(!t.joinable());
    }

    void
    run()
    {
        testJoin();
        testDetach();
        testJoinable();
    }
};

} // namespace

TEST_SUITE(foundations_test, "boost.capy.doc.3a_foundations");
