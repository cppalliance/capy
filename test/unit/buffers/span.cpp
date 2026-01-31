//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Test that header file is self-contained.
#include <boost/capy/buffers/span.hpp>

#include <boost/capy/buffers/make_buffer.hpp>
#include <boost/capy/test/buffer_to_string.hpp>

#include <array>
#include <span>
#include <string_view>

#include "test_suite.hpp"

namespace boost {
namespace capy {

struct span_test
{
    void
    testRemovePrefixConstBuffer()
    {
        // Single buffer
        {
            std::string_view data = "0123456789";
            std::array<const_buffer, 1> arr = { make_buffer(data) };
            std::span<const_buffer> bs(arr);

            remove_span_prefix(bs, 0);
            BOOST_TEST_EQ(test::buffer_to_string(bs), "0123456789");

            remove_span_prefix(bs, 3);
            BOOST_TEST_EQ(test::buffer_to_string(bs), "3456789");

            remove_span_prefix(bs, 4);
            BOOST_TEST_EQ(test::buffer_to_string(bs), "789");

            remove_span_prefix(bs, 100);
            BOOST_TEST_EQ(buffer_size(bs), 0u);
        }

        // Multiple buffers
        {
            std::string_view s1 = "ABCD";
            std::string_view s2 = "EFGH";
            std::string_view s3 = "IJKL";
            std::array<const_buffer, 3> arr = {
                make_buffer(s1),
                make_buffer(s2),
                make_buffer(s3)
            };
            std::span<const_buffer> bs(arr);

            BOOST_TEST_EQ(test::buffer_to_string(bs), "ABCDEFGHIJKL");

            remove_span_prefix(bs, 2);
            BOOST_TEST_EQ(test::buffer_to_string(bs), "CDEFGHIJKL");

            remove_span_prefix(bs, 3);
            BOOST_TEST_EQ(test::buffer_to_string(bs), "FGHIJKL");

            remove_span_prefix(bs, 4);
            BOOST_TEST_EQ(test::buffer_to_string(bs), "JKL");

            remove_span_prefix(bs, 3);
            BOOST_TEST_EQ(buffer_size(bs), 0u);
        }
    }

    void
    testRemovePrefixMutableBuffer()
    {
        char data[] = "0123456789";
        std::array<mutable_buffer, 1> arr = {
            mutable_buffer(data, 10)
        };
        std::span<mutable_buffer> bs(arr);

        remove_span_prefix(bs, 3);
        BOOST_TEST_EQ(test::buffer_to_string(bs), "3456789");

        remove_span_prefix(bs, 100);
        BOOST_TEST_EQ(buffer_size(bs), 0u);
    }

    void
    testRemoveSuffixConstBuffer()
    {
        // Single buffer
        {
            std::string_view data = "0123456789";
            std::array<const_buffer, 1> arr = { make_buffer(data) };
            std::span<const_buffer> bs(arr);

            remove_span_suffix(bs, 0);
            BOOST_TEST_EQ(test::buffer_to_string(bs), "0123456789");

            remove_span_suffix(bs, 3);
            BOOST_TEST_EQ(test::buffer_to_string(bs), "0123456");

            remove_span_suffix(bs, 4);
            BOOST_TEST_EQ(test::buffer_to_string(bs), "012");

            remove_span_suffix(bs, 100);
            BOOST_TEST_EQ(buffer_size(bs), 0u);
        }

        // Multiple buffers
        {
            std::string_view s1 = "ABCD";
            std::string_view s2 = "EFGH";
            std::string_view s3 = "IJKL";
            std::array<const_buffer, 3> arr = {
                make_buffer(s1),
                make_buffer(s2),
                make_buffer(s3)
            };
            std::span<const_buffer> bs(arr);

            BOOST_TEST_EQ(test::buffer_to_string(bs), "ABCDEFGHIJKL");

            remove_span_suffix(bs, 2);
            BOOST_TEST_EQ(test::buffer_to_string(bs), "ABCDEFGHIJ");

            remove_span_suffix(bs, 3);
            BOOST_TEST_EQ(test::buffer_to_string(bs), "ABCDEFG");

            remove_span_suffix(bs, 4);
            BOOST_TEST_EQ(test::buffer_to_string(bs), "ABC");

            remove_span_suffix(bs, 3);
            BOOST_TEST_EQ(buffer_size(bs), 0u);
        }
    }

    void
    testRemoveSuffixMutableBuffer()
    {
        char data[] = "0123456789";
        std::array<mutable_buffer, 1> arr = {
            mutable_buffer(data, 10)
        };
        std::span<mutable_buffer> bs(arr);

        remove_span_suffix(bs, 3);
        BOOST_TEST_EQ(test::buffer_to_string(bs), "0123456");

        remove_span_suffix(bs, 100);
        BOOST_TEST_EQ(buffer_size(bs), 0u);
    }

    void
    testKeepPrefixConstBuffer()
    {
        std::string_view s1 = "ABCD";
        std::string_view s2 = "EFGH";
        std::string_view s3 = "IJKL";
        std::array<const_buffer, 3> arr = {
            make_buffer(s1),
            make_buffer(s2),
            make_buffer(s3)
        };
        std::span<const_buffer> bs(arr);

        keep_span_prefix(bs, 100);
        BOOST_TEST_EQ(test::buffer_to_string(bs), "ABCDEFGHIJKL");

        keep_span_prefix(bs, 7);
        BOOST_TEST_EQ(test::buffer_to_string(bs), "ABCDEFG");

        keep_span_prefix(bs, 3);
        BOOST_TEST_EQ(test::buffer_to_string(bs), "ABC");

        keep_span_prefix(bs, 0);
        BOOST_TEST_EQ(buffer_size(bs), 0u);
    }

    void
    testKeepPrefixMutableBuffer()
    {
        char data[] = "0123456789";
        std::array<mutable_buffer, 1> arr = {
            mutable_buffer(data, 10)
        };
        std::span<mutable_buffer> bs(arr);

        keep_span_prefix(bs, 5);
        BOOST_TEST_EQ(test::buffer_to_string(bs), "01234");

        keep_span_prefix(bs, 0);
        BOOST_TEST_EQ(buffer_size(bs), 0u);
    }

    void
    testKeepSuffixConstBuffer()
    {
        std::string_view s1 = "ABCD";
        std::string_view s2 = "EFGH";
        std::string_view s3 = "IJKL";
        std::array<const_buffer, 3> arr = {
            make_buffer(s1),
            make_buffer(s2),
            make_buffer(s3)
        };
        std::span<const_buffer> bs(arr);

        keep_span_suffix(bs, 100);
        BOOST_TEST_EQ(test::buffer_to_string(bs), "ABCDEFGHIJKL");

        keep_span_suffix(bs, 7);
        BOOST_TEST_EQ(test::buffer_to_string(bs), "FGHIJKL");

        keep_span_suffix(bs, 3);
        BOOST_TEST_EQ(test::buffer_to_string(bs), "JKL");

        keep_span_suffix(bs, 0);
        BOOST_TEST_EQ(buffer_size(bs), 0u);
    }

    void
    testKeepSuffixMutableBuffer()
    {
        char data[] = "0123456789";
        std::array<mutable_buffer, 1> arr = {
            mutable_buffer(data, 10)
        };
        std::span<mutable_buffer> bs(arr);

        keep_span_suffix(bs, 5);
        BOOST_TEST_EQ(test::buffer_to_string(bs), "56789");

        keep_span_suffix(bs, 0);
        BOOST_TEST_EQ(buffer_size(bs), 0u);
    }

    void
    testEmptySpan()
    {
        std::span<const_buffer> bs;

        remove_span_prefix(bs, 10);
        BOOST_TEST_EQ(buffer_size(bs), 0u);

        remove_span_suffix(bs, 10);
        BOOST_TEST_EQ(buffer_size(bs), 0u);

        keep_span_prefix(bs, 10);
        BOOST_TEST_EQ(buffer_size(bs), 0u);

        keep_span_suffix(bs, 10);
        BOOST_TEST_EQ(buffer_size(bs), 0u);
    }

    void
    run()
    {
        testRemovePrefixConstBuffer();
        testRemovePrefixMutableBuffer();
        testRemoveSuffixConstBuffer();
        testRemoveSuffixMutableBuffer();
        testKeepPrefixConstBuffer();
        testKeepPrefixMutableBuffer();
        testKeepSuffixConstBuffer();
        testKeepSuffixMutableBuffer();
        testEmptySpan();
    }
};

TEST_SUITE(
    span_test,
    "boost.capy.buffers.span");

} // capy
} // boost
