//
// Copyright (c) 2025 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Test that header file is self-contained.
#include <boost/capy/core/embed.hpp>

#include "test_suite.hpp"

#if __cpp_lib_string_view >= 201606L
# include <string_view>
#endif

namespace boost {
namespace capy {

namespace {

embed text(R"(
Hello "world"
This has quotes and )
)");

} // (anon)

struct embed_test
{
    core::string_view good =
        "Hello \"world\"\nThis has quotes and )\n";

    void check(core::string_view s)
    {
        BOOST_TEST_EQ(s, good);
    }

#if __cpp_lib_string_view >= 201606L
    void check_std(std::string_view s)
    {
        BOOST_TEST_EQ(s, good);
    }
#endif

    void run()
    {
        check(text);
#if __cpp_lib_string_view >= 201606L
        check_std(text);
#endif
        BOOST_TEST_EQ(text.get(), good);
        BOOST_TEST_EQ(*text, good);
        BOOST_TEST_EQ(text->data(), good);
    }
};

TEST_SUITE(embed_test, "boost.capy.embed");

} // capy
} // boost
