//
// Copyright (c) 2026 Michael Vandeberg
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Test that header file is self-contained.
#include <boost/capy/read_at_least.hpp>

#include <boost/capy/buffers/make_buffer.hpp>
#include <boost/capy/cond.hpp>
#include <boost/capy/error.hpp>
#include <boost/capy/test/fuse.hpp>
#include <boost/capy/test/read_stream.hpp>

#include "test_suite.hpp"

#include <array>
#include <cstring>
#include <string_view>
#include <system_error>

namespace boost {
namespace capy {

namespace {

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
    view(std::size_t n) const
    {
        return std::string_view(storage, n);
    }
};

} // namespace

// Mock whose read_some reports a contingency in the SAME completion that
// transfers bytes. The test read_stream cannot do this (it reports errors
// and eof with zero bytes), so it is needed to exercise the
// "request satisfied but ec set" boundary.
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

struct read_at_least_test
{
    void
    testSatisfiedExact()
    {
        // A single read delivers exactly n; request satisfied.
        BOOST_TEST(test::fuse().armed([](test::fuse& f) -> task<void>
        {
            test::read_stream rs(f);
            rs.provide("hello");

            single_buffer_factory bf(5);
            auto [ec, n] = co_await read_at_least(rs, bf.buffer(), 5);
            if(ec)
                co_return;

            BOOST_TEST_EQ(n, 5u);
            BOOST_TEST_EQ(bf.view(n), "hello");
        }));
    }

    void
    testSatisfiedWithBonus()
    {
        // A single read delivers more than n (up to buffer capacity);
        // the extra bytes are kept and no further read is performed.
        BOOST_TEST(test::fuse().armed([](test::fuse& f) -> task<void>
        {
            test::read_stream rs(f);
            rs.provide("hello world");  // 11 bytes available

            single_buffer_factory bf(32);  // generous capacity
            auto [ec, n] = co_await read_at_least(rs, bf.buffer(), 5);
            if(ec)
                co_return;

            // One read_some returns all 11 available bytes; 11 >= 5,
            // so we stop with the bonus bytes included.
            BOOST_TEST_EQ(n, 11u);
            BOOST_TEST_EQ(bf.view(n), "hello world");
        }));
    }

    void
    testLoopUntilN()
    {
        // Chunked delivery forces multiple reads to reach n.
        BOOST_TEST(test::fuse().armed([](test::fuse& f) -> task<void>
        {
            test::read_stream rs(f, /*max_read_size*/ 4);
            rs.provide("abcdefghij");  // 10 bytes, delivered 4 at a time

            single_buffer_factory bf(32);
            auto [ec, n] = co_await read_at_least(rs, bf.buffer(), 10);
            if(ec)
                co_return;

            // reads of 4 + 4 + 2 (data exhausted) -> 10 >= 10
            BOOST_TEST_EQ(n, 10u);
            BOOST_TEST_EQ(bf.view(n), "abcdefghij");
        }));
    }

    void
    testZeroMinimum()
    {
        // n == 0 returns immediately without awaiting read_some.
        BOOST_TEST(test::fuse().armed([](test::fuse& f) -> task<void>
        {
            test::read_stream rs(f);
            rs.provide("data");

            single_buffer_factory bf(32);
            auto [ec, n] = co_await read_at_least(rs, bf.buffer(), 0);
            if(ec)
                co_return;

            BOOST_TEST_EQ(n, 0u);
            // Stream was not consumed.
            BOOST_TEST_EQ(rs.available(), 4u);
        }));
    }

    void
    testImpossibleRequest()
    {
        // n > buffer_size(buffers) fails immediately with EINVAL and
        // does not touch the stream.
        BOOST_TEST(test::fuse().inert([](test::fuse&) -> task<void>
        {
            test::read_stream rs;
            rs.provide("plenty of data here");

            single_buffer_factory bf(8);
            auto [ec, n] = co_await read_at_least(rs, bf.buffer(), 16);
            BOOST_TEST(ec == std::errc::invalid_argument);
            BOOST_TEST_EQ(n, 0u);
            // Stream was not consumed.
            BOOST_TEST_EQ(rs.available(), 19u);
        }));
    }

    void
    testEofBeforeN()
    {
        // EOF before n bytes are read reports the condition with the
        // partial count. Use an inert fuse so only the real-EOF path is
        // exercised (an armed fuse would inject a different error).
        BOOST_TEST(test::fuse().inert([](test::fuse& f) -> task<void>
        {
            test::read_stream rs(f);
            rs.provide("abc");  // only 3 bytes

            single_buffer_factory bf(32);
            auto [ec, n] = co_await read_at_least(rs, bf.buffer(), 10);

            BOOST_TEST(ec == cond::eof);
            BOOST_TEST_EQ(n, 3u);
            BOOST_TEST_EQ(bf.view(n), "abc");
        }));
    }

    void
    testContingencyCoincidentWithN()
    {
        // A contingency on the read that reaches n is a success
        // (count >= n); a contingency on a short transfer is reported.

        // eof coincident with reaching n -> success
        BOOST_TEST(test::fuse().inert([](test::fuse&) -> task<void>
        {
            contingent_read_stream rs{error::eof, 8};
            single_buffer_factory bf(8);
            auto [ec, n] = co_await read_at_least(rs, bf.buffer(), 8);
            BOOST_TEST(! ec);
            BOOST_TEST_EQ(n, 8u);
        }));

        // contingency with a short transfer -> reported
        BOOST_TEST(test::fuse().inert([](test::fuse&) -> task<void>
        {
            contingent_read_stream rs{error::eof, 5};
            single_buffer_factory bf(8);
            auto [ec, n] = co_await read_at_least(rs, bf.buffer(), 8);
            BOOST_TEST(ec == cond::eof);
            BOOST_TEST_EQ(n, 5u);
        }));
    }

    // Regression: capy#263. read_at_least() must take its buffer
    // sequence by value so that storing the returned awaitable past
    // the full-expression that created the sequence does not dangle.
    void
    testStoredAwaitableTemporarySequence()
    {
        BOOST_TEST(test::fuse().armed([](test::fuse& f) -> task<void>
        {
            test::read_stream rs(f);
            rs.provide("helloworld");

            char storage[10] = {};

            auto aw = read_at_least(rs, std::array<mutable_buffer, 2>{{
                mutable_buffer(storage, 5),
                mutable_buffer(storage + 5, 5)
            }}, 10);

            auto [ec, n] = co_await std::move(aw);
            if(ec)
                co_return;

            BOOST_TEST_EQ(n, 10u);
            BOOST_TEST_EQ(std::string_view(storage, 10), "helloworld");
        }));
    }

    void
    run()
    {
        testSatisfiedExact();
        testSatisfiedWithBonus();
        testLoopUntilN();
        testZeroMinimum();
        testImpossibleRequest();
        testEofBeforeN();
        testContingencyCoincidentWithN();
        testStoredAwaitableTemporarySequence();
    }
};

TEST_SUITE(
    read_at_least_test,
    "boost.capy.read_at_least");

} // namespace capy
} // namespace boost
