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

#include <boost/capy/buffers/make_buffer.hpp>
#include <boost/capy/buffers/string_dynamic_buffer.hpp>
#include <boost/capy/concept/read_source.hpp>
#include <boost/capy/cond.hpp>
#include <boost/capy/error.hpp>
#include <boost/capy/io_result.hpp>
#include <boost/capy/ex/run_async.hpp>

#include "test_helpers.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <string>
#include <vector>

namespace boost {
namespace capy {

namespace {

// Mock read awaitable that returns data from a string
struct mock_read_awaitable
{
    std::string* data_;
    std::size_t* pos_;
    std::size_t chunk_size_;
    mutable_buffer buf_;
    system::error_code ec_;

    bool await_ready() const noexcept { return true; }

    void await_suspend(
        coro,
        executor_ref,
        std::stop_token) const noexcept
    {
    }

    io_result<std::size_t>
    await_resume() noexcept
    {
        if(ec_)
            return {ec_, 0};

        std::size_t remaining = data_->size() - *pos_;
        if(remaining == 0)
            return {error::eof, 0};

        std::size_t to_read = (std::min)({
            buf_.size(),
            chunk_size_,
            remaining});

        std::memcpy(buf_.data(), data_->data() + *pos_, to_read);
        *pos_ += to_read;
        return {{}, to_read};
    }
};

// Mock stream that reads from a string
struct mock_read_stream
{
    std::string data;
    std::size_t pos = 0;
    std::size_t chunk_size = 1024;
    system::error_code forced_error;

    template<MutableBufferSequence MB>
    mock_read_awaitable
    read_some(MB const& buffers)
    {
        mutable_buffer buf = *begin(buffers);
        return {&data, &pos, chunk_size, buf, forced_error};
    }
};

static_assert(ReadStream<mock_read_stream>);

// Mock stream that returns error after N bytes
struct mock_error_stream
{
    std::string data;
    std::size_t pos = 0;
    std::size_t error_after = 0;
    system::error_code error_to_return;

    template<MutableBufferSequence MB>
    auto read_some(MB const& buffers)
    {
        struct awaitable
        {
            mock_error_stream* self_;
            mutable_buffer buf_;

            bool await_ready() const noexcept { return true; }

            void await_suspend(
                coro,
                executor_ref,
                std::stop_token) const noexcept
            {
            }

            io_result<std::size_t>
            await_resume() noexcept
            {
                if(self_->pos >= self_->error_after)
                    return {self_->error_to_return, 0};

                std::size_t remaining = self_->data.size() - self_->pos;
                if(remaining == 0)
                    return {error::eof, 0};

                std::size_t can_read = self_->error_after - self_->pos;
                std::size_t to_read = std::min({buf_.size(), remaining, can_read});

                std::memcpy(buf_.data(), self_->data.data() + self_->pos, to_read);
                self_->pos += to_read;
                return {{}, to_read};
            }
        };

        mutable_buffer buf = *begin(buffers);
        return awaitable{this, buf};
    }
};

static_assert(ReadStream<mock_error_stream>);

//----------------------------------------------------------
// Mock ReadSource for testing
//----------------------------------------------------------

// Mock source awaitable that returns data from a string
struct mock_source_awaitable
{
    std::string* data_;
    std::size_t* pos_;
    mutable_buffer buf_;
    system::error_code ec_;

    bool await_ready() const noexcept { return true; }

    void await_suspend(
        coro,
        executor_ref,
        std::stop_token) const noexcept
    {
    }

    io_result<std::size_t>
    await_resume() noexcept
    {
        if(ec_)
            return {ec_, 0};

        // Empty buffer completes immediately with success
        if(buf_.size() == 0)
            return {{}, 0};

        std::size_t remaining = data_->size() - *pos_;
        if(remaining == 0)
            return {error::eof, 0};

        std::size_t to_read = std::min(buf_.size(), remaining);
        std::memcpy(buf_.data(), data_->data() + *pos_, to_read);
        *pos_ += to_read;

        if(*pos_ >= data_->size())
            return {error::eof, to_read};

        return {{}, to_read};
    }
};

// Mock source that implements ReadSource concept
struct mock_read_source
{
    std::string data;
    std::size_t pos = 0;
    system::error_code forced_error;

    template<MutableBufferSequence MB>
    mock_source_awaitable
    read(MB const& buffers)
    {
        mutable_buffer buf = *begin(buffers);
        return {&data, &pos, buf, forced_error};
    }
};

static_assert(ReadSource<mock_read_source>);

} // namespace

struct read_test
{
    void
    testReadSingleBuffer()
    {
        int dispatch_count = 0;
        test_executor ex(dispatch_count);
        bool completed = false;

        auto do_test = []() -> task<void>
        {
            mock_read_stream stream;
            stream.data = "hello world";

            char buf[32] = {};
            auto [ec, n] = co_await read(stream, make_buffer(buf, 11));

            BOOST_TEST(!ec);
            BOOST_TEST_EQ(n, 11u);
            BOOST_TEST_EQ(std::string_view(buf, n), "hello world");
        };

        run_async(ex,
            [&]() { completed = true; },
            [](std::exception_ptr) {})(do_test());

        BOOST_TEST(completed);
    }

    void
    testReadExactSize()
    {
        int dispatch_count = 0;
        test_executor ex(dispatch_count);
        bool completed = false;

        auto do_test = []() -> task<void>
        {
            mock_read_stream stream;
            stream.data = "exact";

            char buf[5] = {};
            auto [ec, n] = co_await read(stream, make_buffer(buf));

            BOOST_TEST(!ec);
            BOOST_TEST_EQ(n, 5u);
            BOOST_TEST_EQ(std::string_view(buf, n), "exact");
        };

        run_async(ex,
            [&]() { completed = true; },
            [](std::exception_ptr) {})(do_test());

        BOOST_TEST(completed);
    }

    void
    testReadWithChunking()
    {
        int dispatch_count = 0;
        test_executor ex(dispatch_count);
        bool completed = false;

        auto do_test = []() -> task<void>
        {
            mock_read_stream stream;
            stream.data = "abcdefghij";
            stream.chunk_size = 3;

            char buf[10] = {};
            auto [ec, n] = co_await read(stream, make_buffer(buf));

            BOOST_TEST(!ec);
            BOOST_TEST_EQ(n, 10u);
            BOOST_TEST_EQ(std::string_view(buf, n), "abcdefghij");
        };

        run_async(ex,
            [&]() { completed = true; },
            [](std::exception_ptr) {})(do_test());

        BOOST_TEST(completed);
    }

    void
    testReadEofBeforeFull()
    {
        int dispatch_count = 0;
        test_executor ex(dispatch_count);
        bool completed = false;

        auto do_test = []() -> task<void>
        {
            mock_read_stream stream;
            stream.data = "short";

            char buf[32] = {};
            auto [ec, n] = co_await read(stream, make_buffer(buf));

            BOOST_TEST(ec == cond::eof);
            BOOST_TEST_EQ(n, 5u);
            BOOST_TEST_EQ(std::string_view(buf, n), "short");
        };

        run_async(ex,
            [&]() { completed = true; },
            [](std::exception_ptr) {})(do_test());

        BOOST_TEST(completed);
    }

    void
    testReadEmptyBuffer()
    {
        int dispatch_count = 0;
        test_executor ex(dispatch_count);
        bool completed = false;

        auto do_test = []() -> task<void>
        {
            mock_read_stream stream;
            stream.data = "data";

            auto [ec, n] = co_await read(stream, mutable_buffer());

            BOOST_TEST(!ec);
            BOOST_TEST_EQ(n, 0u);
        };

        run_async(ex,
            [&]() { completed = true; },
            [](std::exception_ptr) {})(do_test());

        BOOST_TEST(completed);
    }

    void
    testReadBufferSequence()
    {
        int dispatch_count = 0;
        test_executor ex(dispatch_count);
        bool completed = false;

        auto do_test = []() -> task<void>
        {
            mock_read_stream stream;
            stream.data = "helloworld";
            stream.chunk_size = 100;

            char buf1[5] = {};
            char buf2[5] = {};
            std::array<mutable_buffer, 2> buffers = {{
                make_buffer(buf1),
                make_buffer(buf2)
            }};

            auto [ec, n] = co_await read(stream, buffers);

            BOOST_TEST(!ec);
            BOOST_TEST_EQ(n, 10u);
            BOOST_TEST_EQ(std::string_view(buf1, 5), "hello");
            BOOST_TEST_EQ(std::string_view(buf2, 5), "world");
        };

        run_async(ex,
            [&]() { completed = true; },
            [](std::exception_ptr) {})(do_test());

        BOOST_TEST(completed);
    }

    void
    testReadErrorMidway()
    {
        int dispatch_count = 0;
        test_executor ex(dispatch_count);
        bool completed = false;

        auto do_test = []() -> task<void>
        {
            mock_error_stream stream;
            stream.data = "abcdefghij";
            stream.error_after = 5;
            stream.error_to_return = make_error_code(system::errc::io_error);

            char buf[10] = {};
            auto [ec, n] = co_await read(stream, make_buffer(buf));

            BOOST_TEST(ec == system::errc::io_error);
            BOOST_TEST_EQ(n, 5u);
            BOOST_TEST_EQ(std::string_view(buf, n), "abcde");
        };

        run_async(ex,
            [&]() { completed = true; },
            [](std::exception_ptr) {})(do_test());

        BOOST_TEST(completed);
    }

    void
    testReadImmediateError()
    {
        int dispatch_count = 0;
        test_executor ex(dispatch_count);
        bool completed = false;

        auto do_test = []() -> task<void>
        {
            mock_read_stream stream;
            stream.data = "data";
            stream.forced_error = make_error_code(system::errc::connection_reset);

            char buf[10] = {};
            auto [ec, n] = co_await read(stream, make_buffer(buf));

            BOOST_TEST(ec == system::errc::connection_reset);
            BOOST_TEST_EQ(n, 0u);
        };

        run_async(ex,
            [&]() { completed = true; },
            [](std::exception_ptr) {})(do_test());

        BOOST_TEST(completed);
    }

    //----------------------------------------------------------
    // ReadSource tests (uses DynamicBuffer, reads until EOF)
    //----------------------------------------------------------

    void
    testSourceReadAll()
    {
        int dispatch_count = 0;
        test_executor ex(dispatch_count);
        bool completed = false;

        auto do_test = []() -> task<void>
        {
            mock_read_source source;
            source.data = "hello world";

            std::string result;
            string_dynamic_buffer sb(&result);
            auto [ec, n] = co_await read(source, sb);

            BOOST_TEST(!ec);
            BOOST_TEST_EQ(n, 11u);
            BOOST_TEST_EQ(result, "hello world");
        };

        run_async(ex,
            [&]() { completed = true; },
            [](std::exception_ptr) {})(do_test());

        BOOST_TEST(completed);
    }

    void
    testSourceReadLargeData()
    {
        int dispatch_count = 0;
        test_executor ex(dispatch_count);
        bool completed = false;

        auto do_test = []() -> task<void>
        {
            mock_read_source source;
            source.data = std::string(10000, 'x');

            std::string result;
            string_dynamic_buffer sb(&result);
            auto [ec, n] = co_await read(source, sb);

            BOOST_TEST(!ec);
            BOOST_TEST_EQ(n, 10000u);
            BOOST_TEST_EQ(result.size(), 10000u);
            BOOST_TEST(result == std::string(10000, 'x'));
        };

        run_async(ex,
            [&]() { completed = true; },
            [](std::exception_ptr) {})(do_test());

        BOOST_TEST(completed);
    }

    void
    testSourceReadError()
    {
        int dispatch_count = 0;
        test_executor ex(dispatch_count);
        bool completed = false;

        auto do_test = []() -> task<void>
        {
            mock_read_source source;
            source.data = "data";
            source.forced_error = make_error_code(system::errc::io_error);

            std::string result;
            string_dynamic_buffer sb(&result);
            auto [ec, n] = co_await read(source, sb);

            BOOST_TEST(ec == system::errc::io_error);
            BOOST_TEST_EQ(n, 0u);
        };

        run_async(ex,
            [&]() { completed = true; },
            [](std::exception_ptr) {})(do_test());

        BOOST_TEST(completed);
    }

    void
    testSourceReadEmpty()
    {
        int dispatch_count = 0;
        test_executor ex(dispatch_count);
        bool completed = false;

        auto do_test = []() -> task<void>
        {
            mock_read_source source;
            source.data = "";

            std::string result;
            string_dynamic_buffer sb(&result);
            auto [ec, n] = co_await read(source, sb);

            BOOST_TEST(!ec);
            BOOST_TEST_EQ(n, 0u);
            BOOST_TEST(result.empty());
        };

        run_async(ex,
            [&]() { completed = true; },
            [](std::exception_ptr) {})(do_test());

        BOOST_TEST(completed);
    }

    void
    testSourceReadWithInitialAmount()
    {
        int dispatch_count = 0;
        test_executor ex(dispatch_count);
        bool completed = false;

        auto do_test = []() -> task<void>
        {
            mock_read_source source;
            source.data = "small";

            std::string result;
            string_dynamic_buffer sb(&result);
            auto [ec, n] = co_await read(source, sb, 64);

            BOOST_TEST(!ec);
            BOOST_TEST_EQ(n, 5u);
            BOOST_TEST_EQ(result, "small");
        };

        run_async(ex,
            [&]() { completed = true; },
            [](std::exception_ptr) {})(do_test());

        BOOST_TEST(completed);
    }

    void
    run()
    {
        testReadSingleBuffer();
        testReadExactSize();
        testReadWithChunking();
        testReadEofBeforeFull();
        testReadEmptyBuffer();
        testReadBufferSequence();
        testReadErrorMidway();
        testReadImmediateError();

        // ReadSource tests (DynamicBuffer)
        testSourceReadAll();
        testSourceReadLargeData();
        testSourceReadError();
        testSourceReadEmpty();
        testSourceReadWithInitialAmount();
    }
};

TEST_SUITE(
    read_test,
    "boost.capy.read");

} // namespace capy
} // namespace boost
