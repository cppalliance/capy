//
// Copyright (c) 2026 Steve Gerbino
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Test that the header is self-contained.
#include <boost/capy/buffers/consuming_buffers.hpp>

#include <boost/capy/buffers.hpp>

#include <array>
#include <cstring>
#include <string>

#include "test_suite.hpp"

namespace boost {
namespace capy {

namespace {

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

struct consuming_buffers_test
{
    void
    testNotABufferSequence()
    {
        char a[4];
        std::array<mutable_buffer, 1> bufs = { mutable_buffer(a, sizeof(a)) };
        using C = consuming_buffers<decltype(bufs)>;
        // Door 1: the cursor is NOT itself a buffer sequence.
        static_assert(!ConstBufferSequence<C>,
            "consuming_buffers must not itself model a buffer sequence");
        // Its data() IS a buffer sequence.
        static_assert(MutableBufferSequence<decltype(
            std::declval<C const&>().data())>,
            "consuming_buffers::data() must be a buffer sequence");
    }

    void
    testInitialDataIsWhole()
    {
        char a[10], b[20];
        std::memset(a, 'A', sizeof(a));
        std::memset(b, 'B', sizeof(b));
        std::array<mutable_buffer, 2> bufs = {
            mutable_buffer(a, sizeof(a)), mutable_buffer(b, sizeof(b)) };

        consuming_buffers consuming(bufs);
        BOOST_TEST_EQ(buffer_size(consuming.data()), 30u);
        BOOST_TEST_EQ(flatten(consuming.data()),
            std::string(10, 'A') + std::string(20, 'B'));
    }

    void
    testConsumeWithinBuffer()
    {
        char a[10], b[20];
        std::memset(a, 'A', sizeof(a));
        std::memset(b, 'B', sizeof(b));
        std::array<mutable_buffer, 2> bufs = {
            mutable_buffer(a, sizeof(a)), mutable_buffer(b, sizeof(b)) };

        consuming_buffers consuming(bufs);
        consuming.consume(3);
        BOOST_TEST_EQ(buffer_size(consuming.data()), 27u);
        BOOST_TEST_EQ(flatten(consuming.data()),
            std::string(7, 'A') + std::string(20, 'B'));
    }

    void
    testConsumeAcrossBuffers()
    {
        char a[10], b[20];
        std::memset(a, 'A', sizeof(a));
        std::memset(b, 'B', sizeof(b));
        std::array<mutable_buffer, 2> bufs = {
            mutable_buffer(a, sizeof(a)), mutable_buffer(b, sizeof(b)) };

        consuming_buffers consuming(bufs);
        consuming.consume(15);  // all of A + 5 of B
        BOOST_TEST_EQ(buffer_size(consuming.data()), 15u);
        BOOST_TEST_EQ(flatten(consuming.data()), std::string(15, 'B'));
    }

    void
    testIncrementalConsume()
    {
        char a[10], b[20];
        std::memset(a, 'A', sizeof(a));
        std::memset(b, 'B', sizeof(b));
        std::array<mutable_buffer, 2> bufs = {
            mutable_buffer(a, sizeof(a)), mutable_buffer(b, sizeof(b)) };

        // Consume in several steps; data() must track the remainder exactly.
        consuming_buffers consuming(bufs);
        consuming.consume(4);
        consuming.consume(4);
        consuming.consume(4);   // 12 total: all A + 2 B
        BOOST_TEST_EQ(buffer_size(consuming.data()), 18u);
        BOOST_TEST_EQ(flatten(consuming.data()), std::string(18, 'B'));
    }

    void
    testConsumeAllAndClamp()
    {
        char a[10];
        std::memset(a, 'A', sizeof(a));
        std::array<mutable_buffer, 1> bufs = { mutable_buffer(a, sizeof(a)) };

        consuming_buffers consuming(bufs);
        consuming.consume(1000);  // clamped to 10
        BOOST_TEST_EQ(buffer_size(consuming.data()), 0u);
        BOOST_TEST(buffer_empty(consuming.data()));
    }

    void
    run()
    {
        testNotABufferSequence();
        testInitialDataIsWhole();
        testConsumeWithinBuffer();
        testConsumeAcrossBuffers();
        testIncrementalConsume();
        testConsumeAllAndClamp();
    }
};

TEST_SUITE(consuming_buffers_test, "boost.capy.buffers.consuming_buffers");

} // (anon)

} // namespace capy
} // namespace boost
