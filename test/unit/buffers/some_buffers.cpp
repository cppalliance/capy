//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Test that header file is self-contained.
#include <boost/capy/buffers/some_buffers.hpp>

#include "test_buffers.hpp"

#include <array>
#include <cstring>
#include <string>

namespace boost {
namespace capy {
namespace {

class some_buffers_test
{
public:
    void
    testSomeConstBuffers()
    {
        // Default construct
        {
            some_const_buffers bufs;
            BOOST_TEST_EQ(bufs.size(), 0u);
            BOOST_TEST(bufs.begin() == bufs.end());
        }

        // Construct from single buffer
        {
            char const data[] = "hello";
            const_buffer buf(data, 5);
            some_const_buffers bufs(buf);
            BOOST_TEST_EQ(bufs.size(), 1u);
            BOOST_TEST_EQ(bufs.data()[0].data(), data);
            BOOST_TEST_EQ(bufs.data()[0].size(), 5u);
        }

        // Construct from array of buffers
        {
            char const d1[] = "hello";
            char const d2[] = "world";
            char const d3[] = "!";
            std::array<const_buffer, 3> arr = {{
                const_buffer(d1, 5),
                const_buffer(d2, 5),
                const_buffer(d3, 1)
            }};
            some_const_buffers bufs(arr);
            BOOST_TEST_EQ(bufs.size(), 3u);
            BOOST_TEST_EQ(bufs.data()[0].size(), 5u);
            BOOST_TEST_EQ(bufs.data()[1].size(), 5u);
            BOOST_TEST_EQ(bufs.data()[2].size(), 1u);
        }

        // Copy construct
        {
            char const data[] = "test";
            const_buffer buf(data, 4);
            some_const_buffers bufs1(buf);
            some_const_buffers bufs2(bufs1);
            BOOST_TEST_EQ(bufs2.size(), 1u);
            BOOST_TEST_EQ(bufs2.data()[0].data(), data);
            BOOST_TEST_EQ(bufs2.data()[0].size(), 4u);
        }

        // Iterator access
        {
            char const data[] = "abc";
            const_buffer buf(data, 3);
            some_const_buffers bufs(buf);
            std::size_t count = 0;
            for(auto it = bufs.begin(); it != bufs.end(); ++it)
                ++count;
            BOOST_TEST_EQ(count, 1u);
        }

        // Skip single empty buffer
        {
            const_buffer buf(nullptr, 0);
            some_const_buffers bufs(buf);
            BOOST_TEST_EQ(bufs.size(), 0u);
        }

        // Skip all empty buffers
        {
            std::array<const_buffer, 3> arr = {{
                const_buffer(nullptr, 0),
                const_buffer(nullptr, 0),
                const_buffer(nullptr, 0)
            }};
            some_const_buffers bufs(arr);
            BOOST_TEST_EQ(bufs.size(), 0u);
        }

        // Skip empty buffers at beginning
        {
            char const d1[] = "hello";
            std::array<const_buffer, 3> arr = {{
                const_buffer(nullptr, 0),
                const_buffer(nullptr, 0),
                const_buffer(d1, 5)
            }};
            some_const_buffers bufs(arr);
            BOOST_TEST_EQ(bufs.size(), 1u);
            BOOST_TEST_EQ(bufs.data()[0].data(), d1);
            BOOST_TEST_EQ(bufs.data()[0].size(), 5u);
        }

        // Skip empty buffers in middle
        {
            char const d1[] = "hello";
            char const d2[] = "world";
            std::array<const_buffer, 4> arr = {{
                const_buffer(d1, 5),
                const_buffer(nullptr, 0),
                const_buffer(nullptr, 0),
                const_buffer(d2, 5)
            }};
            some_const_buffers bufs(arr);
            BOOST_TEST_EQ(bufs.size(), 2u);
            BOOST_TEST_EQ(bufs.data()[0].data(), d1);
            BOOST_TEST_EQ(bufs.data()[1].data(), d2);
        }

        // Skip empty buffers at end
        {
            char const d1[] = "hello";
            std::array<const_buffer, 3> arr = {{
                const_buffer(d1, 5),
                const_buffer(nullptr, 0),
                const_buffer(nullptr, 0)
            }};
            some_const_buffers bufs(arr);
            BOOST_TEST_EQ(bufs.size(), 1u);
            BOOST_TEST_EQ(bufs.data()[0].data(), d1);
        }

        // Mixed empty and non-empty
        {
            char const d1[] = "a";
            char const d2[] = "bb";
            char const d3[] = "ccc";
            std::array<const_buffer, 6> arr = {{
                const_buffer(nullptr, 0),
                const_buffer(d1, 1),
                const_buffer(nullptr, 0),
                const_buffer(d2, 2),
                const_buffer(d3, 3),
                const_buffer(nullptr, 0)
            }};
            some_const_buffers bufs(arr);
            BOOST_TEST_EQ(bufs.size(), 3u);
            BOOST_TEST_EQ(bufs.data()[0].size(), 1u);
            BOOST_TEST_EQ(bufs.data()[1].size(), 2u);
            BOOST_TEST_EQ(bufs.data()[2].size(), 3u);
        }
    }

    void
    testSomeMutableBuffers()
    {
        // Default construct
        {
            some_mutable_buffers bufs;
            BOOST_TEST_EQ(bufs.size(), 0u);
            BOOST_TEST(bufs.begin() == bufs.end());
        }

        // Construct from single buffer
        {
            char data[32] = {};
            mutable_buffer buf(data, sizeof(data));
            some_mutable_buffers bufs(buf);
            BOOST_TEST_EQ(bufs.size(), 1u);
            BOOST_TEST_EQ(bufs.data()[0].data(), data);
            BOOST_TEST_EQ(bufs.data()[0].size(), sizeof(data));
        }

        // Construct from array of buffers
        {
            char d1[8] = {};
            char d2[16] = {};
            char d3[4] = {};
            std::array<mutable_buffer, 3> arr = {{
                mutable_buffer(d1, sizeof(d1)),
                mutable_buffer(d2, sizeof(d2)),
                mutable_buffer(d3, sizeof(d3))
            }};
            some_mutable_buffers bufs(arr);
            BOOST_TEST_EQ(bufs.size(), 3u);
            BOOST_TEST_EQ(bufs.data()[0].size(), 8u);
            BOOST_TEST_EQ(bufs.data()[1].size(), 16u);
            BOOST_TEST_EQ(bufs.data()[2].size(), 4u);
        }

        // Copy construct
        {
            char data[64] = {};
            mutable_buffer buf(data, sizeof(data));
            some_mutable_buffers bufs1(buf);
            some_mutable_buffers bufs2(bufs1);
            BOOST_TEST_EQ(bufs2.size(), 1u);
            BOOST_TEST_EQ(bufs2.data()[0].data(), data);
            BOOST_TEST_EQ(bufs2.data()[0].size(), sizeof(data));
        }

        // Iterator access
        {
            char data[10] = {};
            mutable_buffer buf(data, sizeof(data));
            some_mutable_buffers bufs(buf);
            std::size_t count = 0;
            for(auto it = bufs.begin(); it != bufs.end(); ++it)
                ++count;
            BOOST_TEST_EQ(count, 1u);
        }

        // Skip single empty buffer
        {
            mutable_buffer buf(nullptr, 0);
            some_mutable_buffers bufs(buf);
            BOOST_TEST_EQ(bufs.size(), 0u);
        }

        // Skip all empty buffers
        {
            std::array<mutable_buffer, 3> arr = {{
                mutable_buffer(nullptr, 0),
                mutable_buffer(nullptr, 0),
                mutable_buffer(nullptr, 0)
            }};
            some_mutable_buffers bufs(arr);
            BOOST_TEST_EQ(bufs.size(), 0u);
        }

        // Skip empty buffers at beginning
        {
            char d1[8] = {};
            std::array<mutable_buffer, 3> arr = {{
                mutable_buffer(nullptr, 0),
                mutable_buffer(nullptr, 0),
                mutable_buffer(d1, sizeof(d1))
            }};
            some_mutable_buffers bufs(arr);
            BOOST_TEST_EQ(bufs.size(), 1u);
            BOOST_TEST_EQ(bufs.data()[0].data(), d1);
            BOOST_TEST_EQ(bufs.data()[0].size(), sizeof(d1));
        }

        // Skip empty buffers in middle
        {
            char d1[8] = {};
            char d2[16] = {};
            std::array<mutable_buffer, 4> arr = {{
                mutable_buffer(d1, sizeof(d1)),
                mutable_buffer(nullptr, 0),
                mutable_buffer(nullptr, 0),
                mutable_buffer(d2, sizeof(d2))
            }};
            some_mutable_buffers bufs(arr);
            BOOST_TEST_EQ(bufs.size(), 2u);
            BOOST_TEST_EQ(bufs.data()[0].data(), d1);
            BOOST_TEST_EQ(bufs.data()[1].data(), d2);
        }

        // Skip empty buffers at end
        {
            char d1[8] = {};
            std::array<mutable_buffer, 3> arr = {{
                mutable_buffer(d1, sizeof(d1)),
                mutable_buffer(nullptr, 0),
                mutable_buffer(nullptr, 0)
            }};
            some_mutable_buffers bufs(arr);
            BOOST_TEST_EQ(bufs.size(), 1u);
            BOOST_TEST_EQ(bufs.data()[0].data(), d1);
        }

        // Mixed empty and non-empty
        {
            char d1[4] = {};
            char d2[8] = {};
            char d3[12] = {};
            std::array<mutable_buffer, 6> arr = {{
                mutable_buffer(nullptr, 0),
                mutable_buffer(d1, sizeof(d1)),
                mutable_buffer(nullptr, 0),
                mutable_buffer(d2, sizeof(d2)),
                mutable_buffer(d3, sizeof(d3)),
                mutable_buffer(nullptr, 0)
            }};
            some_mutable_buffers bufs(arr);
            BOOST_TEST_EQ(bufs.size(), 3u);
            BOOST_TEST_EQ(bufs.data()[0].size(), sizeof(d1));
            BOOST_TEST_EQ(bufs.data()[1].size(), sizeof(d2));
            BOOST_TEST_EQ(bufs.data()[2].size(), sizeof(d3));
        }
    }

    void
    run()
    {
        testSomeConstBuffers();
        testSomeMutableBuffers();
    }
};

TEST_SUITE(some_buffers_test, "boost.capy.buffers.some_buffers");

} // namespace
} // namespace capy
} // namespace boost
