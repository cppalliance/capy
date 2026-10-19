//
// Copyright (c) 2026 Michael Vandeberg
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#include <boost/capy/ex/thread_pool.hpp>

#include <boost/capy/continuation.hpp>

#include "fault.hpp"
#include "test_suite.hpp"

#include <cerrno>
#include <coroutine>
#include <limits>
#include <system_error>

namespace boost {
namespace capy {

namespace {

using test::fault::fault_scope;
using test::fault::hook_is_live;
using test::fault::sys;

// An nth no test reaches: the arm only counts calls.
constexpr unsigned count_only = std::numeric_limits<unsigned>::max();

} // namespace

// A worker thread that fails to start surfaces as a throw from post(),
// and the next post() starts only the workers that are missing.
struct thread_pool_faults_test
{
    // The second worker fails to start, so the retry starts one.
    void
    testRetryStartsOnlyMissingThreads()
    {
        // Declared first: join() can stop the pool with c2 still
        // queued, and the destructor then abandons it.
        continuation c1, c2;
        c1.h = c2.h = std::noop_coroutine();
        thread_pool pool(2);

        {
            fault_scope fault(sys::pthread_create, EAGAIN, 2);
            BOOST_TEST_THROWS(
                pool.get_executor().post(c1), std::system_error);
            BOOST_TEST(fault.fired());
        }
        {
            fault_scope counter(sys::pthread_create, EAGAIN, count_only);
            pool.get_executor().post(c2);
            BOOST_TEST_EQ(counter.count(), 1u);
        }

        pool.join();
    }

    // The first worker fails to start, so the retry starts both.
    void
    testRetryAfterFirstThreadFails()
    {
        // Declared first: join() can stop the pool with c2 still
        // queued, and the destructor then abandons it.
        continuation c1, c2;
        c1.h = c2.h = std::noop_coroutine();
        thread_pool pool(2);

        {
            fault_scope fault(sys::pthread_create, EAGAIN);
            BOOST_TEST_THROWS(
                pool.get_executor().post(c1), std::system_error);
            BOOST_TEST(fault.fired());
        }
        {
            fault_scope counter(sys::pthread_create, EAGAIN, count_only);
            pool.get_executor().post(c2);
            BOOST_TEST_EQ(counter.count(), 2u);
        }

        pool.join();
    }

    void
    run()
    {
        if(!hook_is_live(sys::pthread_create))
        {
            test_suite::log << "pthread_create hook not live; skipping\n";
            return;
        }
        testRetryStartsOnlyMissingThreads();
        testRetryAfterFirstThreadFails();
    }
};

TEST_SUITE(
    thread_pool_faults_test,
    "boost.capy.fault.thread_pool");

} // capy
} // boost
