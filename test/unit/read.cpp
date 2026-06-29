//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Test that header file is self-contained.
#include <boost/capy/read.hpp>

#include <boost/capy/buffers/make_buffer.hpp>
#include <boost/capy/cond.hpp>
#include <boost/capy/error.hpp>
#include <boost/capy/test/fuse.hpp>
#include <boost/capy/test/read_stream.hpp>

#include "test_suite.hpp"

#include <array>
#include <cstring>
#include <stdexcept>
#include <string>
#include <string_view>

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

} // namespace

// Mock whose read_some reports a contingency in the SAME completion that
// transfers bytes. The test read_stream cannot do this (it reports errors
// and eof with zero bytes), so it is needed to exercise the
// "buffer filled but ec set" boundary.
struct contingent_read_stream
{
    std::error_code ec;
    std::size_t deliver;

    template<MutableBufferSequence MB>
    auto
    read_some(MB buffers)
    {
        struct awaitable
        {
            contingent_read_stream* self_;
            MB buffers_;

            bool await_ready() const noexcept { return true; }

            void await_suspend(
                std::coroutine_handle<>, io_env const*) const noexcept {}

            io_result<std::size_t>
            await_resume()
            {
                std::size_t const cap = buffer_size(buffers_);
                std::size_t const n =
                    self_->deliver < cap ? self_->deliver : cap;
                self_->deliver -= n;
                return {self_->ec, n};
            }
        };
        return awaitable{this, buffers};
    }
};

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

    // Regression: capy#263. Free-function read() must take its buffer
    // sequence by value so that storing the returned awaitable past
    // the full-expression that created the sequence does not dangle.
    void
    testReadStoredAwaitableTemporarySequence()
    {
        BOOST_TEST(test::fuse().armed([](test::fuse& f) -> task<void>
        {
            test::read_stream rs(f);
            rs.provide("helloworld");

            char storage[10] = {};

            // The std::array<mutable_buffer, 2> argument is a temporary
            // that ends its lifetime at the end of this full-expression.
            auto aw = read(rs, std::array<mutable_buffer, 2>{{
                mutable_buffer(storage, 5),
                mutable_buffer(storage + 5, 5)
            }});

            // If read() bound the sequence by const&, the awaitable now
            // holds a dangling reference and the next line trips ASan
            // (or silently reads stale stack).
            auto [ec, n] = co_await std::move(aw);
            if(ec)
                co_return;

            BOOST_TEST_EQ(n, 10u);
            BOOST_TEST_EQ(std::string_view(storage, 10), "helloworld");
        }));
    }

    void
    testFullTransferContingency()
    {
        // A contingency on the read that fills the buffer is a success
        // (n == buffer_size); a contingency on a short transfer is
        // reported.

        // eof coincident with a full fill -> success
        BOOST_TEST(test::fuse().inert([](test::fuse&) -> task<void>
        {
            contingent_read_stream rs{error::eof, 8};
            single_buffer_factory bf(8);
            auto [ec, n] = co_await read(rs, bf.buffer());
            BOOST_TEST(! ec);
            BOOST_TEST_EQ(n, 8u);
        }));

        // contingency with a short transfer -> reported
        BOOST_TEST(test::fuse().inert([](test::fuse&) -> task<void>
        {
            contingent_read_stream rs{error::eof, 5};
            single_buffer_factory bf(8);
            auto [ec, n] = co_await read(rs, bf.buffer());
            BOOST_TEST(ec == cond::eof);
            BOOST_TEST_EQ(n, 5u);
        }));

        // the suppressed condition is deferred, not lost: the next read
        // surfaces it (here the stream is at eof with no more data).
        BOOST_TEST(test::fuse().inert([](test::fuse&) -> task<void>
        {
            contingent_read_stream rs{error::eof, 8};
            single_buffer_factory bf(8);
            auto [ec1, n1] = co_await read(rs, bf.buffer());
            BOOST_TEST(! ec1);
            BOOST_TEST_EQ(n1, 8u);
            auto [ec2, n2] = co_await read(rs, bf.buffer());
            BOOST_TEST(ec2 == cond::eof);
            BOOST_TEST_EQ(n2, 0u);
        }));
    }

    void
    testReadStream()
    {
        testReadSingleBuffer();
        testReadBufferArray();
        testReadBufferPair();
        testReadStoredAwaitableTemporarySequence();
        testFullTransferContingency();
    }

    //----------------------------------------------------------

    void
    run()
    {
        testReadStream();
    }
};

TEST_SUITE(
    read_test,
    "boost.capy.read");

} // namespace capy
} // namespace boost
