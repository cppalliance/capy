//
// Copyright (c) 2025 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Test that header file is self-contained.
#include <boost/capy/write.hpp>

#include <boost/capy/buffers/buffer_pair.hpp>
#include <boost/capy/buffers/make_buffer.hpp>
#include <boost/capy/error.hpp>
#include <boost/capy/test/fuse.hpp>
#include <boost/capy/test/write_stream.hpp>

#include "test_suite.hpp"

#include <array>
#include <string>
#include <string_view>

namespace boost {
namespace capy {

namespace {

//----------------------------------------------------------
// Buffer Factories for WriteStream tests
//----------------------------------------------------------

struct single_buffer_factory
{
    std::string data;

    explicit single_buffer_factory(std::string_view sv)
        : data(sv)
    {
    }

    const_buffer
    buffer() const
    {
        return make_buffer(data);
    }

    std::size_t
    size() const
    {
        return data.size();
    }
};

struct buffer_array_factory
{
    std::string data1;
    std::string data2;

    buffer_array_factory(std::string_view sv1, std::string_view sv2)
        : data1(sv1)
        , data2(sv2)
    {
    }

    std::array<const_buffer, 2>
    buffer() const
    {
        return {{
            const_buffer(data1.data(), data1.size()),
            const_buffer(data2.data(), data2.size())
        }};
    }

    std::size_t
    size() const
    {
        return data1.size() + data2.size();
    }

    std::string
    combined() const
    {
        return data1 + data2;
    }
};

struct buffer_pair_factory
{
    std::string data1;
    std::string data2;

    buffer_pair_factory(std::string_view sv1, std::string_view sv2)
        : data1(sv1)
        , data2(sv2)
    {
    }

    const_buffer_pair
    buffer() const
    {
        return {{
            const_buffer(data1.data(), data1.size()),
            const_buffer(data2.data(), data2.size())
        }};
    }

    std::size_t
    size() const
    {
        return data1.size() + data2.size();
    }

    std::string
    combined() const
    {
        return data1 + data2;
    }
};

} // namespace

struct write_test
{
    //----------------------------------------------------------
    // WriteStream tests (ConstBufferSequence)
    //----------------------------------------------------------

    void
    testWriteSingleBuffer()
    {
        // Write single buffer completely
        BOOST_TEST(test::fuse().armed([](test::fuse& f) -> task<void>
        {
            test::write_stream ws(f);

            single_buffer_factory bf("hello world");
            auto [ec, n] = co_await write(ws, bf.buffer());
            if(ec)
                co_return;

            BOOST_TEST_EQ(n, 11u);
            BOOST_TEST_EQ(ws.data(), "hello world");
        }));

        // Write exact size
        BOOST_TEST(test::fuse().armed([](test::fuse& f) -> task<void>
        {
            test::write_stream ws(f);

            single_buffer_factory bf("exact");
            auto [ec, n] = co_await write(ws, bf.buffer());
            if(ec)
                co_return;

            BOOST_TEST_EQ(n, 5u);
            BOOST_TEST_EQ(ws.data(), "exact");
        }));

        // Write empty buffer
        BOOST_TEST(test::fuse().armed([](test::fuse& f) -> task<void>
        {
            test::write_stream ws(f);

            auto [ec, n] = co_await write(ws, const_buffer());
            if(ec)
                co_return;

            BOOST_TEST_EQ(n, 0u);
            BOOST_TEST(ws.data().empty());
        }));

        // Write large data
        BOOST_TEST(test::fuse().armed([](test::fuse& f) -> task<void>
        {
            test::write_stream ws(f);

            std::string large_data(10000, 'x');
            single_buffer_factory bf(large_data);
            auto [ec, n] = co_await write(ws, bf.buffer());
            if(ec)
                co_return;

            BOOST_TEST_EQ(n, 10000u);
            BOOST_TEST_EQ(ws.data().size(), 10000u);
            BOOST_TEST(ws.data() == large_data);
        }));
    }

    void
    testWriteBufferArray()
    {
        // Write buffer array completely
        BOOST_TEST(test::fuse().armed([](test::fuse& f) -> task<void>
        {
            test::write_stream ws(f);

            buffer_array_factory bf("hello", "world");
            auto [ec, n] = co_await write(ws, bf.buffer());
            if(ec)
                co_return;

            BOOST_TEST_EQ(n, 10u);
            BOOST_TEST_EQ(ws.data(), "helloworld");
        }));

        // Write buffer array with empty first buffer
        BOOST_TEST(test::fuse().armed([](test::fuse& f) -> task<void>
        {
            test::write_stream ws(f);

            buffer_array_factory bf("", "world");
            auto [ec, n] = co_await write(ws, bf.buffer());
            if(ec)
                co_return;

            BOOST_TEST_EQ(n, 5u);
            BOOST_TEST_EQ(ws.data(), "world");
        }));
    }

    void
    testWriteBufferPair()
    {
        // Write buffer pair completely
        BOOST_TEST(test::fuse().armed([](test::fuse& f) -> task<void>
        {
            test::write_stream ws(f);

            buffer_pair_factory bf("hello", "world");
            auto [ec, n] = co_await write(ws, bf.buffer());
            if(ec)
                co_return;

            BOOST_TEST_EQ(n, 10u);
            BOOST_TEST_EQ(ws.data(), "helloworld");
        }));

        // Write buffer pair with uneven sizes
        BOOST_TEST(test::fuse().armed([](test::fuse& f) -> task<void>
        {
            test::write_stream ws(f);

            buffer_pair_factory bf("ab", "cdefgh");
            auto [ec, n] = co_await write(ws, bf.buffer());
            if(ec)
                co_return;

            BOOST_TEST_EQ(n, 8u);
            BOOST_TEST_EQ(ws.data(), "abcdefgh");
        }));
    }

    void
    testWriteStream()
    {
        testWriteSingleBuffer();
        testWriteBufferArray();
        testWriteBufferPair();
    }

    //----------------------------------------------------------

    void
    run()
    {
        testWriteStream();
    }
};

TEST_SUITE(
    write_test,
    "boost.capy.write");

} // namespace capy
} // namespace boost
