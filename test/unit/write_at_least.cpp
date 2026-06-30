//
// Copyright (c) 2026 Michael Vandeberg
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Test that header file is self-contained.
#include <boost/capy/write_at_least.hpp>

#include <boost/capy/buffers/make_buffer.hpp>
#include <boost/capy/cond.hpp>
#include <boost/capy/error.hpp>
#include <boost/capy/test/fuse.hpp>
#include <boost/capy/test/write_stream.hpp>

#include "test_suite.hpp"

#include <array>
#include <string>
#include <string_view>
#include <system_error>

namespace boost {
namespace capy {

namespace {

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
};

} // namespace

// Mock whose write_some reports a contingency in the SAME completion that
// transfers bytes, to exercise the "request satisfied but ec set"
// boundary. The test write_stream reports errors with zero bytes.
struct contingent_write_stream
{
    std::error_code ec;
    std::size_t accept;

    template<ConstBufferSequence CB>
    auto
    write_some(CB buffers)
    {
        struct awaitable
        {
            contingent_write_stream* self_;
            CB buffers_;

            bool await_ready() const noexcept { return true; }

            void await_suspend(
                std::coroutine_handle<>, io_env const*) const noexcept {}

            io_result<std::size_t>
            await_resume()
            {
                std::size_t const cap = buffer_size(buffers_);
                std::size_t const n =
                    self_->accept < cap ? self_->accept : cap;
                self_->accept -= n;
                return {self_->ec, n};
            }
        };
        return awaitable{this, buffers};
    }
};

struct write_at_least_test
{
    void
    testSatisfiedExact()
    {
        // A single write transfers exactly n; request satisfied.
        BOOST_TEST(test::fuse().armed([](test::fuse& f) -> task<void>
        {
            test::write_stream ws(f);

            single_buffer_factory bf("hello");
            auto [ec, n] = co_await write_at_least(ws, bf.buffer(), 5);
            if(ec)
                co_return;

            BOOST_TEST_EQ(n, 5u);
            BOOST_TEST_EQ(ws.data(), "hello");
        }));
    }

    void
    testSatisfiedWithBonus()
    {
        // A single write transfers more than n; the extra bytes are
        // counted and no further write is performed.
        BOOST_TEST(test::fuse().armed([](test::fuse& f) -> task<void>
        {
            test::write_stream ws(f);

            single_buffer_factory bf("hello world");  // 11 bytes
            auto [ec, n] = co_await write_at_least(ws, bf.buffer(), 5);
            if(ec)
                co_return;

            // One write_some transfers all 11 bytes; 11 >= 5.
            BOOST_TEST_EQ(n, 11u);
            BOOST_TEST_EQ(ws.data(), "hello world");
        }));
    }

    void
    testLoopUntilN()
    {
        // Chunked delivery forces multiple writes to reach n, and the
        // loop stops before the whole buffer is consumed.
        BOOST_TEST(test::fuse().armed([](test::fuse& f) -> task<void>
        {
            test::write_stream ws(f, /*max_write_size*/ 4);

            single_buffer_factory bf("abcdefghijklmnopqrst");  // 20 bytes
            auto [ec, n] = co_await write_at_least(ws, bf.buffer(), 10);
            if(ec)
                co_return;

            // writes of 4 + 4 + 4 -> 12 >= 10, then stop.
            BOOST_TEST_EQ(n, 12u);
            BOOST_TEST_EQ(ws.data(), "abcdefghijkl");
        }));
    }

    void
    testZeroMinimum()
    {
        // n == 0 returns immediately without awaiting write_some.
        BOOST_TEST(test::fuse().armed([](test::fuse& f) -> task<void>
        {
            test::write_stream ws(f);

            single_buffer_factory bf("data");
            auto [ec, n] = co_await write_at_least(ws, bf.buffer(), 0);
            if(ec)
                co_return;

            BOOST_TEST_EQ(n, 0u);
            BOOST_TEST(ws.data().empty());
        }));
    }

    void
    testImpossibleRequest()
    {
        // n > buffer_size(buffers) fails immediately with EINVAL and
        // does not touch the stream.
        BOOST_TEST(test::fuse().inert([](test::fuse&) -> task<void>
        {
            test::write_stream ws;

            single_buffer_factory bf("12345678");  // 8 bytes
            auto [ec, n] = co_await write_at_least(ws, bf.buffer(), 16);
            BOOST_TEST(ec == std::errc::invalid_argument);
            BOOST_TEST_EQ(n, 0u);
            BOOST_TEST(ws.data().empty());
        }));
    }

    void
    testContingencyCoincidentWithN()
    {
        // A contingency on the write that reaches n is a success
        // (count >= n); a contingency on a short transfer is reported.

        // contingency coincident with reaching n -> success
        BOOST_TEST(test::fuse().inert([](test::fuse&) -> task<void>
        {
            contingent_write_stream ws{error::canceled, 8};
            single_buffer_factory bf("12345678");
            auto [ec, n] = co_await write_at_least(ws, bf.buffer(), 8);
            BOOST_TEST(! ec);
            BOOST_TEST_EQ(n, 8u);
        }));

        // contingency with a short write -> reported
        BOOST_TEST(test::fuse().inert([](test::fuse&) -> task<void>
        {
            contingent_write_stream ws{error::canceled, 5};
            single_buffer_factory bf("12345678");
            auto [ec, n] = co_await write_at_least(ws, bf.buffer(), 8);
            BOOST_TEST(!! ec);
            BOOST_TEST_EQ(n, 5u);
        }));
    }

    // Regression: capy#263. write_at_least() must take its buffer
    // sequence by value so that storing the returned awaitable past
    // the full-expression that created the sequence does not dangle.
    void
    testStoredAwaitableTemporarySequence()
    {
        BOOST_TEST(test::fuse().armed([](test::fuse& f) -> task<void>
        {
            test::write_stream ws(f);

            char const data1[] = "hello";
            char const data2[] = "world";

            auto aw = write_at_least(ws, std::array<const_buffer, 2>{{
                const_buffer(data1, 5),
                const_buffer(data2, 5)
            }}, 10);

            auto [ec, n] = co_await std::move(aw);
            if(ec)
                co_return;

            BOOST_TEST_EQ(n, 10u);
            BOOST_TEST_EQ(ws.data(), "helloworld");
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
        testContingencyCoincidentWithN();
        testStoredAwaitableTemporarySequence();
    }
};

TEST_SUITE(
    write_at_least_test,
    "boost.capy.write_at_least");

} // namespace capy
} // namespace boost
