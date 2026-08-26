//
// Copyright (c) 2026 Michael Vandeberg
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Reference examples for boost::capy::test::fuse::fuse, injected into its documentation by
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
void custom_error_code()
{
    auto custom_ec = std::make_error_code(
        std::errc::operation_canceled);
    capy::test::fuse f(custom_ec);

    std::error_code captured_ec;
    auto r = f([&](capy::test::fuse& f) {
        auto ec = f.maybe_fail();
        if(ec)
        {
            captured_ec = ec;
            return;
        }
    });

    // The fuse delivers the error code it was constructed with,
    // not error::test_failure.
    BOOST_TEST( captured_ec == custom_ec );
}
// end::example_1[]
} // namespace ex_1
namespace ex_2 {
// tag::example_2[]
void default_error_code()
{
    capy::test::fuse f;
    std::error_code captured_ec;

    auto r = f([&](capy::test::fuse& f) {
        auto ec = f.maybe_fail();
        if(ec)
        {
            captured_ec = ec;
            return;
        }
    });

    // A default-constructed fuse delivers error::test_failure.
    BOOST_TEST( captured_ec == capy::error::test_failure );
}
// end::example_2[]
} // namespace ex_2

} // namespace
