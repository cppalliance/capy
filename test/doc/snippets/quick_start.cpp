//
// Copyright (c) 2026 Steve Gerbino
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Compiled fragments shown in pages/quick-start.adoc. Pages include the
// tagged regions; scaffolding stays outside the tags.

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
