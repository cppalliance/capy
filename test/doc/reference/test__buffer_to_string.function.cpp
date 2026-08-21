//
// Copyright (c) 2026 Michael Vandeberg
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Reference examples for boost::capy::test::buffer_to_string, injected into its documentation by
// doc/addons/extensions/reference-snippets.lua. Declared in:
//   include/boost/capy/test/buffer_to_string.hpp
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
// tag::example[]
task<>
buffer_to_string_examples()
{
    // Single buffer sequence
    const_buffer cb( "hello", 5 );
    BOOST_TEST_EQ( buffer_to_string( cb ), "hello" );

    // Multiple buffer sequences (concatenation)
    const_buffer b1( "hello", 5 );
    const_buffer b2( " world", 6 );
    BOOST_TEST_EQ( buffer_to_string( b1, b2 ), "hello world" );

    // With bufgrind splits: each half is itself a buffer sequence,
    // so pass it directly -- there is no .data() to unwrap.
    bufgrind bg( cb );
    while( bg ) {
        auto [h1, h2] = co_await bg.next();
        BOOST_TEST_EQ( buffer_to_string( h1, h2 ), "hello" );
    }
}
// end::example[]
} // namespace ex_1

} // namespace
