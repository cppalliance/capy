//
// Copyright (c) 2026 Michael Vandeberg
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Reference examples for boost::capy::test::fuse::armed, injected into its documentation by
// doc/addons/extensions/reference-snippets.lua. Declared in:
//   include/boost/capy/test/fuse.hpp
//
// The tagged regions are what the reference renders; the includes,
// suppressions and namespaces around them are scaffolding. Each region gets
// its own namespace so that examples which reuse a name still compile.

// Examples leave results unused; the reference explains them in prose.
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

#include <boost/capy.hpp>
#include <boost/capy/test.hpp>

#include "test_suite.hpp"

#include <cassert>
#include <chrono>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace capy = boost::capy;
using namespace boost::capy;
using namespace boost::capy::test;

namespace {
namespace ex_1 {
// tag::example_1[]
// This runner drives each iteration on a background thread via
// thread_pool, rather than run_blocking's single-threaded event
// loop -- demonstrating that armed() only needs a way to run the
// task to completion and report any exception, not any
// particular kind of executor. join() blocks until the posted
// task and its handlers have finished, so ep is safe to read
// once it returns.
std::exception_ptr run_one_iteration(task<> t)
{
    thread_pool pool(1);
    std::exception_ptr ep;
    run_async(pool.get_executor(),
        [](auto&&...) {},
        [&](std::exception_ptr e) { ep = e; }
    )(std::move(t));
    pool.join();
    return ep;
}

void armed_with_custom_runner()
{
    fuse f;
    auto r = f.armed(run_one_iteration,
        [](fuse& f) -> task<>
        {
            auto ec = f.maybe_fail();
            if(ec)
                co_return;
        });
}
// end::example_1[]
} // namespace ex_1

namespace ex_2 {
// tag::example_2[]
void armed_basic_two_points()
{
    fuse f;
    auto r = f.armed([](fuse& f) {
        auto ec = f.maybe_fail();
        if(ec)
            return;

        ec = f.maybe_fail();
        if(ec)
            return;
    });

    if(!r)
    {
        std::cerr << "Failed at " << r.loc.file_name()
            << ":" << r.loc.line() << "\n";
    }
}
// end::example_2[]
} // namespace ex_2

namespace ex_3 {
// tag::example_3[]
void armed_coroutine_two_points()
{
    fuse f;
    auto r = f.armed([&](fuse&) -> task<void> {
        auto ec = f.maybe_fail();
        if(ec)
            co_return;

        ec = f.maybe_fail();
        if(ec)
            co_return;
    });

    if(!r)
    {
        std::cerr << "Failed at " << r.loc.file_name()
            << ":" << r.loc.line() << "\n";
    }
}
// end::example_3[]
} // namespace ex_3

} // namespace
