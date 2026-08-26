//
// Copyright (c) 2026 Michael Vandeberg
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Reference examples for operator(), injected into its documentation by
// doc/addons/extensions/reference-snippets.lua. Declared in:
//   include/boost/capy/buffers.hpp
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
unsigned char header[16];
unsigned char payload[512];
std::array<capy::mutable_buffer, 2> bufs = {
    capy::mutable_buffer( header, sizeof(header) ),
    capy::mutable_buffer( payload, sizeof(payload) ) };
std::size_t total = capy::buffer_size( bufs );  // 16 + 512
// end::example[]
} // namespace ex_1

} // namespace
