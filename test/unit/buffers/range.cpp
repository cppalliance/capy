//
// Copyright (c) 2023 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Test that header file is self-contained.
#include <boost/capy/buffers/range.hpp>

#include "test_suite.hpp"

namespace boost {
namespace capy {

struct range_test
{
    void
    run()
    {
    }
};

TEST_SUITE(
    range_test,
    "boost.capy.buffers.range");

} // capy
} // boost
