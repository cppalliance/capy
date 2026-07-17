//
// Copyright (c) 2026 Michael Vandeberg
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Test that the header is self-contained.
#include <boost/capy/buffers/buffer_slice.hpp>

#include <boost/capy/buffers.hpp>

#include <array>
#include <concepts>
#include <cstring>
#include <string>

#include "test_suite.hpp"

namespace boost {
namespace capy {

namespace {

// Flatten the bytes of a buffer sequence for byte-exact comparison.
template<class Seq>
std::string
flatten(Seq const& s)
{
    std::string out;
    for (auto it = capy::begin(s); it != capy::end(s); ++it)
    {
        const_buffer b(*it);
        out.append(static_cast<char const*>(b.data()), b.size());
    }
    return out;
}

struct buffer_slice_test
{
    void
    testReturnsBufferSequence()
    {
        char a[10], b[20];
        std::memset(a, 'A', sizeof(a));
        std::memset(b, 'B', sizeof(b));
        std::array<mutable_buffer, 2> bufs = {
            mutable_buffer(a, sizeof(a)), mutable_buffer(b, sizeof(b)) };

        auto s = buffer_slice(bufs, 0);
        // The result IS a buffer sequence (not a Slice with .data()).
        static_assert(MutableBufferSequence<decltype(s)>,
            "buffer_slice's result must model MutableBufferSequence");
        static_assert(std::same_as<decltype(s), slice_type<decltype(bufs)>>,
            "buffer_slice must return slice_type<BS>");
        BOOST_TEST_EQ(buffer_size(s), 30u);
        BOOST_TEST_EQ(flatten(s), std::string(10, 'A') + std::string(20, 'B'));
    }

    void
    testConstInput()
    {
        char a[10] = {};
        std::array<const_buffer, 1> cb = { const_buffer(a, sizeof(a)) };
        auto s = buffer_slice(cb);
        static_assert(ConstBufferSequence<decltype(s)>,
            "buffer_slice over const input must model ConstBufferSequence");
        static_assert(!MutableBufferSequence<decltype(s)>,
            "buffer_slice over const input must NOT model MutableBufferSequence");
    }

    void
    testOffsetLength()
    {
        char a[10], b[20];
        std::memset(a, 'A', sizeof(a));
        std::memset(b, 'B', sizeof(b));
        std::array<mutable_buffer, 2> bufs = {
            mutable_buffer(a, sizeof(a)), mutable_buffer(b, sizeof(b)) };

        auto s = buffer_slice(bufs, 5, 10);
        BOOST_TEST_EQ(buffer_size(s), 10u);
        BOOST_TEST_EQ(flatten(s), std::string(5, 'A') + std::string(5, 'B'));
    }

    void
    testOffsetToEnd()
    {
        char a[10], b[20];
        std::memset(a, 'A', sizeof(a));
        std::memset(b, 'B', sizeof(b));
        std::array<mutable_buffer, 2> bufs = {
            mutable_buffer(a, sizeof(a)), mutable_buffer(b, sizeof(b)) };

        auto s = buffer_slice(bufs, 12);
        BOOST_TEST_EQ(buffer_size(s), 18u);
        BOOST_TEST_EQ(flatten(s), std::string(18, 'B'));
    }

    void
    testSingleBufferSelfSlice()
    {
        char a[10];
        std::memset(a, 'A', sizeof(a));

        // A single buffer is closed under sub-ranging: slicing yields a
        // buffer of the same kind, NOT a slice_of wrapper.
        mutable_buffer mb(a, sizeof(a));
        auto s = buffer_slice(mb, 2, 5);
        static_assert(std::same_as<decltype(s), mutable_buffer>,
            "slicing a mutable_buffer yields a mutable_buffer");
        BOOST_TEST_EQ(buffer_size(s), 5u);
        BOOST_TEST_EQ(flatten(s), std::string(5, 'A'));

        const_buffer cbuf(a, sizeof(a));
        auto cs = buffer_slice(cbuf, 3);
        static_assert(std::same_as<decltype(cs), const_buffer>,
            "slicing a const_buffer yields a const_buffer");
        BOOST_TEST_EQ(buffer_size(cs), 7u);
    }

    void
    testClampAndEmpty()
    {
        char a[10];
        std::memset(a, 'A', sizeof(a));
        std::array<mutable_buffer, 1> bufs = { mutable_buffer(a, sizeof(a)) };

        auto over = buffer_slice(bufs, 4, 999);  // length clamps to remaining
        BOOST_TEST_EQ(buffer_size(over), 6u);

        auto past = buffer_slice(bufs, 100);      // offset past end -> empty
        BOOST_TEST_EQ(buffer_size(past), 0u);
    }

    void
    run()
    {
        testReturnsBufferSequence();
        testConstInput();
        testOffsetLength();
        testOffsetToEnd();
        testSingleBufferSelfSlice();
        testClampAndEmpty();
    }
};

TEST_SUITE(buffer_slice_test, "boost.capy.buffers.buffer_slice");

} // (anon)

} // namespace capy
} // namespace boost
