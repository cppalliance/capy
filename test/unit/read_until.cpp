//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Test that header file is self-contained.
#include <boost/capy/read_until.hpp>

#include <boost/capy/buffers/string_dynamic_buffer.hpp>
#include <boost/capy/cond.hpp>
#include <boost/capy/error.hpp>
#include <boost/capy/test/fuse.hpp>
#include <boost/capy/test/read_stream.hpp>

#include "test_suite.hpp"

#include <cstring>
#include <span>
#include <string>
#include <string_view>

namespace boost {
namespace capy {

struct read_until_test
{
    //----------------------------------------------------------
    // Fast path tests (delimiter already in buffer)
    //----------------------------------------------------------

    void
    testFastPath()
    {
        // Delimiter already in buffer - no I/O needed
        BOOST_TEST(test::fuse().armed([](test::fuse& f) -> task<void>
        {
            test::read_stream rs(f);
            // Don't provide any data - buffer is pre-filled

            std::string data = "hello\r\nworld\r\n";
            auto [ec, n] = co_await read_until(rs, dynamic_buffer(data), "\r\n");
            if(ec)
                co_return;

            BOOST_TEST_EQ(n, 7u);  // "hello\r\n"
        }));

        // Multiple delimiters in buffer - second call also fast
        BOOST_TEST(test::fuse().armed([](test::fuse& f) -> task<void>
        {
            test::read_stream rs(f);

            std::string data = "line1\nline2\nline3\n";
            string_dynamic_buffer db(&data);

            // First read_until
            auto [ec1, n1] = co_await read_until(rs, db, "\n");
            if(ec1)
                co_return;
            BOOST_TEST_EQ(n1, 6u);  // "line1\n"

            // Consume first line
            db.consume(n1);

            // Second read_until - should also be fast path
            auto [ec2, n2] = co_await read_until(rs, db, "\n");
            if(ec2)
                co_return;
            BOOST_TEST_EQ(n2, 6u);  // "line2\n"
        }));

        // Empty delimiter returns 0 immediately
        BOOST_TEST(test::fuse().armed([](test::fuse& f) -> task<void>
        {
            test::read_stream rs(f);

            std::string data = "some data";
            auto [ec, n] = co_await read_until(rs, dynamic_buffer(data), "");
            if(ec)
                co_return;

            BOOST_TEST_EQ(n, 0u);
        }));
    }

    //----------------------------------------------------------
    // Basic read tests
    //----------------------------------------------------------

    void
    testBasicRead()
    {
        // Delimiter found in first read
        BOOST_TEST(test::fuse().armed([](test::fuse& f) -> task<void>
        {
            test::read_stream rs(f);
            rs.provide("hello\r\nworld");

            std::string data;
            auto [ec, n] = co_await read_until(rs, dynamic_buffer(data), "\r\n");
            if(ec)
                co_return;

            BOOST_TEST_EQ(n, 7u);
            BOOST_TEST_EQ(data.substr(0, n), "hello\r\n");
        }));

        // Single character delimiter
        BOOST_TEST(test::fuse().armed([](test::fuse& f) -> task<void>
        {
            test::read_stream rs(f);
            rs.provide("abc:def:ghi");

            std::string data;
            auto [ec, n] = co_await read_until(rs, dynamic_buffer(data), ":");
            if(ec)
                co_return;

            BOOST_TEST_EQ(n, 4u);
            BOOST_TEST_EQ(data.substr(0, n), "abc:");
        }));

        // Delimiter at very beginning
        BOOST_TEST(test::fuse().armed([](test::fuse& f) -> task<void>
        {
            test::read_stream rs(f);
            rs.provide("\r\nhello");

            std::string data;
            auto [ec, n] = co_await read_until(rs, dynamic_buffer(data), "\r\n");
            if(ec)
                co_return;

            BOOST_TEST_EQ(n, 2u);
        }));

        // Delimiter at exact end of data
        BOOST_TEST(test::fuse().armed([](test::fuse& f) -> task<void>
        {
            test::read_stream rs(f);
            rs.provide("hello\r\n");

            std::string data;
            auto [ec, n] = co_await read_until(rs, dynamic_buffer(data), "\r\n");
            if(ec)
                co_return;

            BOOST_TEST_EQ(n, 7u);
            BOOST_TEST_EQ(data, "hello\r\n");
        }));
    }

    //----------------------------------------------------------
    // Chunked read tests (delimiter spans reads)
    //----------------------------------------------------------

    void
    testChunkedRead()
    {
        // Delimiter spans multiple reads
        BOOST_TEST(test::fuse().armed([](test::fuse& f) -> task<void>
        {
            test::read_stream rs(f, 3);  // max 3 bytes per read
            rs.provide("hello\r\nworld");

            std::string data;
            auto [ec, n] = co_await read_until(rs, dynamic_buffer(data), "\r\n");
            if(ec)
                co_return;

            BOOST_TEST_EQ(n, 7u);
            BOOST_TEST_EQ(data.substr(0, n), "hello\r\n");
        }));

        // Very small chunks, delimiter split across boundary
        BOOST_TEST(test::fuse().armed([](test::fuse& f) -> task<void>
        {
            test::read_stream rs(f, 1);  // 1 byte per read
            rs.provide("ab\r\ncd");

            std::string data;
            auto [ec, n] = co_await read_until(rs, dynamic_buffer(data), "\r\n");
            if(ec)
                co_return;

            BOOST_TEST_EQ(n, 4u);
            BOOST_TEST_EQ(data.substr(0, n), "ab\r\n");
        }));

        // Long delimiter spanning reads
        BOOST_TEST(test::fuse().armed([](test::fuse& f) -> task<void>
        {
            test::read_stream rs(f, 2);
            rs.provide("helloENDMARKworld");

            std::string data;
            auto [ec, n] = co_await read_until(rs, dynamic_buffer(data), "ENDMARK");
            if(ec)
                co_return;

            BOOST_TEST_EQ(n, 12u);  // "helloENDMARK"
        }));

        // Lvalue (non-owning) buffer with a chunked read: the
        // delimiter is not present up front, so the inner coroutine
        // runs and dereferences the pointer-stored buffer.
        BOOST_TEST(test::fuse().armed([](test::fuse& f) -> task<void>
        {
            test::read_stream rs(f, 3);  // max 3 bytes per read
            rs.provide("hello\r\nworld");

            std::string data;
            string_dynamic_buffer db(&data);  // lvalue: pointer storage
            auto [ec, n] = co_await read_until(rs, db, "\r\n");
            if(ec)
                co_return;

            BOOST_TEST_EQ(n, 7u);
            BOOST_TEST_EQ(data.substr(0, n), "hello\r\n");
        }));
    }

    //----------------------------------------------------------
    // Error condition tests
    //----------------------------------------------------------

    void
    testErrorConditions()
    {
        // EOF before delimiter found
        BOOST_TEST(test::fuse().armed([](test::fuse& f) -> task<void>
        {
            test::read_stream rs(f);
            rs.provide("no delimiter here");

            std::string data;
            auto [ec, n] = co_await read_until(rs, dynamic_buffer(data), "\r\n");
            if(ec && ec != cond::eof)
                co_return;
            BOOST_TEST(ec == cond::eof);
            BOOST_TEST_EQ(n, 17u);  // All data read
            BOOST_TEST_EQ(data, "no delimiter here");
        }));

        // max_size reached before delimiter
        BOOST_TEST(test::fuse().armed([](test::fuse& f) -> task<void>
        {
            test::read_stream rs(f);
            rs.provide("this is a very long string without the delimiter");

            std::string data;
            auto [ec, n] = co_await read_until(
                rs, dynamic_buffer(data, 10), "\r\n");  // max 10 bytes
            if(ec && ec != cond::not_found)
                co_return;
            BOOST_TEST(ec == cond::not_found);
            BOOST_TEST_EQ(n, 0u);
        }));

        // Delimiter not in data at all, small max_size
        BOOST_TEST(test::fuse().armed([](test::fuse& f) -> task<void>
        {
            test::read_stream rs(f);
            rs.provide("aaaaaaaaaaaaaaaaaaaa");

            std::string data;
            auto [ec, n] = co_await read_until(
                rs, dynamic_buffer(data, 5), "XXX");
            if(ec && ec != cond::not_found)
                co_return;
            BOOST_TEST(ec == cond::not_found);
            BOOST_TEST_EQ(n, 0u);
        }));

        // Empty stream
        BOOST_TEST(test::fuse().armed([](test::fuse& f) -> task<void>
        {
            test::read_stream rs(f);
            // No data provided

            std::string data;
            auto [ec, n] = co_await read_until(rs, dynamic_buffer(data), "\n");
            if(ec && ec != cond::eof)
                co_return;
            BOOST_TEST(ec == cond::eof);
            BOOST_TEST_EQ(n, 0u);
        }));
    }

    //----------------------------------------------------------
    // Pre-filled buffer tests
    //----------------------------------------------------------

    void
    testPrefilledBuffer()
    {
        // Buffer has partial data, delimiter comes from stream
        BOOST_TEST(test::fuse().armed([](test::fuse& f) -> task<void>
        {
            test::read_stream rs(f);
            rs.provide("llo\r\nworld");

            std::string data = "he";  // Pre-fill with "he"
            auto [ec, n] = co_await read_until(rs, dynamic_buffer(data), "\r\n");
            if(ec)
                co_return;

            BOOST_TEST_EQ(n, 7u);  // "hello\r\n"
            BOOST_TEST_EQ(data.substr(0, n), "hello\r\n");
        }));

        // Delimiter partially in buffer, completed by stream
        BOOST_TEST(test::fuse().armed([](test::fuse& f) -> task<void>
        {
            test::read_stream rs(f);
            rs.provide("\nrest");

            std::string data = "line\r";  // Has \r but not \n
            auto [ec, n] = co_await read_until(rs, dynamic_buffer(data), "\r\n");
            if(ec)
                co_return;

            BOOST_TEST_EQ(n, 6u);  // "line\r\n"
        }));
    }

    //----------------------------------------------------------
    // MatchCondition tests
    //----------------------------------------------------------

    void
    testMatchCondition()
    {
        // Custom matcher: find double newline (HTTP header end)
        auto match_header_end = [](
            std::string_view data,
            std::size_t* hint) -> std::size_t
        {
            auto pos = data.find("\r\n\r\n");
            if(pos != std::string_view::npos)
                return pos + 4;
            if(hint)
                *hint = 3;  // Partial "\r\n\r" possible
            return std::string_view::npos;
        };

        // Basic match condition usage
        BOOST_TEST(test::fuse().armed([&](test::fuse& f) -> task<void>
        {
            test::read_stream rs(f);
            rs.provide("GET / HTTP/1.1\r\nHost: example.com\r\n\r\nbody");

            std::string data;
            auto [ec, n] = co_await read_until(
                rs, dynamic_buffer(data), match_header_end);
            if(ec)
                co_return;

            BOOST_TEST_EQ(n, 37u);  // Headers including \r\n\r\n
        }));

        // Match condition with chunked reads
        BOOST_TEST(test::fuse().armed([&](test::fuse& f) -> task<void>
        {
            test::read_stream rs(f, 5);  // 5 bytes at a time
            rs.provide("A\r\n\r\nB");

            std::string data;
            auto [ec, n] = co_await read_until(
                rs, dynamic_buffer(data), match_header_end);
            if(ec)
                co_return;

            BOOST_TEST_EQ(n, 5u);
        }));

        // Match condition: delimiter not found (EOF)
        BOOST_TEST(test::fuse().armed([&](test::fuse& f) -> task<void>
        {
            test::read_stream rs(f);
            rs.provide("partial\r\n");

            std::string data;
            auto [ec, n] = co_await read_until(
                rs, dynamic_buffer(data), match_header_end);
            if(ec && ec != cond::eof)
                co_return;
            BOOST_TEST(ec == cond::eof);
        }));

        // Match at position 0 (empty match returns immediately)
        auto match_zero = [](std::string_view, std::size_t*) -> std::size_t
        {
            return 0;  // Always returns position 0
        };

        BOOST_TEST(test::fuse().armed([&](test::fuse& f) -> task<void>
        {
            test::read_stream rs(f);

            std::string data = "anything";
            auto [ec, n] = co_await read_until(
                rs, dynamic_buffer(data), match_zero);
            if(ec)
                co_return;

            BOOST_TEST_EQ(n, 0u);
        }));

        // Stateful matcher: count occurrences
        struct match_nth_newline
        {
            int target;
            mutable int count = 0;

            std::size_t
            operator()(std::string_view data, std::size_t* hint) const
            {
                count = 0;
                for(std::size_t i = 0; i < data.size(); ++i)
                {
                    if(data[i] == '\n')
                    {
                        ++count;
                        if(count == target)
                            return i + 1;
                    }
                }
                if(hint)
                    *hint = 0;
                return std::string_view::npos;
            }
        };

        BOOST_TEST(test::fuse().armed([](test::fuse& f) -> task<void>
        {
            test::read_stream rs(f);
            rs.provide("line1\nline2\nline3\nline4\n");

            std::string data;
            auto [ec, n] = co_await read_until(
                rs, dynamic_buffer(data), match_nth_newline{3});
            if(ec)
                co_return;

            BOOST_TEST_EQ(n, 18u);  // Up to third \n
        }));
    }

    //----------------------------------------------------------

    void
    testMatchHelpers()
    {
        // A multi-buffer sequence forces linearize_buffers and the
        // multi-buffer branch of search_buffer_for_match, which the
        // contiguous string_dynamic_buffer never reaches.
        const_buffer bufs[2] = {
            const_buffer("hel", 3),
            const_buffer("lo\r\nx", 5)
        };
        std::span<const_buffer> seq(bufs, 2);

        BOOST_TEST_EQ(detail::linearize_buffers(seq), "hello\r\nx");

        match_delim m{"\r\n"};
        BOOST_TEST_EQ(detail::search_buffer_for_match(seq, m), 7u);

        // On a miss, a multi-character delimiter records an overlap
        // hint of delim.size() - 1.
        std::size_t hint = 999;
        BOOST_TEST(m("partial te", &hint) == std::string_view::npos);
        BOOST_TEST_EQ(hint, 1u);
    }

    void
    run()
    {
        testFastPath();
        testBasicRead();
        testChunkedRead();
        testErrorConditions();
        testPrefilledBuffer();
        testMatchCondition();
        testMatchHelpers();
    }
};

TEST_SUITE(
    read_until_test,
    "boost.capy.read_until");

} // namespace capy
} // namespace boost
