//
// Copyright (c) 2026 Michael Vandeberg
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Reference examples for boost::capy::ready, injected into its documentation by
// doc/addons/extensions/reference-snippets.lua. Declared in:
//   include/boost/capy/ex/immediate.hpp
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
std::size_t write_all_sync(capy::const_buffer buf)
{
    return capy::buffer_size(buf);
}

std::error_code connect_sync()
{
    return {};
}

capy::immediate<capy::io_result<std::size_t>>
write(capy::const_buffer buf)
{
    auto n = write_all_sync(buf);
    return capy::ready(n);  // success with n bytes
}

capy::immediate<capy::io_result<>>
connect()
{
    connect_sync();
    return capy::ready();  // void success
}
// end::example_1[]
} // namespace ex_1
namespace ex_2 {
// tag::example_2[]
std::error_code write_checked_sync(capy::const_buffer buf)
{
    if(capy::buffer_size(buf) == 0)
        return std::make_error_code(std::errc::invalid_argument);
    return {};
}

capy::immediate<capy::io_result<std::size_t>>
write(capy::const_buffer buf)
{
    auto ec = write_checked_sync(buf);
    if(ec)
        return capy::ready(ec, std::size_t{0});
    return capy::ready(capy::buffer_size(buf));
}
// end::example_2[]
} // namespace ex_2

} // namespace
