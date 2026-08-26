//
// Copyright (c) 2026 Michael Vandeberg
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Reference examples for boost::capy::WriteStream, injected into its documentation by
// doc/addons/extensions/reference-snippets.lua. Declared in:
//   include/boost/capy/concept/write_stream.hpp
//
// The tagged regions are what the reference renders; the includes,
// suppressions and namespaces around them are scaffolding. Each region gets
// its own namespace so that examples which reuse a name still compile.

#include "../doc_warnings.hpp"

#include <boost/capy.hpp>

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
template< capy::ConstBufferSequence Buffers >
capy::IoAwaitable auto write_some( Buffers buffers );
// end::example_1[]
} // namespace ex_1

namespace ex_2 {
// tag::example_2[]
template< capy::WriteStream Stream >
capy::task<> write_all( Stream& s, char const* buf, std::size_t size )
{
    std::size_t total = 0;
    while( total < size )
    {
        auto [ec, n] = co_await s.write_some(
            capy::const_buffer( buf + total, size - total ) );
        total += n;
        if( ec )
            co_return;
    }
}
// end::example_2[]
} // namespace ex_2

} // namespace
