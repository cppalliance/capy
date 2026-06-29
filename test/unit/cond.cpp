//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Test that header file is self-contained.
#include <boost/capy/cond.hpp>

#include <boost/capy/error.hpp>
#include <system_error>

#include "test_suite.hpp"

namespace boost {
namespace capy {

class cond_test
{
public:
    void
    run()
    {
        // Category name
        auto ec_eof = make_error_condition(cond::eof);
        BOOST_TEST(std::string(ec_eof.category().name()) == "boost.capy");

        // Messages
        BOOST_TEST(make_error_condition(cond::eof).message() == "end of file");
        BOOST_TEST(make_error_condition(cond::canceled).message() == "operation canceled");

        // Equivalence: error::eof == cond::eof
        {
            auto ec = make_error_code(error::eof);
            BOOST_TEST(ec == cond::eof);
            BOOST_TEST(!(ec == cond::canceled));
        }

        // Equivalence: std::errc::operation_canceled == cond::canceled
        {
            auto ec = make_error_code(std::errc::operation_canceled);
            BOOST_TEST(ec == cond::canceled);
            BOOST_TEST(!(ec == cond::eof));
        }

        // Equivalence: std::errc::operation_canceled == cond::canceled
        {
            std::error_code ec = std::make_error_code(std::errc::operation_canceled);
            BOOST_TEST(ec == cond::canceled);
            BOOST_TEST(!(ec == cond::eof));
        }

        // Non-matching codes return false
        {
            auto ec = make_error_code(std::errc::invalid_argument);
            BOOST_TEST(!(ec == cond::eof));
            BOOST_TEST(!(ec == cond::canceled));
        }

        // Remaining messages, including the default branch.
        auto const ecnd = make_error_condition(cond::eof);
        auto const& cat = ecnd.category();
        BOOST_TEST(
            cat.message(static_cast<int>(cond::stream_truncated)) ==
            "stream truncated");
        BOOST_TEST(
            cat.message(static_cast<int>(cond::timeout)) ==
            "operation timed out");
        BOOST_TEST(cat.message(9999) == "unknown");

        // Equivalence: stream_truncated and timeout.
        BOOST_TEST(make_error_code(error::stream_truncated) ==
            cond::stream_truncated);
        // A non-matching code exercises cond_cat::equivalent for
        // stream_truncated, which the positive comparison above now
        // resolves via error_cat::default_error_condition instead.
        BOOST_TEST(!(make_error_code(error::eof) == cond::stream_truncated));
        BOOST_TEST(make_error_code(error::timeout) == cond::timeout);

        // Equivalence: std::errc::timed_out == cond::timeout
        {
            auto ec = make_error_code(std::errc::timed_out);
            BOOST_TEST(ec == cond::timeout);
            BOOST_TEST(!(ec == cond::eof));
        }
        {
            std::error_code ec = std::make_error_code(std::errc::timed_out);
            BOOST_TEST(ec == cond::timeout);
            BOOST_TEST(!(ec == cond::canceled));
        }

        // Out-of-range condition is equivalent to nothing.
        {
            auto ec = make_error_code(error::eof);
            BOOST_TEST(! cat.equivalent(ec, 9999));
        }
    }
};

TEST_SUITE(cond_test, "boost.capy.cond");

} // capy
} // boost
