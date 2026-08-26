//
// Copyright (c) 2026 Michael Vandeberg
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Reference examples for boost::capy::test::stream, injected into its documentation by
// doc/addons/extensions/reference-snippets.lua. Declared in:
//   include/boost/capy/test/stream.hpp
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
void
stream_pair_armed_demo()
{
    capy::test::fuse f;

    auto r = f.armed( [&]( capy::test::fuse& ) -> capy::task<> {
        // Constructed inside the lambda: armed() re-invokes this
        // function once per injected failure point, and a stream
        // pair constructed outside would carry buffered state
        // across those rounds.
        auto [a, b] = capy::test::make_stream_pair( f );

        auto [ec, n] = co_await a.write_some(
            capy::const_buffer( "hello", 5 ) );
        if( ec )
            co_return;

        char buf[32];
        auto [ec2, n2] = co_await b.read_some(
            capy::mutable_buffer( buf, sizeof( buf ) ) );
        if( ec2 )
            co_return;
        // buf contains "hello"
    } );
}
// end::example[]
} // namespace ex_1

} // namespace
