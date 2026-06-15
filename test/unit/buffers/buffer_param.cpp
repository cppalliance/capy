//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Test that header file is self-contained.
#include <boost/capy/buffers/buffer_param.hpp>

#include "test_buffers.hpp"

#include <cstring>
#include <span>
#include <string>
#include <vector>

namespace boost {
namespace capy {

namespace {

// Helper to compute total bytes in a span
std::size_t
total_size(std::span<const_buffer> bufs)
{
    std::size_t n = 0;
    for(auto const& b : bufs)
        n += b.size();
    return n;
}

std::size_t
total_size(std::span<mutable_buffer> bufs)
{
    std::size_t n = 0;
    for(auto const& b : bufs)
        n += b.size();
    return n;
}

// Helper to copy data from buffer span to string
std::string
to_string(std::span<const_buffer> bufs)
{
    std::string result;
    for(auto const& b : bufs)
        result.append(
            static_cast<char const*>(b.data()),
            b.size());
    return result;
}

// Helper to consume all data from a buffer_param
template<ConstBufferSequence BS>
std::string
consume_all(buffer_param<BS>& bp)
{
    std::string result;
    while(true)
    {
        auto bufs = bp.data();
        if(bufs.empty())
            break;
        result += to_string(bufs);
        bp.consume(total_size(bufs));
    }
    return result;
}

} // namespace

struct buffer_param_test
{
    void
    testConstSingleBuffer()
    {
        // Single const_buffer
        {
            std::string data = "Hello, World!";
            const_buffer buf(data.data(), data.size());

            buffer_param bp(buf);
            auto result = consume_all(bp);
            BOOST_TEST_EQ(result, data);
        }

        // Empty buffer
        {
            const_buffer buf;
            buffer_param bp(buf);
            auto result = consume_all(bp);
            BOOST_TEST(result.empty());
        }
    }

    void
    testConstMultipleBuffers()
    {
        // Vector of buffers
        {
            std::string s1 = "Hello, ";
            std::string s2 = "World";
            std::string s3 = "!";

            std::vector<const_buffer> bufs = {
                const_buffer(s1.data(), s1.size()),
                const_buffer(s2.data(), s2.size()),
                const_buffer(s3.data(), s3.size())
            };

            buffer_param bp(bufs);
            auto result = consume_all(bp);
            BOOST_TEST_EQ(result, "Hello, World!");
        }

        // With empty buffers interspersed (should be skipped)
        {
            std::string s1 = "AB";
            std::string s2 = "CD";

            std::vector<const_buffer> bufs = {
                const_buffer(),
                const_buffer(s1.data(), s1.size()),
                const_buffer(),
                const_buffer(s2.data(), s2.size()),
                const_buffer()
            };

            buffer_param bp(bufs);
            auto result = consume_all(bp);
            BOOST_TEST_EQ(result, "ABCD");
        }
    }

    void
    testConstPartialConsume()
    {
        std::string data = "0123456789";
        const_buffer buf(data.data(), data.size());

        // Consume 3 bytes at a time
        {
            buffer_param bp(buf);

            auto bufs = bp.data();
            BOOST_TEST_EQ(total_size(bufs), 10u);

            // Consume 3
            bp.consume(3);
            bufs = bp.data();
            BOOST_TEST_EQ(total_size(bufs), 7u);
            BOOST_TEST_EQ(
                static_cast<char const*>(bufs[0].data())[0],
                '3');

            // Consume 4 more
            bp.consume(4);
            bufs = bp.data();
            BOOST_TEST_EQ(total_size(bufs), 3u);
            BOOST_TEST_EQ(
                static_cast<char const*>(bufs[0].data())[0],
                '7');

            // Consume remaining
            bp.consume(3);
            bufs = bp.data();
            BOOST_TEST(bufs.empty());
        }
    }

    void
    testConstLargeSequence()
    {
        // More than 16 buffers (tests window refill)
        std::vector<std::string> strings;
        std::vector<const_buffer> bufs;
        std::string expected;

        for(int i = 0; i < 50; ++i)
        {
            strings.push_back(std::to_string(i) + ",");
            expected += strings.back();
        }

        for(auto const& s : strings)
            bufs.emplace_back(s.data(), s.size());

        buffer_param bp(bufs);
        auto result = consume_all(bp);
        BOOST_TEST_EQ(result, expected);
    }

    void
    testMutableSingleBuffer()
    {
        // Mutable buffer - write into it
        {
            char data[32] = {};
            mutable_buffer buf(data, sizeof(data));

            buffer_param bp(buf);
            auto bufs = bp.data();

            BOOST_TEST_EQ(bufs.size(), 1u);
            BOOST_TEST_EQ(bufs[0].size(), sizeof(data));

            // Write some data
            std::memcpy(bufs[0].data(), "Test", 4);
            bp.consume(4);

            bufs = bp.data();
            BOOST_TEST_EQ(bufs[0].size(), sizeof(data) - 4);

            // Write more
            std::memcpy(bufs[0].data(), "Data", 4);
            bp.consume(4);

            BOOST_TEST_EQ(std::string(data, 8), "TestData");
        }
    }

    void
    testMutableMultipleBuffers()
    {
        char buf1[8] = {};
        char buf2[8] = {};
        char buf3[8] = {};

        std::vector<mutable_buffer> bufs = {
            mutable_buffer(buf1, sizeof(buf1)),
            mutable_buffer(buf2, sizeof(buf2)),
            mutable_buffer(buf3, sizeof(buf3))
        };

        buffer_param bp(bufs);

        // Fill all buffers through the param
        std::size_t total = 0;
        int chunk = 0;
        while(true)
        {
            auto spans = bp.data();
            if(spans.empty())
                break;

            for(auto& b : spans)
            {
                std::memset(b.data(), 'A' + chunk, b.size());
                total += b.size();
            }
            bp.consume(total_size(spans));
            ++chunk;
        }

        BOOST_TEST_EQ(total, 24u);

        // Verify all buffers were written
        BOOST_TEST_EQ(std::string(buf1, 8), "AAAAAAAA");
        BOOST_TEST_EQ(std::string(buf2, 8), "AAAAAAAA");
        BOOST_TEST_EQ(std::string(buf3, 8), "AAAAAAAA");
    }

    void
    testBufferType()
    {
        // Verify buffer_type is correct for const sequences
        {
            std::vector<const_buffer> bufs;
            buffer_param bp(bufs);
            static_assert(std::is_same_v<
                decltype(bp)::buffer_type, const_buffer>);
        }

        // Verify buffer_type is correct for mutable sequences
        {
            std::vector<mutable_buffer> bufs;
            buffer_param bp(bufs);
            static_assert(std::is_same_v<
                decltype(bp)::buffer_type, mutable_buffer>);
        }
    }

    void
    testMore()
    {
        // Single buffer — no more after first window
        {
            std::string data = "Hello";
            const_buffer buf(data.data(), data.size());
            buffer_param bp(buf);

            auto bufs = bp.data();
            BOOST_TEST(! bufs.empty());
            BOOST_TEST(! bp.more());
        }

        // Empty sequence — no more
        {
            std::vector<const_buffer> bufs;
            buffer_param bp(bufs);
            auto d = bp.data();
            BOOST_TEST(d.empty());
            BOOST_TEST(! bp.more());
        }

        // Exactly max_iovec_ buffers — fits in one window
        {
            std::vector<std::string> strings(detail::max_iovec_, "x");
            std::vector<const_buffer> bufs;
            for(auto const& s : strings)
                bufs.emplace_back(s.data(), s.size());

            buffer_param bp(bufs);
            auto d = bp.data();
            BOOST_TEST(! d.empty());
            BOOST_TEST(! bp.more());
        }

        // One more than max_iovec_ — needs two windows
        {
            std::vector<std::string> strings(
                detail::max_iovec_ + 1, "x");
            std::vector<const_buffer> bufs;
            for(auto const& s : strings)
                bufs.emplace_back(s.data(), s.size());

            buffer_param bp(bufs);
            auto d = bp.data();
            BOOST_TEST(! d.empty());
            BOOST_TEST(bp.more());

            bp.consume(total_size(d));
            d = bp.data();
            BOOST_TEST(! d.empty());
            BOOST_TEST(! bp.more());

            bp.consume(total_size(d));
            d = bp.data();
            BOOST_TEST(d.empty());
        }
    }

    void
    testPartialByteConsumption()
    {
        std::string data = "ABCDEFGHIJ";
        const_buffer buf(data.data(), data.size());

        buffer_param bp(buf);
        std::string result;
        std::size_t n = 5;

        while(n > 0)
        {
            auto bufs = bp.data();
            if(bufs.empty())
                break;
            auto chunk = (std::min)(n, total_size(bufs));
            // Copy only 'chunk' bytes
            std::size_t copied = 0;
            for(auto const& b : bufs)
            {
                auto take = (std::min)(b.size(), chunk - copied);
                result.append(
                    static_cast<char const*>(b.data()),
                    take);
                copied += take;
                if(copied >= chunk)
                    break;
            }
            bp.consume(chunk);
            n -= chunk;
        }

        BOOST_TEST_EQ(result, "ABCDE");
    }

    void
    run()
    {
        testConstSingleBuffer();
        testConstMultipleBuffers();
        testConstPartialConsume();
        testConstLargeSequence();
        testMutableSingleBuffer();
        testMutableMultipleBuffers();
        testBufferType();
        testMore();
        testPartialByteConsumption();
    }
};

TEST_SUITE(
    buffer_param_test,
    "boost.capy.buffers.buffer_param");

} // capy
} // boost
