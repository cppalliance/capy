//
// Copyright (c) 2026 Michael Vandeberg
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Reference examples for boost::capy::io_task, injected into its documentation by
// doc/addons/extensions/reference-snippets.lua. Declared in:
//   include/boost/capy/io_task.hpp
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
capy::io_task<std::size_t> read_one( capy::any_read_stream& s, capy::mutable_buffer buf )
{
    co_return co_await s.read_some(buf);  // returns io_result<std::size_t>
}

capy::io_task<> require_ready(bool ready)
{
    if(!ready)
        co_return capy::make_error_code(capy::error::eof);  // error_code converts to io_result<>
    co_return {};
}
// end::example[]
} // namespace ex_1

} // namespace
