//
// Copyright (c) 2026 Michael Vandeberg
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Reference examples for boost::capy::consuming_buffers, injected into its documentation by
// doc/addons/extensions/reference-snippets.lua. Declared in:
//   include/boost/capy/buffers/consuming_buffers.hpp
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
template<capy::MutableBufferSequence Buffers>
capy::io_task<std::size_t> read_until_full( capy::any_read_stream& stream, Buffers buffers )
{
    capy::consuming_buffers consuming(buffers);
    std::size_t total = 0, want = capy::buffer_size(buffers);
    while (total < want)
    {
        auto [ec, n] = co_await stream.read_some(consuming.data());
        consuming.consume(n);
        total += n;
        if (ec && total < want) co_return {ec, total};
    }
    co_return {std::error_code(), total};
}
// end::example[]
} // namespace ex_1

} // namespace
