//
// Copyright (c) 2025 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/beast2
//

// Test that header file is self-contained.
#include <boost/capy/neunique_ptr.hpp>

#include "test_suite.hpp"

namespace boost {
namespace capy {

struct neunique_ptr_test
{
    void
    run()
    {
    }
};

TEST_SUITE(
    neunique_ptr_test,
    "boost.capy.neunique_ptr");

} // capy
} // boost
