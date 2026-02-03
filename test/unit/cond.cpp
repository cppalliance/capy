//
// Copyright (c) 2025 Vinnie Falco (vinnie dot falco at gmail dot com)
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

        // Message: not_found
        BOOST_TEST(make_error_condition(cond::not_found).message() == "not found");

        // Equivalence: error::not_found == cond::not_found
        {
            auto ec = make_error_code(error::not_found);
            BOOST_TEST(ec == cond::not_found);
            BOOST_TEST(!(ec == cond::eof));
            BOOST_TEST(!(ec == cond::canceled));
        }

        // Non-matching codes don't match not_found
        {
            auto ec = make_error_code(error::eof);
            BOOST_TEST(!(ec == cond::not_found));
        }
    }
};

TEST_SUITE(cond_test, "boost.capy.cond");

} // capy
} // boost
