//
// Copyright (c) 2025 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Test that header file is self-contained.
#include <boost/capy/read.hpp>

#include <boost/capy/buffers/buffer_pair.hpp>
#include <boost/capy/buffers/circular_dynamic_buffer.hpp>
#include <boost/capy/buffers/make_buffer.hpp>
#include <boost/capy/buffers/string_dynamic_buffer.hpp>
#include <boost/capy/cond.hpp>
#include <boost/capy/error.hpp>
#include <boost/capy/test/fuse.hpp>
#include <boost/capy/test/read_source.hpp>
#include <boost/capy/test/read_stream.hpp>

#include "test_suite.hpp"

#include <array>
#include <cstring>
#include <string>

namespace boost {
namespace capy {

namespace {

//----------------------------------------------------------
// Buffer Factories for ReadStream tests
//----------------------------------------------------------

struct single_buffer_factory
{
    char storage[1024];
    std::size_t size;

    explicit single_buffer_factory(std::size_t n)
        : size(n)
    {
        std::memset(storage, 0, sizeof(storage));
    }

    mutable_buffer
    buffer()
    {
        return mutable_buffer(storage, size);
    }

    std::string_view
    view() const
    {
        return std::string_view(storage, size);
    }
};

struct buffer_array_factory
{
    char storage1[512];
    char storage2[512];
    std::size_t size1;
    std::size_t size2;

    buffer_array_factory(std::size_t n1, std::size_t n2)
        : size1(n1)
        , size2(n2)
    {
        std::memset(storage1, 0, sizeof(storage1));
        std::memset(storage2, 0, sizeof(storage2));
    }

    std::array<mutable_buffer, 2>
    buffer()
    {
        return {{
            mutable_buffer(storage1, size1),
            mutable_buffer(storage2, size2)
        }};
    }

    std::string
    combined() const
    {
        std::string result;
        result.append(storage1, size1);
        result.append(storage2, size2);
        return result;
    }
};

struct buffer_pair_factory
{
    char storage1[512];
    char storage2[512];
    std::size_t size1;
    std::size_t size2;

    buffer_pair_factory(std::size_t n1, std::size_t n2)
        : size1(n1)
        , size2(n2)
    {
        std::memset(storage1, 0, sizeof(storage1));
        std::memset(storage2, 0, sizeof(storage2));
    }

    mutable_buffer_pair
    buffer()
    {
        return {{
            mutable_buffer(storage1, size1),
            mutable_buffer(storage2, size2)
        }};
    }

    std::string
    combined() const
    {
        std::string result;
        result.append(storage1, size1);
        result.append(storage2, size2);
        return result;
    }
};

//----------------------------------------------------------
// Dynamic Buffer Factories for ReadSource tests
//----------------------------------------------------------

struct string_dynbuf_factory
{
    std::string str;

    string_dynamic_buffer
    buffer()
    {
        str.clear();
        return string_dynamic_buffer(&str);
    }

    std::string const&
    data() const
    {
        return str;
    }
};

struct circular_dynamic_buffer_factory
{
    char storage[4096];
    circular_dynamic_buffer cb;

    circular_dynamic_buffer_factory()
        : cb(storage, sizeof(storage))
    {
    }

    circular_dynamic_buffer&
    buffer()
    {
        cb = circular_dynamic_buffer(storage, sizeof(storage));
        return cb;
    }

    std::string
    data() const
    {
        std::string result;
        auto bufs = cb.data();
        for(auto const& buf : bufs)
            result.append(
                static_cast<char const*>(buf.data()),
                buf.size());
        return result;
    }
};

} // namespace

struct read_test
{
    //----------------------------------------------------------
    // ReadStream tests (MutableBufferSequence)
    //----------------------------------------------------------

    void
    testReadSingleBuffer()
    {
        // Read fills buffer completely
        BOOST_TEST(test::fuse().armed([](test::fuse& f) -> task<void>
        {
            test::read_stream rs(f);
            rs.provide("hello world");

            single_buffer_factory bf(11);
            auto [ec, n] = co_await read(rs, bf.buffer());
            if(ec)
                co_return;

            BOOST_TEST_EQ(n, 11u);
            BOOST_TEST_EQ(bf.view(), "hello world");
        }));

        // Read exact size
        BOOST_TEST(test::fuse().armed([](test::fuse& f) -> task<void>
        {
            test::read_stream rs(f);
            rs.provide("exact");

            single_buffer_factory bf(5);
            auto [ec, n] = co_await read(rs, bf.buffer());
            if(ec)
                co_return;

            BOOST_TEST_EQ(n, 5u);
            BOOST_TEST_EQ(bf.view(), "exact");
        }));

        // EOF before buffer full
        BOOST_TEST(test::fuse().armed([](test::fuse& f) -> task<void>
        {
            test::read_stream rs(f);
            rs.provide("short");

            single_buffer_factory bf(32);
            auto [ec, n] = co_await read(rs, bf.buffer());
            if(ec)
                co_return;

            BOOST_TEST(ec == cond::eof);
            BOOST_TEST_EQ(n, 5u);
            BOOST_TEST_EQ(std::string_view(bf.storage, n), "short");
        }));

        // Empty buffer returns immediately
        BOOST_TEST(test::fuse().armed([](test::fuse& f) -> task<void>
        {
            test::read_stream rs(f);
            rs.provide("data");

            auto [ec, n] = co_await read(rs, mutable_buffer());
            if(ec)
                co_return;

            BOOST_TEST_EQ(n, 0u);
        }));
    }

    void
    testReadBufferArray()
    {
        // Read fills buffer array completely
        BOOST_TEST(test::fuse().armed([](test::fuse& f) -> task<void>
        {
            test::read_stream rs(f);
            rs.provide("helloworld");

            buffer_array_factory bf(5, 5);
            auto [ec, n] = co_await read(rs, bf.buffer());
            if(ec)
                co_return;

            BOOST_TEST_EQ(n, 10u);
            BOOST_TEST_EQ(std::string_view(bf.storage1, 5), "hello");
            BOOST_TEST_EQ(std::string_view(bf.storage2, 5), "world");
        }));

        // EOF before buffer array full
        BOOST_TEST(test::fuse().armed([](test::fuse& f) -> task<void>
        {
            test::read_stream rs(f);
            rs.provide("short");

            buffer_array_factory bf(10, 10);
            auto [ec, n] = co_await read(rs, bf.buffer());
            if(ec)
                co_return;

            BOOST_TEST(ec == cond::eof);
            BOOST_TEST_EQ(n, 5u);
        }));
    }

    void
    testReadBufferPair()
    {
        // Read fills buffer pair completely
        BOOST_TEST(test::fuse().armed([](test::fuse& f) -> task<void>
        {
            test::read_stream rs(f);
            rs.provide("helloworld");

            buffer_pair_factory bf(5, 5);
            auto [ec, n] = co_await read(rs, bf.buffer());
            if(ec)
                co_return;

            BOOST_TEST_EQ(n, 10u);
            BOOST_TEST_EQ(std::string_view(bf.storage1, 5), "hello");
            BOOST_TEST_EQ(std::string_view(bf.storage2, 5), "world");
        }));

        // EOF before buffer pair full
        BOOST_TEST(test::fuse().armed([](test::fuse& f) -> task<void>
        {
            test::read_stream rs(f);
            rs.provide("abc");

            buffer_pair_factory bf(5, 5);
            auto [ec, n] = co_await read(rs, bf.buffer());
            if(ec)
                co_return;

            BOOST_TEST(ec == cond::eof);
            BOOST_TEST_EQ(n, 3u);
        }));
    }

    void
    testReadStream()
    {
        testReadSingleBuffer();
        testReadBufferArray();
        testReadBufferPair();
    }

    //----------------------------------------------------------
    // ReadSource tests (DynamicBuffer)
    //----------------------------------------------------------

    void
    testSourceStringDynBuf()
    {
        // Read all data until EOF
        BOOST_TEST(test::fuse().armed([](test::fuse& f) -> task<void>
        {
            test::read_source rs(f);
            rs.provide("hello world");

            string_dynbuf_factory df;
            auto db = df.buffer();
            auto [ec, n] = co_await read(rs, db);
            if(ec)
                co_return;

            BOOST_TEST_EQ(n, 11u);
            BOOST_TEST_EQ(df.data(), "hello world");
        }));

        // Read large data (tests growth strategy)
        BOOST_TEST(test::fuse().armed([](test::fuse& f) -> task<void>
        {
            test::read_source rs(f);
            std::string large_data(10000, 'x');
            rs.provide(large_data);

            string_dynbuf_factory df;
            auto db = df.buffer();
            auto [ec, n] = co_await read(rs, db);
            if(ec)
                co_return;

            BOOST_TEST_EQ(n, 10000u);
            BOOST_TEST_EQ(df.data().size(), 10000u);
            BOOST_TEST(df.data() == large_data);
        }));

        // Empty source
        BOOST_TEST(test::fuse().armed([](test::fuse& f) -> task<void>
        {
            test::read_source rs(f);

            string_dynbuf_factory df;
            auto db = df.buffer();
            auto [ec, n] = co_await read(rs, db);
            if(ec)
                co_return;

            BOOST_TEST_EQ(n, 0u);
            BOOST_TEST(df.data().empty());
        }));

        // Custom initial_amount
        BOOST_TEST(test::fuse().armed([](test::fuse& f) -> task<void>
        {
            test::read_source rs(f);
            rs.provide("small");

            string_dynbuf_factory df;
            auto db = df.buffer();
            auto [ec, n] = co_await read(rs, db, 64);
            if(ec)
                co_return;

            BOOST_TEST_EQ(n, 5u);
            BOOST_TEST_EQ(df.data(), "small");
        }));
    }

    void
    testSourceCircularBuffer()
    {
        // Read all data until EOF
        BOOST_TEST(test::fuse().armed([](test::fuse& f) -> task<void>
        {
            test::read_source rs(f);
            rs.provide("hello world");

            circular_dynamic_buffer_factory df;
            auto& db = df.buffer();
            auto [ec, n] = co_await read(rs, db);
            if(ec)
                co_return;

            BOOST_TEST_EQ(n, 11u);
            BOOST_TEST_EQ(df.data(), "hello world");
        }));

        // Read larger data
        BOOST_TEST(test::fuse().armed([](test::fuse& f) -> task<void>
        {
            test::read_source rs(f);
            std::string data(1000, 'y');
            rs.provide(data);

            circular_dynamic_buffer_factory df;
            auto& db = df.buffer();
            auto [ec, n] = co_await read(rs, db);
            if(ec)
                co_return;

            BOOST_TEST_EQ(n, 1000u);
            BOOST_TEST_EQ(df.data().size(), 1000u);
            BOOST_TEST(df.data() == data);
        }));

        // Empty source
        BOOST_TEST(test::fuse().armed([](test::fuse& f) -> task<void>
        {
            test::read_source rs(f);

            circular_dynamic_buffer_factory df;
            auto& db = df.buffer();
            auto [ec, n] = co_await read(rs, db);
            if(ec)
                co_return;

            BOOST_TEST_EQ(n, 0u);
            BOOST_TEST(df.data().empty());
        }));

        // Custom initial_amount
        BOOST_TEST(test::fuse().armed([](test::fuse& f) -> task<void>
        {
            test::read_source rs(f);
            rs.provide("tiny");

            circular_dynamic_buffer_factory df;
            auto& db = df.buffer();
            auto [ec, n] = co_await read(rs, db, 128);
            if(ec)
                co_return;

            BOOST_TEST_EQ(n, 4u);
            BOOST_TEST_EQ(df.data(), "tiny");
        }));
    }

    void
    testReadSource()
    {
        testSourceStringDynBuf();
        testSourceCircularBuffer();
    }

    //----------------------------------------------------------
    // ReadStream + DynamicBuffer tests
    //----------------------------------------------------------

    void
    testStreamDynBufString()
    {
        // Read all data until EOF
        BOOST_TEST(test::fuse().armed([](test::fuse& f) -> task<void>
        {
            test::read_stream rs(f);
            rs.provide("hello world");

            string_dynbuf_factory df;
            auto db = df.buffer();
            auto [ec, n] = co_await read(rs, db);
            if(ec)
                co_return;

            BOOST_TEST_EQ(n, 11u);
            BOOST_TEST_EQ(df.data(), "hello world");
        }));

        // Read large data (tests growth strategy)
        BOOST_TEST(test::fuse().armed([](test::fuse& f) -> task<void>
        {
            test::read_stream rs(f);
            std::string large_data(10000, 'x');
            rs.provide(large_data);

            string_dynbuf_factory df;
            auto db = df.buffer();
            auto [ec, n] = co_await read(rs, db);
            if(ec)
                co_return;

            BOOST_TEST_EQ(n, 10000u);
            BOOST_TEST_EQ(df.data().size(), 10000u);
            BOOST_TEST(df.data() == large_data);
        }));

        // Empty stream (immediate EOF)
        BOOST_TEST(test::fuse().armed([](test::fuse& f) -> task<void>
        {
            test::read_stream rs(f);

            string_dynbuf_factory df;
            auto db = df.buffer();
            auto [ec, n] = co_await read(rs, db);
            if(ec)
                co_return;

            BOOST_TEST_EQ(n, 0u);
            BOOST_TEST(df.data().empty());
        }));

        // Custom initial_amount
        BOOST_TEST(test::fuse().armed([](test::fuse& f) -> task<void>
        {
            test::read_stream rs(f);
            rs.provide("small");

            string_dynbuf_factory df;
            auto db = df.buffer();
            auto [ec, n] = co_await read(rs, db, 64);
            if(ec)
                co_return;

            BOOST_TEST_EQ(n, 5u);
            BOOST_TEST_EQ(df.data(), "small");
        }));

        // Chunked reads with max_read_size
        BOOST_TEST(test::fuse().armed([](test::fuse& f) -> task<void>
        {
            test::read_stream rs(f, 3);
            rs.provide("hello world");

            string_dynbuf_factory df;
            auto db = df.buffer();
            auto [ec, n] = co_await read(rs, db);
            if(ec)
                co_return;

            BOOST_TEST_EQ(n, 11u);
            BOOST_TEST_EQ(df.data(), "hello world");
        }));
    }

    void
    testStreamDynBufCircular()
    {
        // Read all data until EOF
        BOOST_TEST(test::fuse().armed([](test::fuse& f) -> task<void>
        {
            test::read_stream rs(f);
            rs.provide("hello world");

            circular_dynamic_buffer_factory df;
            auto& db = df.buffer();
            auto [ec, n] = co_await read(rs, db);
            if(ec)
                co_return;

            BOOST_TEST_EQ(n, 11u);
            BOOST_TEST_EQ(df.data(), "hello world");
        }));

        // Read larger data
        BOOST_TEST(test::fuse().armed([](test::fuse& f) -> task<void>
        {
            test::read_stream rs(f);
            std::string data(1000, 'y');
            rs.provide(data);

            circular_dynamic_buffer_factory df;
            auto& db = df.buffer();
            auto [ec, n] = co_await read(rs, db);
            if(ec)
                co_return;

            BOOST_TEST_EQ(n, 1000u);
            BOOST_TEST_EQ(df.data().size(), 1000u);
            BOOST_TEST(df.data() == data);
        }));

        // Empty stream
        BOOST_TEST(test::fuse().armed([](test::fuse& f) -> task<void>
        {
            test::read_stream rs(f);

            circular_dynamic_buffer_factory df;
            auto& db = df.buffer();
            auto [ec, n] = co_await read(rs, db);
            if(ec)
                co_return;

            BOOST_TEST_EQ(n, 0u);
            BOOST_TEST(df.data().empty());
        }));

        // Custom initial_amount
        BOOST_TEST(test::fuse().armed([](test::fuse& f) -> task<void>
        {
            test::read_stream rs(f);
            rs.provide("tiny");

            circular_dynamic_buffer_factory df;
            auto& db = df.buffer();
            auto [ec, n] = co_await read(rs, db, 128);
            if(ec)
                co_return;

            BOOST_TEST_EQ(n, 4u);
            BOOST_TEST_EQ(df.data(), "tiny");
        }));
    }

    void
    testStreamDynBuf()
    {
        testStreamDynBufString();
        testStreamDynBufCircular();
    }

    //----------------------------------------------------------

    void
    run()
    {
        testReadStream();
        testReadSource();
        testStreamDynBuf();
    }
};

TEST_SUITE(
    read_test,
    "boost.capy.read");

} // namespace capy
} // namespace boost
