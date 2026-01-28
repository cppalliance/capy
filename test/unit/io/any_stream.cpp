//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Test that header file is self-contained.
#include <boost/capy/io/any_stream.hpp>

#include <boost/capy/buffers/buffer_copy.hpp>
#include <boost/capy/buffers/make_buffer.hpp>
#include <boost/capy/cond.hpp>
#include <boost/capy/task.hpp>
#include <boost/capy/test/fuse.hpp>

#include "test/unit/test_helpers.hpp"

#include <string>
#include <string_view>

namespace boost {
namespace capy {
namespace {

// Simple bidirectional mock stream for testing any_stream
class mock_stream
{
    test::fuse& f_;
    std::string read_data_;
    std::size_t read_pos_ = 0;
    std::string write_data_;

public:
    explicit
    mock_stream(test::fuse& f) noexcept
        : f_(f)
    {
    }

    void
    provide(std::string_view sv)
    {
        read_data_.append(sv);
    }

    std::string_view
    written() const noexcept
    {
        return write_data_;
    }

    template<MutableBufferSequence MB>
    auto
    read_some(MB const& buffers)
    {
        struct awaitable
        {
            mock_stream* self_;
            MB const& buffers_;

            bool await_ready() const noexcept { return true; }

            void await_suspend(
                coro,
                executor_ref,
                std::stop_token) const noexcept
            {
            }

            io_result<std::size_t>
            await_resume()
            {
                auto ec = self_->f_.maybe_fail();
                if(ec.failed())
                    return {ec, 0};

                if(self_->read_pos_ >= self_->read_data_.size())
                    return {error::eof, 0};

                std::size_t avail =
                    self_->read_data_.size() - self_->read_pos_;
                auto src = make_buffer(
                    self_->read_data_.data() + self_->read_pos_, avail);
                std::size_t const n = buffer_copy(buffers_, src);
                self_->read_pos_ += n;
                return {{}, n};
            }
        };
        return awaitable{this, buffers};
    }

    template<ConstBufferSequence CB>
    auto
    write_some(CB const& buffers)
    {
        struct awaitable
        {
            mock_stream* self_;
            CB const& buffers_;

            bool await_ready() const noexcept { return true; }

            void await_suspend(
                coro,
                executor_ref,
                std::stop_token) const noexcept
            {
            }

            io_result<std::size_t>
            await_resume()
            {
                auto ec = self_->f_.maybe_fail();
                if(ec.failed())
                    return {ec, 0};

                std::size_t n = buffer_size(buffers_);
                if(n == 0)
                    return {{}, 0};

                std::size_t const old_size = self_->write_data_.size();
                self_->write_data_.resize(old_size + n);
                buffer_copy(make_buffer(
                    self_->write_data_.data() + old_size, n), buffers_, n);
                return {{}, n};
            }
        };
        return awaitable{this, buffers};
    }
};

static_assert(ReadStream<mock_stream>);
static_assert(WriteStream<mock_stream>);

class any_stream_test
{
public:
    void
    testConstruct()
    {
        // Default construct
        {
            any_stream as;
            BOOST_TEST(!as.has_value());
            BOOST_TEST(!as);
        }

        // Construct from bidirectional stream
        {
            test::fuse f;
            mock_stream ms(f);
            any_stream as(&ms);
            BOOST_TEST(as.has_value());
            BOOST_TEST(static_cast<bool>(as));
        }
    }

    void
    testMove()
    {
        test::fuse f;
        mock_stream ms(f);

        any_stream as1(&ms);
        BOOST_TEST(as1.has_value());

        // Move construct
        any_stream as2(std::move(as1));
        BOOST_TEST(as2.has_value());
        BOOST_TEST(!as1.has_value());

        // Move assign
        any_stream as3;
        as3 = std::move(as2);
        BOOST_TEST(as3.has_value());
        BOOST_TEST(!as2.has_value());
    }

    void
    testImplicitConversion()
    {
        test::fuse f;
        mock_stream ms(f);
        any_stream as(ms);

        // Implicit conversion to any_read_stream&
        any_read_stream& reader = as;
        BOOST_TEST(reader.has_value());

        // Implicit conversion to any_write_stream&
        any_write_stream& writer = as;
        BOOST_TEST(writer.has_value());
    }

    void
    testReadWrite()
    {
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            mock_stream ms(f);
            ms.provide("hello");

            any_stream as(&ms);

            // Read
            char rbuf[32] = {};
            mutable_buffer rmb(rbuf, sizeof(rbuf));
            auto [ec1, n1] = co_await as.read_some(std::span(&rmb, 1));
            if(ec1.failed())
                co_return;
            BOOST_TEST_EQ(n1, 5u);
            BOOST_TEST_EQ(std::string_view(rbuf, n1), "hello");

            // Write
            char const wdata[] = "world";
            const_buffer wcb(wdata, 5);
            auto [ec2, n2] = co_await as.write_some(std::span(&wcb, 1));
            if(ec2.failed())
                co_return;
            BOOST_TEST_EQ(n2, 5u);
            BOOST_TEST_EQ(ms.written(), "world");
        });
        BOOST_TEST(r.success);
    }

    void
    testReadViaBase()
    {
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            mock_stream ms(f);
            ms.provide("test data");

            any_stream as(&ms);
            any_read_stream& reader = as;

            char buf[32] = {};
            mutable_buffer mb(buf, sizeof(buf));
            auto [ec, n] = co_await reader.read_some(std::span(&mb, 1));
            if(ec.failed())
                co_return;

            BOOST_TEST_EQ(n, 9u);
            BOOST_TEST_EQ(std::string_view(buf, n), "test data");
        });
        BOOST_TEST(r.success);
    }

    void
    testWriteViaBase()
    {
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            mock_stream ms(f);

            any_stream as(&ms);
            any_write_stream& writer = as;

            char const data[] = "test output";
            const_buffer cb(data, 11);
            auto [ec, n] = co_await writer.write_some(std::span(&cb, 1));
            if(ec.failed())
                co_return;

            BOOST_TEST_EQ(n, 11u);
            BOOST_TEST_EQ(ms.written(), "test output");
        });
        BOOST_TEST(r.success);
    }

    void
    run()
    {
        testConstruct();
        testMove();
        testImplicitConversion();
        testReadWrite();
        testReadViaBase();
        testWriteViaBase();
    }
};

TEST_SUITE(any_stream_test, "boost.capy.io.any_stream");

} // namespace
} // namespace capy
} // namespace boost
