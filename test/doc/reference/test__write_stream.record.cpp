//
// Copyright (c) 2026 Michael Vandeberg
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Reference examples for boost::capy::test::write_stream, injected into its documentation by
// doc/addons/extensions/reference-snippets.lua. Declared in:
//   include/boost/capy/test/write_stream.hpp
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
write_stream_armed_demo()
{
    capy::test::fuse f;

    auto r = f.armed( [&]( capy::test::fuse& ) -> capy::task<void> {
        // Constructed inside the lambda: armed() re-invokes this
        // function once per injected failure point, and a
        // write_stream constructed outside would carry
        // accumulated data across those rounds.
        capy::test::write_stream ws( f );

        auto [ec, n] = co_await ws.write_some(
            capy::const_buffer( "Hello", 5 ) );
        if( ec )
            co_return;
        // ws.data() returns "Hello"
    } );
}
// end::example[]
} // namespace ex_1

} // namespace
