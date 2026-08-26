//
// Copyright (c) 2026 Michael Vandeberg
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Reference examples for boost::capy::test::run_blocking, injected into its documentation by
// doc/addons/extensions/reference-snippets.lua. Declared in:
//   include/boost/capy/test/run_blocking.hpp
//
// The tagged regions are what the reference renders; the includes,
// suppressions and namespaces around them are scaffolding. Each region gets
// its own namespace so that examples which reuse a name still compile.

#include "../doc_warnings.hpp"

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

namespace {
namespace ex_1 {
// tag::example_1[]
capy::task<int>
compute_example_2()
{
    co_return 7;
}

void
run_blocking_two_handler_demo()
{
    int result = 0;
    capy::test::run_blocking(
        [&](int v) { result = v; },
        [](std::exception_ptr ep) { std::rethrow_exception( ep ); }
    )( compute_example_2() );
}
// end::example_1[]
} // namespace ex_1

namespace ex_2 {
// tag::example_2[]
capy::task<>
run_blocking_void_example()
{
    co_return;
}

void
run_blocking_no_handler_demo()
{
    capy::test::run_blocking()( run_blocking_void_example() );
}
// end::example_2[]
} // namespace ex_2

namespace ex_3 {
// tag::example_3[]
capy::task<int>
compute_example()
{
    co_return 42;
}

void
run_blocking_h1_demo()
{
    int result = 0;
    capy::test::run_blocking( [&](int v) { result = v; } )(
        compute_example() );
    BOOST_TEST_EQ( result, 42 );
}
// end::example_3[]
} // namespace ex_3

} // namespace
