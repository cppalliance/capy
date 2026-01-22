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

#include <boost/capy/error.hpp>
#include <boost/capy/ex/run_async.hpp>

#include "test_helpers.hpp"

#include <array>
#include <cstring>
#include <string>
#include <vector>

namespace boost {
namespace capy {

namespace {

// Mock write awaitable that appends data to a string
struct mock_write_awaitable
{
    std::string* data_;
    std::size_t chunk_size_;
    const_buffer buf_;
    system::error_code ec_;

    bool await_ready() const noexcept { return true; }

    void await_suspend(
        coro,
        executor_ref,
        std::stop_token) const noexcept
    {
    }

    std::pair<system::error_code, std::size_t>
    await_resume() noexcept
    {
        if(ec_)
            return {ec_, 0};

        std::size_t to_write = std::min(buf_.size(), chunk_size_);
        data_->append(
            static_cast<char const*>(buf_.data()),
            to_write);
        return {{}, to_write};
    }
};

// Mock stream that writes to a string
struct mock_write_stream
{
    std::string data;
    std::size_t chunk_size = 1024;
    system::error_code forced_error;

    template<ConstBufferSequence CB>
    mock_write_awaitable
    write_some(CB const& buffers)
    {
        const_buffer buf = *begin(buffers);
        return {&data, chunk_size, buf, forced_error};
    }
};

static_assert(WriteStream<mock_write_stream>);

// Mock stream that returns error after N bytes
struct mock_error_stream
{
    std::string data;
    std::size_t written = 0;
    std::size_t error_after = 0;
    system::error_code error_to_return;

    template<ConstBufferSequence CB>
    auto write_some(CB const& buffers)
    {
        struct awaitable
        {
            mock_error_stream* self_;
            const_buffer buf_;

            bool await_ready() const noexcept { return true; }

            void await_suspend(
                coro,
                executor_ref,
                std::stop_token) const noexcept
            {
            }

            std::pair<system::error_code, std::size_t>
            await_resume() noexcept
            {
                if(self_->written >= self_->error_after)
                    return {self_->error_to_return, 0};

                std::size_t can_write = self_->error_after - self_->written;
                std::size_t to_write = std::min(buf_.size(), can_write);

                self_->data.append(
                    static_cast<char const*>(buf_.data()),
                    to_write);
                self_->written += to_write;
                return {{}, to_write};
            }
        };

        const_buffer buf = *begin(buffers);
        return awaitable{this, buf};
    }
};

static_assert(WriteStream<mock_error_stream>);

} // namespace

struct write_test
{
    void
    testWriteSingleBuffer()
    {
        int dispatch_count = 0;
        test_executor ex(dispatch_count);
        bool completed = false;

        auto do_test = []() -> task<void>
        {
            mock_write_stream stream;

            std::string_view msg = "hello world";
            auto [ec, n] = co_await write(stream, const_buffer(msg.data(), msg.size()));

            BOOST_TEST(!ec);
            BOOST_TEST_EQ(n, 11u);
            BOOST_TEST_EQ(stream.data, "hello world");
        };

        run_async(ex,
            [&]() { completed = true; },
            [](std::exception_ptr) {})(do_test());

        BOOST_TEST(completed);
    }

    void
    testWriteExactSize()
    {
        int dispatch_count = 0;
        test_executor ex(dispatch_count);
        bool completed = false;

        auto do_test = []() -> task<void>
        {
            mock_write_stream stream;

            std::string_view msg = "exact";
            auto [ec, n] = co_await write(stream, const_buffer(msg.data(), msg.size()));

            BOOST_TEST(!ec);
            BOOST_TEST_EQ(n, 5u);
            BOOST_TEST_EQ(stream.data, "exact");
        };

        run_async(ex,
            [&]() { completed = true; },
            [](std::exception_ptr) {})(do_test());

        BOOST_TEST(completed);
    }

    void
    testWriteWithChunking()
    {
        int dispatch_count = 0;
        test_executor ex(dispatch_count);
        bool completed = false;

        auto do_test = []() -> task<void>
        {
            mock_write_stream stream;
            stream.chunk_size = 3;

            std::string_view msg = "abcdefghij";
            auto [ec, n] = co_await write(stream, const_buffer(msg.data(), msg.size()));

            BOOST_TEST(!ec);
            BOOST_TEST_EQ(n, 10u);
            BOOST_TEST_EQ(stream.data, "abcdefghij");
        };

        run_async(ex,
            [&]() { completed = true; },
            [](std::exception_ptr) {})(do_test());

        BOOST_TEST(completed);
    }

    void
    testWriteEmptyBuffer()
    {
        int dispatch_count = 0;
        test_executor ex(dispatch_count);
        bool completed = false;

        auto do_test = []() -> task<void>
        {
            mock_write_stream stream;

            auto [ec, n] = co_await write(stream, const_buffer(nullptr, 0));

            BOOST_TEST(!ec);
            BOOST_TEST_EQ(n, 0u);
            BOOST_TEST(stream.data.empty());
        };

        run_async(ex,
            [&]() { completed = true; },
            [](std::exception_ptr) {})(do_test());

        BOOST_TEST(completed);
    }

    void
    testWriteBufferSequence()
    {
        int dispatch_count = 0;
        test_executor ex(dispatch_count);
        bool completed = false;

        auto do_test = []() -> task<void>
        {
            mock_write_stream stream;
            stream.chunk_size = 100;

            std::string_view msg1 = "hello";
            std::string_view msg2 = "world";
            std::array<const_buffer, 2> buffers = {{
                const_buffer(msg1.data(), msg1.size()),
                const_buffer(msg2.data(), msg2.size())
            }};

            auto [ec, n] = co_await write(stream, buffers);

            BOOST_TEST(!ec);
            BOOST_TEST_EQ(n, 10u);
            BOOST_TEST_EQ(stream.data, "helloworld");
        };

        run_async(ex,
            [&]() { completed = true; },
            [](std::exception_ptr) {})(do_test());

        BOOST_TEST(completed);
    }

    void
    testWriteErrorMidway()
    {
        int dispatch_count = 0;
        test_executor ex(dispatch_count);
        bool completed = false;

        auto do_test = []() -> task<void>
        {
            mock_error_stream stream;
            stream.error_after = 5;
            stream.error_to_return = make_error_code(system::errc::io_error);

            std::string_view msg = "abcdefghij";
            auto [ec, n] = co_await write(stream, const_buffer(msg.data(), msg.size()));

            BOOST_TEST(ec == system::errc::io_error);
            BOOST_TEST_EQ(n, 5u);
            BOOST_TEST_EQ(stream.data, "abcde");
        };

        run_async(ex,
            [&]() { completed = true; },
            [](std::exception_ptr) {})(do_test());

        BOOST_TEST(completed);
    }

    void
    testWriteImmediateError()
    {
        int dispatch_count = 0;
        test_executor ex(dispatch_count);
        bool completed = false;

        auto do_test = []() -> task<void>
        {
            mock_write_stream stream;
            stream.forced_error = make_error_code(system::errc::broken_pipe);

            std::string_view msg = "data";
            auto [ec, n] = co_await write(stream, const_buffer(msg.data(), msg.size()));

            BOOST_TEST(ec == system::errc::broken_pipe);
            BOOST_TEST_EQ(n, 0u);
            BOOST_TEST(stream.data.empty());
        };

        run_async(ex,
            [&]() { completed = true; },
            [](std::exception_ptr) {})(do_test());

        BOOST_TEST(completed);
    }

    void
    testWriteLargeData()
    {
        int dispatch_count = 0;
        test_executor ex(dispatch_count);
        bool completed = false;

        auto do_test = []() -> task<void>
        {
            mock_write_stream stream;
            stream.chunk_size = 100;

            std::string large_data(1000, 'x');
            auto [ec, n] = co_await write(stream, const_buffer(large_data.data(), large_data.size()));

            BOOST_TEST(!ec);
            BOOST_TEST_EQ(n, 1000u);
            BOOST_TEST_EQ(stream.data.size(), 1000u);
            BOOST_TEST(stream.data == large_data);
        };

        run_async(ex,
            [&]() { completed = true; },
            [](std::exception_ptr) {})(do_test());

        BOOST_TEST(completed);
    }

    void
    run()
    {
        testWriteSingleBuffer();
        testWriteExactSize();
        testWriteWithChunking();
        testWriteEmptyBuffer();
        testWriteBufferSequence();
        testWriteErrorMidway();
        testWriteImmediateError();
        testWriteLargeData();
    }
};

TEST_SUITE(
    write_test,
    "boost.capy.write");

} // namespace capy
} // namespace boost
