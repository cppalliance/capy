//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
// Copyright (c) 2026 Michael Vandeberg
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Test that header file is self-contained.
#include <boost/capy/error.hpp>

#include <string>
#include <system_error>

#include "test_suite.hpp"

namespace boost {
namespace capy {

class error_category_test
{
public:
    void
    run()
    {
        auto const ec = make_error_code(error::eof);
        auto const& cat = ec.category();

        BOOST_TEST(std::string(cat.name()) == "boost.capy");

        BOOST_TEST(cat.message(static_cast<int>(error::eof)) == "eof");
        BOOST_TEST(
            cat.message(static_cast<int>(error::canceled)) ==
            "operation canceled");
        BOOST_TEST(
            cat.message(static_cast<int>(error::test_failure)) ==
            "test failure");
        BOOST_TEST(
            cat.message(static_cast<int>(error::stream_truncated)) ==
            "stream truncated");
        BOOST_TEST(cat.message(static_cast<int>(error::timeout)) == "timeout");

        // Out-of-range value hits the default branch.
        BOOST_TEST(cat.message(9999) == "unknown");

        // issue #267: capy error codes compare equal to their portable
        // std conditions via default_error_condition().
        BOOST_TEST(make_error_code(error::canceled) == std::errc::operation_canceled);
        BOOST_TEST(make_error_code(error::timeout)  == std::errc::timed_out);

        // exact repro from the issue
        {
            std::error_code      e = error::canceled;
            std::error_condition c{std::errc::operation_canceled};
            BOOST_TEST(e == c);
        }

        // non-matching codes still differ
        BOOST_TEST(!(make_error_code(error::eof) == std::errc::operation_canceled));
    }
};

TEST_SUITE(error_category_test, "boost.capy.error");

} // capy
} // boost
