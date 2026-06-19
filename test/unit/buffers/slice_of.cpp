//
// Copyright (c) 2026 Steve Gerbino
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Test that the header is self-contained.
#include <boost/capy/detail/slice_of.hpp>

#include <boost/capy/buffers.hpp>

#include <array>
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

struct slice_of_test
{
    void
    testModelsConcept()
    {
        char a[10];
        std::array<mutable_buffer, 1> mb = { mutable_buffer(a, sizeof(a)) };
        std::array<const_buffer, 1> cb = { const_buffer(a, sizeof(a)) };

        using MS = detail::slice_of<decltype(mb)>;
        using CS = detail::slice_of<decltype(cb)>;

        static_assert(MutableBufferSequence<MS>,
            "slice_of of mutable input must model MutableBufferSequence");
        static_assert(ConstBufferSequence<MS>,
            "slice_of of mutable input must model ConstBufferSequence");
        static_assert(ConstBufferSequence<CS>,
            "slice_of of const input must model ConstBufferSequence");
        static_assert(!MutableBufferSequence<CS>,
            "slice_of of const input must NOT model MutableBufferSequence");
        // It is a plain buffer sequence: no data()/remove_prefix members.
        static_assert(std::ranges::bidirectional_range<MS>,
            "slice_of must be a bidirectional range");
    }

    void
    testWholeRange()
    {
        char a[10], b[20];
        std::memset(a, 'A', sizeof(a));
        std::memset(b, 'B', sizeof(b));
        std::array<mutable_buffer, 2> bufs = {
            mutable_buffer(a, sizeof(a)), mutable_buffer(b, sizeof(b)) };

        detail::slice_of<decltype(bufs)> s(bufs, 0);
        BOOST_TEST_EQ(buffer_size(s), 30u);
        BOOST_TEST_EQ(flatten(s), std::string(10, 'A') + std::string(20, 'B'));
    }

    void
    testOffsetLength()
    {
        char a[10], b[20];
        std::memset(a, 'A', sizeof(a));
        std::memset(b, 'B', sizeof(b));
        std::array<mutable_buffer, 2> bufs = {
            mutable_buffer(a, sizeof(a)), mutable_buffer(b, sizeof(b)) };

        // [5, 15): last 5 of A, first 5 of B
        detail::slice_of<decltype(bufs)> s(bufs, 5, 10);
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

        // offset 12, default length: drop first 12 (all A + 2 B) -> 18 B
        detail::slice_of<decltype(bufs)> s(bufs, 12);
        BOOST_TEST_EQ(buffer_size(s), 18u);
        BOOST_TEST_EQ(flatten(s), std::string(18, 'B'));
    }

    void
    testFrontAndBackTrim()
    {
        char a[10], b[20];
        std::memset(a, 'A', sizeof(a));
        std::memset(b, 'B', sizeof(b));
        std::array<mutable_buffer, 2> bufs = {
            mutable_buffer(a, sizeof(a)), mutable_buffer(b, sizeof(b)) };

        // [3, 27): trims 3 off the front buffer and 3 off the back buffer
        detail::slice_of<decltype(bufs)> s(bufs, 3, 24);
        BOOST_TEST_EQ(buffer_size(s), 24u);
        BOOST_TEST_EQ(flatten(s), std::string(7, 'A') + std::string(17, 'B'));
    }

    void
    testEmptyAndClamp()
    {
        char a[10];
        std::memset(a, 'A', sizeof(a));
        std::array<mutable_buffer, 1> bufs = { mutable_buffer(a, sizeof(a)) };

        // offset past the end -> empty
        detail::slice_of<decltype(bufs)> past(bufs, 100);
        BOOST_TEST_EQ(buffer_size(past), 0u);

        // length past the end -> clamped to remaining
        detail::slice_of<decltype(bufs)> over(bufs, 4, 999);
        BOOST_TEST_EQ(buffer_size(over), 6u);
        BOOST_TEST_EQ(flatten(over), std::string(6, 'A'));
    }

    void
    testSkipsEmptyBuffersAtOffset()
    {
        char a[10], b[20];
        std::memset(a, 'A', sizeof(a));
        std::memset(b, 'B', sizeof(b));
        char empty[1];
        std::array<mutable_buffer, 3> bufs = {
            mutable_buffer(a, sizeof(a)),
            mutable_buffer(empty, 0),
            mutable_buffer(b, sizeof(b)) };

        // offset lands exactly at the boundary after A; empty buffer skipped
        detail::slice_of<decltype(bufs)> s(bufs, 10);
        BOOST_TEST_EQ(buffer_size(s), 20u);
        BOOST_TEST_EQ(flatten(s), std::string(20, 'B'));
    }

    void
    run()
    {
        testModelsConcept();
        testWholeRange();
        testOffsetLength();
        testOffsetToEnd();
        testFrontAndBackTrim();
        testEmptyAndClamp();
        testSkipsEmptyBuffersAtOffset();
    }
};

TEST_SUITE(slice_of_test, "boost.capy.buffers.slice_of");

} // (anon)

} // namespace capy
} // namespace boost
