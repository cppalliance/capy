//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Test that header file is self-contained.
#include <boost/capy/ex/this_coro.hpp>

#include "test_suite.hpp"

namespace boost {
namespace capy {

struct this_coro_executor_test
{
    void
    testTagType()
    {
        this_coro::executor_tag tag1;
        this_coro::executor_tag tag2{};
        (void)tag1;
        (void)tag2;

        static_assert(std::is_trivially_copyable_v<this_coro::executor_tag>);
    }

    void
    testConstant()
    {
        auto tag = this_coro::executor;
        static_assert(std::is_same_v<
            decltype(this_coro::executor), this_coro::executor_tag const>);
        (void)tag;
    }

    void
    run()
    {
        testTagType();
        testConstant();
    }
};

TEST_SUITE(
    this_coro_executor_test,
    "boost.capy.ex.this_coro.executor");

} // namespace capy
} // namespace boost
