//
// Copyright (c) 2026 Michael Vandeberg
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Reference examples for boost::capy::immediate, injected into its documentation by
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
// Wrap a sync operation as an awaitable
capy::immediate<int> get_value()
{
    return {42};
}

capy::task<void> example()
{
    int x = co_await get_value();  // No suspension, returns 42
}
// end::example_1[]
} // namespace ex_1

namespace ex_2 {
// tag::example_2[]
struct my_sync_sink
{
    template<capy::ConstBufferSequence CB>
    capy::immediate<capy::io_result<std::size_t>>
    write(CB buffers)
    {
        auto n = process_sync(buffers);
        return {{std::error_code(), n}};
    }

    capy::immediate<capy::io_result<>>
    write_eof()
    {
        return {{}};
    }
};
// end::example_2[]
} // namespace ex_2

} // namespace
