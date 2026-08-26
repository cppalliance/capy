//
// Copyright (c) 2026 Michael Vandeberg
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Reference examples for boost::capy::cond, injected into its documentation by
// doc/addons/extensions/reference-snippets.lua. Declared in:
//   include/boost/capy/cond.hpp
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
// tag::example[]
capy::task<> classify_read_error( capy::any_read_stream& stream, capy::mutable_buffer bufs )
{
    auto [ec, n] = co_await stream.read_some( bufs );
    if( ec == capy::cond::canceled )
    {
        // handle cancellation
    }
    else if( ec == capy::cond::eof )
    {
        // handle end of stream
    }
    else if( ec )
    {
        // handle other errors
    }
}
// end::example[]
} // namespace ex_1

} // namespace
