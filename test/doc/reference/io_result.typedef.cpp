//
// Copyright (c) 2026 Michael Vandeberg
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Reference examples for boost::capy::io_result, injected into its documentation by
// doc/addons/extensions/reference-snippets.lua. Declared in:
//   include/boost/capy/io_result.hpp
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
capy::task<> discard_first_chunk( capy::any_read_stream& s, capy::mutable_buffer buf )
{
    auto [ec, n] = co_await s.read_some(buf);
    if (ec)
        co_return;  // error: n's meaning here is defined by read_some
}
// end::example_1[]
} // namespace ex_1

namespace ex_2 {
// tag::example_2[]
capy::task<> read_into_locals( capy::any_read_stream& s, capy::mutable_buffer buf )
{
    std::error_code ec;
    std::size_t n = 0;
    std::tie(ec, n) = co_await s.read_some(buf);
}
// end::example_2[]
} // namespace ex_2

} // namespace
