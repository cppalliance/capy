//
// Copyright (c) 2025 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Test that header file is self-contained.
#include <boost/capy/ex/get_stop_token.hpp>

#include "test_suite.hpp"

namespace boost {
namespace capy {

#if BOOST_CAPY_HAS_STOP_TOKEN

struct get_stop_token_test
{
    void
    testTagType()
    {
        get_stop_token_tag tag1;
        get_stop_token_tag tag2{};
        (void)tag1;
        (void)tag2;

        static_assert(std::is_trivially_copyable_v<get_stop_token_tag>);
    }

    void
    testFunction()
    {
        auto tag = get_stop_token();
        static_assert(std::is_same_v<decltype(tag), get_stop_token_tag>);
        static_assert(noexcept(get_stop_token()));
    }

    void
    run()
    {
        testTagType();
        testFunction();
    }
};

TEST_SUITE(
    get_stop_token_test,
    "boost.capy.ex.get_stop_token");

#endif // BOOST_CAPY_HAS_STOP_TOKEN

} // namespace capy
} // namespace boost
