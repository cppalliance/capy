//
// Copyright (c) 2026 Michael Vandeberg
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Reference examples for boost::capy::read, injected into its documentation by
// doc/addons/extensions/reference-snippets.lua. Declared in:
//   include/boost/capy/read.hpp
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
capy::task<> process_message(capy::ReadStream auto& stream)
{
    std::vector<char> header(16);  // known header size for some protocol
    auto [ec, n] = co_await capy::read(stream, capy::make_buffer(header));
    if (ec == capy::cond::eof)
        co_return;  // Connection closed
    if (ec)
        throw std::system_error(ec);

    // at this point `header` contains exactly 16 bytes
}
// end::example[]
} // namespace ex_1

} // namespace
