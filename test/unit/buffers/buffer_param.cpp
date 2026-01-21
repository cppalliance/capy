//
// Copyright (c) 2025 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Test that header file is self-contained.
#include <boost/capy/buffers/buffer_param.hpp>

#include <boost/capy/buffers/buffer_pair.hpp>
#include <boost/core/span.hpp>
#include <array>

#include "test_buffers.hpp"

namespace boost {
namespace capy {

struct buffer_param_test
{
    void
    testConstBuffer()
    {
        char const data[] = "Hello";
        const_buffer cb(data, 5);

        buffer_param ref(cb);

        mutable_buffer dest[8];
        auto n = ref.copy_to(dest, 8);
        BOOST_TEST_EQ(n, 1);
        BOOST_TEST_EQ(dest[0].data(), data);
        BOOST_TEST_EQ(dest[0].size(), 5);
    }

    void
    testMutableBuffer()
    {
        char data[] = "Hello";
        mutable_buffer mb(data, 5);

        buffer_param ref(mb);

        mutable_buffer dest[8];
        auto n = ref.copy_to(dest, 8);
        BOOST_TEST_EQ(n, 1);
        BOOST_TEST_EQ(dest[0].data(), data);
        BOOST_TEST_EQ(dest[0].size(), 5);
    }

    void
    testConstBufferPair()
    {
        char const data1[] = "Hello";
        char const data2[] = "World";
        const_buffer_pair cbp{{
            const_buffer(data1, 5),
            const_buffer(data2, 5) }};

        buffer_param ref(cbp);

        mutable_buffer dest[8];
        auto n = ref.copy_to(dest, 8);
        BOOST_TEST_EQ(n, 2);
        BOOST_TEST_EQ(dest[0].data(), data1);
        BOOST_TEST_EQ(dest[0].size(), 5);
        BOOST_TEST_EQ(dest[1].data(), data2);
        BOOST_TEST_EQ(dest[1].size(), 5);
    }

    void
    testMutableBufferPair()
    {
        char data1[] = "Hello";
        char data2[] = "World";
        mutable_buffer_pair mbp{{
            mutable_buffer(data1, 5),
            mutable_buffer(data2, 5) }};

        buffer_param ref(mbp);

        mutable_buffer dest[8];
        auto n = ref.copy_to(dest, 8);
        BOOST_TEST_EQ(n, 2);
        BOOST_TEST_EQ(dest[0].data(), data1);
        BOOST_TEST_EQ(dest[0].size(), 5);
        BOOST_TEST_EQ(dest[1].data(), data2);
        BOOST_TEST_EQ(dest[1].size(), 5);
    }

    void
    testSpan()
    {
        char const data1[] = "One";
        char const data2[] = "Two";
        char const data3[] = "Three";
        const_buffer arr[3] = {
            const_buffer(data1, 3),
            const_buffer(data2, 3),
            const_buffer(data3, 5) };
        span<const_buffer const> s(arr, 3);

        buffer_param ref(s);

        mutable_buffer dest[8];
        auto n = ref.copy_to(dest, 8);
        BOOST_TEST_EQ(n, 3);
        BOOST_TEST_EQ(dest[0].data(), data1);
        BOOST_TEST_EQ(dest[0].size(), 3);
        BOOST_TEST_EQ(dest[1].data(), data2);
        BOOST_TEST_EQ(dest[1].size(), 3);
        BOOST_TEST_EQ(dest[2].data(), data3);
        BOOST_TEST_EQ(dest[2].size(), 5);
    }

    void
    testArray()
    {
        char const data1[] = "One";
        char const data2[] = "Two";
        char const data3[] = "Three";
        std::array<const_buffer, 3> arr{{
            const_buffer(data1, 3),
            const_buffer(data2, 3),
            const_buffer(data3, 5) }};

        buffer_param ref(arr);

        mutable_buffer dest[8];
        auto n = ref.copy_to(dest, 8);
        BOOST_TEST_EQ(n, 3);
        BOOST_TEST_EQ(dest[0].data(), data1);
        BOOST_TEST_EQ(dest[0].size(), 3);
        BOOST_TEST_EQ(dest[1].data(), data2);
        BOOST_TEST_EQ(dest[1].size(), 3);
        BOOST_TEST_EQ(dest[2].data(), data3);
        BOOST_TEST_EQ(dest[2].size(), 5);
    }

    void
    testCArray()
    {
        char const data1[] = "One";
        char const data2[] = "Two";
        char const data3[] = "Three";
        const_buffer arr[3] = {
            const_buffer(data1, 3),
            const_buffer(data2, 3),
            const_buffer(data3, 5) };

        buffer_param ref(arr);

        mutable_buffer dest[8];
        auto n = ref.copy_to(dest, 8);
        BOOST_TEST_EQ(n, 3);
        BOOST_TEST_EQ(dest[0].data(), data1);
        BOOST_TEST_EQ(dest[0].size(), 3);
        BOOST_TEST_EQ(dest[1].data(), data2);
        BOOST_TEST_EQ(dest[1].size(), 3);
        BOOST_TEST_EQ(dest[2].data(), data3);
        BOOST_TEST_EQ(dest[2].size(), 5);
    }

    void
    testLimitedCopy()
    {
        char const data1[] = "One";
        char const data2[] = "Two";
        char const data3[] = "Three";
        const_buffer arr[3] = {
            const_buffer(data1, 3),
            const_buffer(data2, 3),
            const_buffer(data3, 5) };

        buffer_param ref(arr);

        // copy only 2 buffers
        mutable_buffer dest[2];
        auto n = ref.copy_to(dest, 2);
        BOOST_TEST_EQ(n, 2);
        BOOST_TEST_EQ(dest[0].data(), data1);
        BOOST_TEST_EQ(dest[0].size(), 3);
        BOOST_TEST_EQ(dest[1].data(), data2);
        BOOST_TEST_EQ(dest[1].size(), 3);
    }

    void
    testEmptySequence()
    {
        const_buffer cb;
        buffer_param ref(cb);

        mutable_buffer dest[8];
        auto n = ref.copy_to(dest, 8);
        // Zero total bytes returns 0, regardless of buffer count
        BOOST_TEST_EQ(n, 0);
    }

    void
    testZeroByteConstBuffer()
    {
        // Explicit zero-byte const buffer
        char const* data = "Hello";
        const_buffer cb(data, 0);

        buffer_param ref(cb);

        mutable_buffer dest[8];
        auto n = ref.copy_to(dest, 8);
        BOOST_TEST_EQ(n, 0);
    }

    void
    testZeroByteMultiple()
    {
        // Multiple zero-byte buffers should still return 0
        char const data1[] = "Hello";
        char const data2[] = "World";
        const_buffer arr[3] = {
            const_buffer(data1, 0),
            const_buffer(data2, 0),
            const_buffer(nullptr, 0) };

        buffer_param ref(arr);

        mutable_buffer dest[8];
        auto n = ref.copy_to(dest, 8);
        BOOST_TEST_EQ(n, 0);
    }

    void
    testZeroByteBufferPair()
    {
        // Buffer pair with both zero-byte buffers
        char const data1[] = "Hello";
        char const data2[] = "World";
        const_buffer_pair cbp{{
            const_buffer(data1, 0),
            const_buffer(data2, 0) }};

        buffer_param ref(cbp);

        mutable_buffer dest[8];
        auto n = ref.copy_to(dest, 8);
        BOOST_TEST_EQ(n, 0);
    }

    void
    testMixedZeroAndNonZero()
    {
        // Mix of zero-byte and non-zero buffers
        // Total bytes > 0, so should return buffer count
        char const data1[] = "Hello";
        char const data2[] = "World";
        const_buffer arr[3] = {
            const_buffer(data1, 0),
            const_buffer(data2, 5),
            const_buffer(nullptr, 0) };

        buffer_param ref(arr);

        mutable_buffer dest[8];
        auto n = ref.copy_to(dest, 8);
        BOOST_TEST_EQ(n, 3);
        BOOST_TEST_EQ(dest[0].size(), 0);
        BOOST_TEST_EQ(dest[1].data(), data2);
        BOOST_TEST_EQ(dest[1].size(), 5);
        BOOST_TEST_EQ(dest[2].size(), 0);
    }

    void
    testOneZeroOneNonZero()
    {
        // Buffer pair with one zero-byte, one non-zero
        char const data1[] = "Hello";
        char const data2[] = "World";
        const_buffer_pair cbp{{
            const_buffer(data1, 0),
            const_buffer(data2, 5) }};

        buffer_param ref(cbp);

        mutable_buffer dest[8];
        auto n = ref.copy_to(dest, 8);
        BOOST_TEST_EQ(n, 2);
        BOOST_TEST_EQ(dest[0].size(), 0);
        BOOST_TEST_EQ(dest[1].data(), data2);
        BOOST_TEST_EQ(dest[1].size(), 5);
    }

    void
    testZeroByteMutableBuffer()
    {
        // Zero-byte mutable buffer
        char data[] = "Hello";
        mutable_buffer mb(data, 0);

        buffer_param ref(mb);

        mutable_buffer dest[8];
        auto n = ref.copy_to(dest, 8);
        BOOST_TEST_EQ(n, 0);
    }

    void
    testZeroByteMutableBufferPair()
    {
        // Mutable buffer pair with zero-byte buffers
        char data1[] = "Hello";
        char data2[] = "World";
        mutable_buffer_pair mbp{{
            mutable_buffer(data1, 0),
            mutable_buffer(data2, 0) }};

        buffer_param ref(mbp);

        mutable_buffer dest[8];
        auto n = ref.copy_to(dest, 8);
        BOOST_TEST_EQ(n, 0);
    }

    void
    testEmptySpan()
    {
        // Empty span (no buffers at all)
        span<const_buffer const> s;

        buffer_param ref(s);

        mutable_buffer dest[8];
        auto n = ref.copy_to(dest, 8);
        BOOST_TEST_EQ(n, 0);
    }

    void
    testEmptyArray()
    {
        // Empty std::array (zero-size)
        std::array<const_buffer, 0> arr{};

        buffer_param ref(arr);

        mutable_buffer dest[8];
        auto n = ref.copy_to(dest, 8);
        BOOST_TEST_EQ(n, 0);
    }

    // Helper function that accepts buffer_param by value
    static std::size_t
    acceptByValue(buffer_param p)
    {
        mutable_buffer dest[8];
        return p.copy_to(dest, 8);
    }

    // Helper function that accepts buffer_param by const reference
    static std::size_t
    acceptByConstRef(buffer_param const& p)
    {
        mutable_buffer dest[8];
        return p.copy_to(dest, 8);
    }

    void
    testPassByValue()
    {
        // Test that buffer_param works when passed by value
        char const data[] = "Hello";
        const_buffer cb(data, 5);

        // Pass buffer directly (implicit conversion)
        auto n = acceptByValue(cb);
        BOOST_TEST_EQ(n, 1);

        // Pass buffer_param object
        buffer_param p(cb);
        n = acceptByValue(p);
        BOOST_TEST_EQ(n, 1);

        // Pass buffer sequence directly
        std::array<const_buffer, 2> arr{{
            const_buffer(data, 2),
            const_buffer(data + 2, 3) }};
        n = acceptByValue(arr);
        BOOST_TEST_EQ(n, 2);
    }

    void
    testPassByConstRef()
    {
        // Test that buffer_param works when passed by const reference
        char const data[] = "Hello";
        const_buffer cb(data, 5);

        // Pass buffer_param object by const ref
        buffer_param p(cb);
        auto n = acceptByConstRef(p);
        BOOST_TEST_EQ(n, 1);

        // Pass buffer sequence directly (creates temporary)
        std::array<const_buffer, 2> arr{{
            const_buffer(data, 2),
            const_buffer(data + 2, 3) }};
        n = acceptByConstRef(arr);
        BOOST_TEST_EQ(n, 2);
    }

    void
    run()
    {
        testConstBuffer();
        testMutableBuffer();
        testConstBufferPair();
        testMutableBufferPair();
        testSpan();
        testArray();
        testCArray();
        testLimitedCopy();
        testEmptySequence();
        testZeroByteConstBuffer();
        testZeroByteMultiple();
        testZeroByteBufferPair();
        testMixedZeroAndNonZero();
        testOneZeroOneNonZero();
        testZeroByteMutableBuffer();
        testZeroByteMutableBufferPair();
        testEmptySpan();
        testEmptyArray();
        testPassByValue();
        testPassByConstRef();
    }
};

TEST_SUITE(
    buffer_param_test,
    "boost.capy.buffers.buffer_param");

} // capy
} // boost
