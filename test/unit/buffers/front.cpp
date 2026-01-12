//
// Copyright (c) 2023 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Test that header file is self-contained.
#include <boost/capy/buffers/front.hpp>

#if 0

#include <boost/capy/buffers/copy.hpp>
#include <boost/capy/buffers/buffer_pair.hpp>
#include <boost/capy/buffers/prefix.hpp>
#include <boost/capy/buffers/sans_prefix.hpp>
#include <boost/capy/buffers/sans_suffix.hpp>

#include "test_suite.hpp"

namespace boost {
namespace capy {
namespace buffers {

struct front_test
{
    void
    testBufferSize()
    {
        {
            char a[7]{};
            char b[11]{};
            const_buffer_pair p(
                const_buffer(a, sizeof(a)),
                const_buffer(b, sizeof(b)));
            BOOST_TEST_EQ(
                size(p),
                sizeof(a) + sizeof(b));
        }
    }

    void
    testBufferCopy()
    {
        std::string const s =
            "Howdy partner";
        auto const N = s.size();
        for(std::size_t i = 0;
            i < N; ++i)
        {
            for(std::size_t j = 0;
                j < N; ++j)
            {
                for(std::size_t k = 0;
                    k < N + 2; ++k)
                {
                    const_buffer_pair p0(
                        const_buffer(
                            s.data(), i),
                        const_buffer(
                            s.data() + i, N - i));
                    char tmp[13];
                    std::memset(tmp, 0, sizeof(tmp));
                    mutable_buffer_pair p1(
                        mutable_buffer(
                            tmp, j),
                        mutable_buffer(
                            tmp + j, N - j));
                    auto const n = copy(
                        p1, p0, k);
                    BOOST_TEST_LE(n, N);
                    BOOST_TEST_EQ(
                        s.substr(0, n),
                        std::string(tmp, n));
                }
            }
        }
    }

    void
    testAlgorithms()
    {
        // prefix
        
        {
            char buf[16]{};
            const_buffer b(buf, sizeof(buf));
            const_buffer bp = prefix(b, 5);
            BOOST_TEST_EQ(bp.size(), 5);
        }
 
        {
            char buf[16]{};
            mutable_buffer b(buf, sizeof(buf));
            mutable_buffer bp = prefix(b, 5);
            BOOST_TEST_EQ(bp.size(), 5);
        }

        // sans_prefix
        
        {
            char buf[16]{};
            const_buffer b(buf, sizeof(buf));
            const_buffer bp = sans_prefix(b, 5);
            BOOST_TEST_EQ(bp.size(), 11);
        }
 
        {
            char buf[16]{};
            mutable_buffer b(buf, sizeof(buf));
            mutable_buffer bp = sans_prefix(b, 5);
            BOOST_TEST_EQ(bp.size(), 11);
        }

        // suffix

        {
            char buf[16]{};
            const_buffer b(buf, sizeof(buf));
            const_buffer bp = suffix(b, 5);
            BOOST_TEST_EQ(bp.size(), 5);
        }

        {
            char buf[16]{};
            mutable_buffer b(buf, sizeof(buf));
            mutable_buffer bp = suffix(b, 5);
            BOOST_TEST_EQ(bp.size(), 5);
        }

        // sans_suffix

        {
            char buf[16]{};
            const_buffer b(buf, sizeof(buf));
            const_buffer bp = sans_suffix(b, 5);
            BOOST_TEST_EQ(bp.size(), 11);
        }

        {
            char buf[16]{};
            mutable_buffer b(buf, sizeof(buf));
            mutable_buffer bp = sans_suffix(b, 5);
            BOOST_TEST_EQ(bp.size(), 11);
        }
    }

    void
    run()
    {
        testBufferSize();
        testBufferCopy();
        testAlgorithms();
    }
};

TEST_SUITE(
    front_test,
    "boost.capy.buffers.front");

} // buffers
} // capy
} // boost

#endif
