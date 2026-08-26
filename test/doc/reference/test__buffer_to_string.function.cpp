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
// tag::example[]
capy::task<>
buffer_to_string_examples()
{
    // Single buffer sequence
    capy::const_buffer cb( "hello", 5 );
    BOOST_TEST_EQ( capy::test::buffer_to_string( cb ), "hello" );

    // Multiple buffer sequences (concatenation)
    capy::const_buffer b1( "hello", 5 );
    capy::const_buffer b2( " world", 6 );
    BOOST_TEST_EQ( capy::test::buffer_to_string( b1, b2 ), "hello world" );

    // With bufgrind splits: each half is itself a buffer sequence,
    // so pass it directly -- there is no .data() to unwrap.
    capy::test::bufgrind bg( cb );
    while( bg ) {
        auto [h1, h2] = co_await bg.next();
        BOOST_TEST_EQ( capy::test::buffer_to_string( h1, h2 ), "hello" );
    }
}
// end::example[]
} // namespace ex_1

} // namespace
