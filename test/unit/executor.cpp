//
// Copyright (c) 2025 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Test that header file is self-contained.
#include <boost/capy/concept/executor.hpp>

#include "test_helpers.hpp"

namespace boost {
namespace capy {

struct executor_test
{
    void
    run()
    {
        // executor - equality comparison
        {
            test_io_context ctx1;
            test_io_context ctx2;
            test_executor e1(ctx1);
            test_executor e2(ctx1);
            test_executor e3(ctx2);

            BOOST_TEST(e1 == e2);
            BOOST_TEST(!(e1 != e2));
            BOOST_TEST(e1 != e3);
            BOOST_TEST(!(e1 == e3));
        }

        // executor - context() returns same reference for equal executors
        {
            test_io_context ctx;
            test_executor e1(ctx);
            test_executor e2(ctx);

            BOOST_TEST(e1 == e2);
            BOOST_TEST(&e1.context() == &e2.context());
            BOOST_TEST(&e1.context() == &ctx);
        }

        // executor - copy preserves context
        {
            test_io_context ctx;
            test_executor e1(ctx);
            test_executor e2(e1);

            BOOST_TEST(e1 == e2);
            BOOST_TEST(&e1.context() == &e2.context());
        }
    }
};

TEST_SUITE(
    executor_test,
    "boost.capy.executor");

} // capy
} // boost
