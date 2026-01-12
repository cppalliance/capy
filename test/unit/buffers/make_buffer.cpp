//
// Copyright (c) 2023 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Test that header file is self-contained.
#include <boost/capy/buffers/make_buffer.hpp>

#include "test_suite.hpp"

namespace boost {
namespace capy {
namespace buffers {

struct make_buffer_test
{
    void
    testBuffer()
    {
        char buf[3]{};
        char const* cbuf = buf;

        // make_buffer(mutable_buffer)
        {
            mutable_buffer b(buf, 3);
            auto b1 = make_buffer(b);
            BOOST_TEST_EQ(b1.data(), b.data());
            BOOST_TEST_EQ(b1.size(), b.size());
        }

        // make_buffer(void*, std::size_t)
        {
            auto b = make_buffer(buf, 3);
            BOOST_TEST_EQ(b.data(), buf);
            BOOST_TEST_EQ(b.size(), 3);
        }

        // make_buffer(const_buffer)
        {
            const_buffer b(cbuf, 3);
            auto b1 = make_buffer(b);
            BOOST_TEST_EQ(b1.data(), b.data());
            BOOST_TEST_EQ(b1.size(), b.size());
        }

        // make_buffer(void const*, std::size_t)
        {
            auto b = make_buffer(cbuf, 3);
            BOOST_TEST_EQ(b.data(), cbuf);
            BOOST_TEST_EQ(b.size(), 3);
        }

        // make_buffer(T(&)[N])
        {
            mutable_buffer b = make_buffer(buf);
            BOOST_TEST_EQ(b.data(), buf);
            BOOST_TEST_EQ(b.size(), 3);
        }

        // make_buffer(T const(&)[N])
        {
            char const cbuf3[3]{};
            const_buffer b = make_buffer(cbuf3);
            BOOST_TEST_EQ(b.data(), cbuf3);
            BOOST_TEST_EQ(b.size(), 3);
        }
    }

    void
    run()
    {
        testBuffer();
    }
};

TEST_SUITE(
    make_buffer_test,
    "boost.capy.buffers.make_buffer");

} // buffers
} // capy
} // boost
