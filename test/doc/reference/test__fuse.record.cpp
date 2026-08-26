//
// Copyright (c) 2026 Michael Vandeberg
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Reference examples for boost::capy::test::fuse, injected into its documentation by
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
void basic_inline_usage()
{
    capy::test::fuse()([](capy::test::fuse& f) {
        auto ec = f.maybe_fail();
        if(ec)
            return;

        ec = f.maybe_fail();
        if(ec)
            return;
    });
}
// end::example_1[]
} // namespace ex_1

namespace ex_2 {
// tag::example_2[]
void named_fuse_with_armed()
{
    struct MyObject
    {
        capy::test::fuse& f;
        explicit MyObject(capy::test::fuse& f) : f(f) {}

        void do_something()
        {
            auto ec = f.maybe_fail();
            if(ec)
                return;
        }
    };

    capy::test::fuse f;
    MyObject obj(f);
    auto r = f.armed([&](capy::test::fuse&) {
        obj.do_something();
    });
}
// end::example_2[]
} // namespace ex_2

namespace ex_3 {
// tag::example_3[]
void inert_single_run_test(bool some_condition)
{
    capy::test::fuse f;
    auto r = f.inert([&](capy::test::fuse& f) {
        auto ec = f.maybe_fail();  // Always succeeds
        if(some_condition)
            f.fail();  // Only way to signal failure
    });
}
// end::example_3[]
} // namespace ex_3

namespace ex_4 {
// tag::example_4[]
void dependency_injection_standalone_usage()
{
    class MyService
    {
        capy::test::fuse& f_;
    public:
        explicit MyService(capy::test::fuse& f) : f_(f) {}

        std::error_code do_work()
        {
            auto ec = f_.maybe_fail();  // No-op outside armed/inert
            if(ec)
                return ec;
            // ... actual work ...
            return {};
        }
    };

    // Production usage - fuse is no-op
    capy::test::fuse f;
    MyService svc(f);
    svc.do_work();  // maybe_fail() returns {} always

    // Test usage - failures are injected
    auto r = f.armed([&](capy::test::fuse&) {
        svc.do_work();  // maybe_fail() triggers failures
    });
}
// end::example_4[]
} // namespace ex_4

namespace ex_5 {
// tag::example_5[]
auto custom_ec = std::make_error_code(
    std::errc::operation_canceled);
capy::test::fuse f(custom_ec);
auto r = f.armed([](capy::test::fuse& f) {
    auto ec = f.maybe_fail();
    if(ec)
        return;
});
// end::example_5[]
} // namespace ex_5

namespace ex_6 {
// tag::example_6[]
void checking_the_result()
{
    capy::test::fuse f;
    auto r = f([](capy::test::fuse& f) {
        auto ec = f.maybe_fail();
        if(ec)
            return;
    });

    if(!r)
    {
        std::cerr << "Failed at " << r.loc.file_name()
            << ":" << r.loc.line() << "\n";
    }
}
// end::example_6[]
} // namespace ex_6

namespace ex_7 {
// tag::example_7[]
void test_framework_integration()
{
    capy::test::fuse f;
    auto r = f([](capy::test::fuse& f) {
        auto ec = f.maybe_fail();
        if(ec)
            return;
    });

    // BOOST_TEST is capy's own Boost.Test-style assertion macro,
    // declared in extra/test_suite/test_suite.hpp and included as
    // "test_suite.hpp". A Catch2 suite would spell the same check
    // REQUIRE(r.success).
    BOOST_TEST(r.success);
    if(!r)
    {
        std::cerr << "Failed at " << r.loc.file_name()
            << ":" << r.loc.line() << "\n";
    }
}
// end::example_7[]
} // namespace ex_7

} // namespace
