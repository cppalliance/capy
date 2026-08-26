//
// Copyright (c) 2026 Michael Vandeberg
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Reference examples for boost::capy::test::bufgrind, injected into its documentation by
// doc/addons/extensions/reference-snippets.lua. Declared in:
//   include/boost/capy/test/bufgrind.hpp
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
void
bufgrind_walk_splits_demo()
{
    // Test all split points of a buffer
    std::string data = "hello world";
    auto cb = capy::make_buffer( data );

    capy::test::fuse f;
    auto r = f.inert( [&]( capy::test::fuse& ) -> capy::task<> {
        capy::test::bufgrind bg( cb );
        while( bg ) {
            auto [b1, b2] = co_await bg.next();
            // b1 contains first N bytes (as a buffer sequence)
            // b2 contains remaining bytes (as a buffer sequence)
            // concatenating b1 + b2 equals original
            BOOST_TEST( capy::test::buffer_to_string( b1, b2 ) == data );
        }
    } );
}
// end::example_1[]
} // namespace ex_1

namespace ex_2 {
// tag::example_2[]
// Mutable buffers preserve mutability
char data[100];
capy::mutable_buffer buf( data, sizeof( data ) );

capy::task<>
bufgrind_walk_mutable()
{
    capy::test::bufgrind bg( buf );
    while( bg ) {
        auto [b1, b2] = co_await bg.next();
        // b1, b2 yield mutable_buffer when iterated
        static_assert( capy::MutableBufferSequence<decltype(b1)> );
        static_assert( capy::MutableBufferSequence<decltype(b2)> );
    }
}
// end::example_2[]
} // namespace ex_2

namespace ex_3 {
// tag::example_3[]
// Skip by 10 bytes for faster iteration
capy::const_buffer bufgrind_step_data( "0123456789ABCDE", 15 );

capy::task<>
bufgrind_walk_by_step()
{
    capy::test::bufgrind bg( bufgrind_step_data, 10 );
    while( bg ) {
        auto [b1, b2] = co_await bg.next();
        // Visits positions 0, 10, 20, ..., and always size
    }
}
// end::example_3[]
} // namespace ex_3

} // namespace
