//
// Copyright (c) 2025 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Test that header file is self-contained.
#include <boost/capy/error.hpp>

#include "test_suite.hpp"

namespace boost {
namespace capy {
namespace buffers {

class error_test
{
public:
    void
    check(
        char const* name,
        error ev)
    {
        auto const ec = make_error_code(ev);
        auto const esuccess =
          make_error_code(static_cast<error>(0));
        auto const& cat =
         esuccess.category();
        BOOST_TEST(std::string(ec.category().name()) == name);
        BOOST_TEST(! ec.message().empty());
        BOOST_TEST(
            std::addressof(ec.category()) == std::addressof(cat));
        BOOST_TEST(cat.equivalent(
            static_cast<std::underlying_type<error>::type>(ev),
                ec.category().default_error_condition(
                    static_cast<std::underlying_type<error>::type>(ev))));
        BOOST_TEST(cat.equivalent(ec,
            static_cast<std::underlying_type<error>::type>(ev)));
    }

    void
    run()
    {
        char const* const n = "boost.capy.buffers";

        check(n, error::eof);
    }
};

TEST_SUITE(error_test, "boost.capy.buffers");

} // buffers
} // capy
} // boost
