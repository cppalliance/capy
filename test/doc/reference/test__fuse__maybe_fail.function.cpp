//
// Copyright (c) 2026 Michael Vandeberg
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Reference examples for boost::capy::test::fuse::maybe_fail, injected into its documentation by
// doc/addons/extensions/reference-snippets.lua. Declared in:
//   include/boost/capy/test/fuse.hpp
//
// The tagged regions are what the reference renders; the includes,
// suppressions and namespaces around them are scaffolding. Each region gets
// its own namespace so that examples which reuse a name still compile.

#include "../doc_warnings.hpp"

#include <boost/capy.hpp>
#include <boost/capy/test.hpp>

#include "test_suite.hpp"

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
capy::test::fuse f;
auto r = f([](capy::test::fuse& f) {
    // Error code mode: returns the error
    auto ec = f.maybe_fail();
    if(ec)
        return;

    // Exception mode: throws system_error
    ec = f.maybe_fail();
    if(ec)
        return;
});
// end::example_1[]
} // namespace ex_1

namespace ex_2 {
// tag::example_2[]
capy::test::fuse f;
auto ec = f.maybe_fail();  // Always returns {} (no-op)
// end::example_2[]
} // namespace ex_2

} // namespace
