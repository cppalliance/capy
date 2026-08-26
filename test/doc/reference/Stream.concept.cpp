//
// Copyright (c) 2026 Michael Vandeberg
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Reference examples for boost::capy::Stream, injected into its documentation by
// doc/addons/extensions/reference-snippets.lua. Declared in:
//   include/boost/capy/concept/stream.hpp
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
template<capy::Stream S>
capy::task<> echo(S& stream)
{
    char buf[1024];
    auto [ec, n] = co_await stream.read_some(capy::make_buffer(buf));
    if(ec)
        co_return;

    // write_some may transfer fewer than n bytes (the partial-write
    // contract it inherits from WriteStream), so loop until every
    // byte read is written, or an error stops the loop early.
    std::size_t total = 0;
    while(total < n)
    {
        auto [ec2, n2] = co_await stream.write_some(
            capy::const_buffer(buf + total, n - total));
        total += n2;
        if(ec2)
            co_return;
    }
}
// end::example[]
} // namespace ex_1

} // namespace
