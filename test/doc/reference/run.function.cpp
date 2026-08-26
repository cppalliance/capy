//
// Copyright (c) 2026 Michael Vandeberg
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Reference examples for boost::capy::run, injected into its documentation by
// doc/addons/extensions/reference-snippets.lua. Declared in:
//   include/boost/capy/ex/run.hpp
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
capy::task<void> cancellable_task() { co_return; }

capy::task<void> override_stop_token()
{
    std::stop_source source;
    co_await capy::run(source.get_token())(cancellable_task());
}
// end::example_1[]
} // namespace ex_1

namespace ex_2 {
// tag::example_2[]
capy::task<void> my_task() { co_return; }

capy::task<void> switch_executor( capy::any_executor other_executor )
{
    co_await capy::run(other_executor)(my_task());
}
// end::example_2[]
} // namespace ex_2

} // namespace
