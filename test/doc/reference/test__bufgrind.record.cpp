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
void
bufgrind_walk_splits_demo()
{
    // Test all split points of a buffer
    std::string data = "hello world";
    auto cb = make_buffer( data );

    fuse f;
    auto r = f.inert( [&]( fuse& ) -> task<> {
        bufgrind bg( cb );
        while( bg ) {
            auto [b1, b2] = co_await bg.next();
            // b1 contains first N bytes (as a buffer sequence)
            // b2 contains remaining bytes (as a buffer sequence)
            // concatenating b1 + b2 equals original
            BOOST_TEST( buffer_to_string( b1, b2 ) == data );
        }
    } );
}
// end::example_1[]
} // namespace ex_1

namespace ex_2 {
// tag::example_2[]
// Mutable buffers preserve mutability
char data[100];
mutable_buffer buf( data, sizeof( data ) );

task<>
bufgrind_walk_mutable()
{
    bufgrind bg( buf );
    while( bg ) {
        auto [b1, b2] = co_await bg.next();
        // b1, b2 yield mutable_buffer when iterated
        static_assert( MutableBufferSequence<decltype(b1)> );
        static_assert( MutableBufferSequence<decltype(b2)> );
    }
}
// end::example_2[]
} // namespace ex_2

namespace ex_3 {
// tag::example_3[]
// Skip by 10 bytes for faster iteration
const_buffer bufgrind_step_data( "0123456789ABCDE", 15 );

task<>
bufgrind_walk_by_step()
{
    bufgrind bg( bufgrind_step_data, 10 );
    while( bg ) {
        auto [b1, b2] = co_await bg.next();
        // Visits positions 0, 10, 20, ..., and always size
    }
}
// end::example_3[]
} // namespace ex_3

} // namespace
