//
// Copyright (c) 2026 Steve Gerbino
// Copyright (c) 2026 Michael Vandeberg
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Compiled fragments shown in pages/quick-start.adoc. Pages include the
// tagged regions; scaffolding stays outside the tags.

#include "../doc_warnings.hpp"

#include <boost/capy/task.hpp>
#include <boost/capy/ex/run_async.hpp>
#include <boost/capy/ex/thread_pool.hpp>

#include <exception>
#include <iostream>
#include <stdexcept>

#include "test_suite.hpp"

namespace capy = boost::capy;

namespace {

capy::task<int> answer()
{
    co_return 42;
}

capy::task<int> might_fail()
{
    bool fail = true;
    if (fail)
        throw std::runtime_error("might_fail");
    co_return 0;
}

struct quick_start_test
{
    void
    testHandlingResults()
    {
        capy::thread_pool pool(1);
        auto executor = pool.get_executor();
        // tag::handler[]
        capy::run_async(executor, [](int result) {
            std::cout << "Got: " << result << "\n";
        })(answer());
        // end::handler[]
        pool.join();
    }

    void
    testHandlingErrors()
    {
        capy::thread_pool pool(1);
        auto executor = pool.get_executor();
        // tag::errors[]
        capy::run_async(executor,
            [](int result) {
                std::cout << "Success: " << result << "\n";
            },
            [](std::exception_ptr ep) {
                try {
                    if (ep) std::rethrow_exception(ep);
                } catch (std::exception const& e) {
                    std::cerr << "Error: " << e.what() << "\n";
                }
            }
        )(might_fail());
        // end::errors[]
        pool.join();
    }

    void
    run()
    {
        testHandlingResults();
        testHandlingErrors();
    }
};

} // namespace

TEST_SUITE(quick_start_test, "boost.capy.doc.quick_start");
