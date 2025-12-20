//
// Copyright (c) 2025 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/beast2
//

// Test that header file is self-contained.
#include <boost/capy/task.hpp>

#ifdef BOOST_CAPY_HAS_CORO

#include "test_suite.hpp"

namespace boost {
namespace capy {

static
capy::task<int>
handler()
{
    co_return 42;
}

struct task_test
{
    void
    run()
    {
        auto t = handler();
        while (!t.handle().done())
            t.handle().resume();
        BOOST_TEST_EQ(t.await_resume(), 42);
    }
};

TEST_SUITE(
    task_test,
    "boost.capy.task");

} // capy
} // boost

#endif
