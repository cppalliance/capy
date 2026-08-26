//
// Copyright (c) 2026 Michael Vandeberg
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Reference examples for boost::capy::read_at_least, injected into its documentation by
// doc/addons/extensions/reference-snippets.lua. Declared in:
//   include/boost/capy/read_at_least.hpp
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
capy::task<> fill_buffer(capy::ReadStream auto& stream)
{
    std::vector<char> storage(4096);  // generous capacity
    // Require 16 header bytes; opportunistically take more.
    auto [ec, n] = co_await capy::read_at_least(
        stream, capy::make_buffer(storage), 16);
    if(ec)
        throw std::system_error(ec);

    // at least 16 bytes are available; n may be larger
}
// end::example[]
} // namespace ex_1

} // namespace
