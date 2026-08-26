//
// Copyright (c) 2026 Steve Gerbino
// Copyright (c) 2026 Michael Vandeberg
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Compiled fragments shown in pages/8.examples/8f.timeout-cancellation.adoc.

#include "../doc_warnings.hpp"

#include <boost/capy/task.hpp>
#include <boost/capy/ex/run_async.hpp>
#include <boost/capy/ex/thread_pool.hpp>

#include <stop_token>

#include "test_suite.hpp"

namespace capy = boost::capy;

namespace {

capy::task<> my_task()
{
    co_return;
}

struct timeout_cancellation_test
{
    void testTriggerStop()
    {
        capy::thread_pool pool(1);
        auto ex = pool.get_executor();
        // tag::trigger_stop[]
        std::stop_source source;
        capy::run_async(ex, source.get_token())(my_task());

        // Later:
        source.request_stop();
        // end::trigger_stop[]
        pool.join();
    }

    void run()
    {
        testTriggerStop();
    }
};

} // namespace

TEST_SUITE(timeout_cancellation_test, "boost.capy.doc.8f_timeout_cancellation");
