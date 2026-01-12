//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Test that header file is self-contained.
#include <boost/capy/buffers/to_string.hpp>

#include "test_suite.hpp"

namespace boost {
namespace capy {
namespace buffers {

struct to_string_test
{
    void
    run()
    {
        BOOST_TEST_EQ(to_string(const_buffer("hello", 5)), "hello");
    }
};

TEST_SUITE(
    to_string_test,
    "boost.capy.buffers.to_string");

} // buffers
} // capy
} // boost
