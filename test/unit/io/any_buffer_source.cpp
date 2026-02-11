//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Test that header file is self-contained.
#include <boost/capy/io/any_buffer_source.hpp>

// Test that push_to header is self-contained.
#include <boost/capy/io/push_to.hpp>

#include <boost/capy/buffers/make_buffer.hpp>
#include <boost/capy/cond.hpp>
#include <boost/capy/ex/io_env.hpp>
#include <boost/capy/task.hpp>
#include <boost/capy/test/buffer_source.hpp>
#include <boost/capy/test/write_sink.hpp>

#include "test/unit/test_helpers.hpp"

#include <algorithm>
#include <cstring>
#include <string>
#include <string_view>

namespace boost {
namespace capy {
namespace {

// Static assert that any_buffer_source satisfies BufferSource
static_assert(BufferSource<any_buffer_source>);
static_assert(ReadSource<any_buffer_source>);

//----------------------------------------------------------
// Mock satisfying both BufferSource and ReadSource.
// Tracks which API was used so tests can verify native
// forwarding vs. synthesized path.

class buffer_read_source
{
    test::fuse f_;
    std::string data_;
    std::size_t pos_ = 0;
    std::size_t max_pull_size_;
    bool read_api_used_ = false;

public:
    explicit buffer_read_source(
        test::fuse f = {},
        std::size_t max_pull_size = std::size_t(-1)) noexcept
        : f_(std::move(f))
        , max_pull_size_(max_pull_size)
    {
    }

    void
    provide(std::string_view sv)
    {
        data_.append(sv);
    }

    std::size_t
    available() const noexcept
    {
        return data_.size() - pos_;
    }

    /// Return true if the ReadSource API was used.
    bool
    read_api_used() const noexcept
    {
        return read_api_used_;
    }

    //------------------------------------------------------
    // BufferSource interface

    void
    consume(std::size_t n) noexcept
    {
        pos_ += n;
    }

    auto
    pull(std::span<const_buffer> dest)
    {
        struct awaitable
        {
            buffer_read_source* self_;
            std::span<const_buffer> dest_;

            bool await_ready() const noexcept { return true; }
            void await_suspend(std::coroutine_handle<>, io_env const*) const noexcept {}

            io_result<std::span<const_buffer>>
            await_resume()
            {
                auto ec = self_->f_.maybe_fail();
                if(ec)
                    return {ec, {}};

                if(self_->pos_ >= self_->data_.size())
                    return {error::eof, {}};

                std::size_t avail = self_->data_.size() - self_->pos_;
                std::size_t to_return = (std::min)(avail, self_->max_pull_size_);

                if(dest_.empty())
                    return {{}, {}};

                dest_[0] = make_buffer(
                    self_->data_.data() + self_->pos_,
                    to_return);
                return {{}, dest_.first(1)};
            }
        };
        return awaitable{this, dest};
    }

    //------------------------------------------------------
    // ReadSource interface

    template<MutableBufferSequence MB>
    auto
    read_some(MB buffers)
    {
        struct awaitable
        {
            buffer_read_source* self_;
            MB buffers_;

            bool await_ready() const noexcept { return true; }
            void await_suspend(std::coroutine_handle<>, io_env const*) const noexcept {}

            io_result<std::size_t>
            await_resume()
            {
                self_->read_api_used_ = true;
                if(buffer_empty(buffers_))
                    return {{}, 0};
                auto ec = self_->f_.maybe_fail();
                if(ec)
                    return {ec, 0};

                if(self_->pos_ >= self_->data_.size())
                    return {error::eof, 0};

                std::size_t avail = self_->data_.size() - self_->pos_;
                if(avail > self_->max_pull_size_)
                    avail = self_->max_pull_size_;
                auto src = make_buffer(
                    self_->data_.data() + self_->pos_, avail);
                std::size_t const n = buffer_copy(buffers_, src);
                self_->pos_ += n;
                return {{}, n};
            }
        };
        return awaitable{this, buffers};
    }

    template<MutableBufferSequence MB>
    auto
    read(MB buffers)
    {
        struct awaitable
        {
            buffer_read_source* self_;
            MB buffers_;

            bool await_ready() const noexcept { return true; }
            void await_suspend(std::coroutine_handle<>, io_env const*) const noexcept {}

            io_result<std::size_t>
            await_resume()
            {
                self_->read_api_used_ = true;
                if(buffer_empty(buffers_))
                    return {{}, 0};
                auto ec = self_->f_.maybe_fail();
                if(ec)
                    return {ec, 0};

                if(self_->pos_ >= self_->data_.size())
                    return {error::eof, 0};

                std::size_t avail = self_->data_.size() - self_->pos_;
                auto src = make_buffer(
                    self_->data_.data() + self_->pos_, avail);
                std::size_t const n = buffer_copy(buffers_, src);
                self_->pos_ += n;

                if(n < buffer_size(buffers_))
                    return {error::eof, n};
                return {{}, n};
            }
        };
        return awaitable{this, buffers};
    }
};

// Verify concepts at compile time
static_assert(BufferSource<buffer_read_source>);
static_assert(ReadSource<buffer_read_source>);

// Verify BufferSource-only mock does NOT satisfy ReadSource
static_assert(!ReadSource<test::buffer_source>);

//----------------------------------------------------------

class any_buffer_source_test
{
public:
    void
    testConstruct()
    {
        // Default construct
        {
            any_buffer_source abs;
            BOOST_TEST(!abs.has_value());
            BOOST_TEST(!abs);
        }

        // Construct from BufferSource-only (reference)
        {
            test::fuse f;
            test::buffer_source bs(f);
            any_buffer_source abs(&bs);
            BOOST_TEST(abs.has_value());
            BOOST_TEST(static_cast<bool>(abs));
        }

        // Construct from BufferSource+ReadSource (reference)
        {
            test::fuse f;
            buffer_read_source brs(f);
            any_buffer_source abs(&brs);
            BOOST_TEST(abs.has_value());
        }

        // Owning construct from BufferSource+ReadSource
        {
            test::fuse f;
            any_buffer_source abs((buffer_read_source(f)));
            BOOST_TEST(abs.has_value());
        }
    }

    void
    testMove()
    {
        test::fuse f;
        test::buffer_source bs(f);

        any_buffer_source abs1(&bs);
        BOOST_TEST(abs1.has_value());

        // Move construct
        any_buffer_source abs2(std::move(abs1));
        BOOST_TEST(abs2.has_value());
        BOOST_TEST(!abs1.has_value());

        // Move assign
        any_buffer_source abs3;
        abs3 = std::move(abs2);
        BOOST_TEST(abs3.has_value());
        BOOST_TEST(!abs2.has_value());
    }

    void
    testMoveNative()
    {
        test::fuse f;
        buffer_read_source brs(f);

        any_buffer_source abs1(&brs);
        BOOST_TEST(abs1.has_value());

        // Move construct
        any_buffer_source abs2(std::move(abs1));
        BOOST_TEST(abs2.has_value());
        BOOST_TEST(!abs1.has_value());

        // Move assign
        any_buffer_source abs3;
        abs3 = std::move(abs2);
        BOOST_TEST(abs3.has_value());
        BOOST_TEST(!abs2.has_value());
    }

    void
    testPull()
    {
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            test::buffer_source bs(f);
            bs.provide("hello world");

            any_buffer_source abs(&bs);

            const_buffer arr[detail::max_iovec_];
            auto [ec, bufs] = co_await abs.pull(arr);
            if(ec)
                co_return;

            BOOST_TEST_EQ(bufs.size(), 1u);
            BOOST_TEST_EQ(bufs[0].size(), 11u);
            abs.consume(11);
        });
        BOOST_TEST(r.success);
    }

    void
    testConsume()
    {
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            test::buffer_source bs(f);
            bs.provide("hello world");

            any_buffer_source abs(&bs);

            const_buffer arr[detail::max_iovec_];

            // First pull returns all data
            auto [ec1, bufs1] = co_await abs.pull(arr);
            if(ec1)
                co_return;
            BOOST_TEST_EQ(bufs1.size(), 1u);
            BOOST_TEST_EQ(bufs1[0].size(), 11u);

            // Consume partial (5 bytes = "hello")
            abs.consume(5);

            // Second pull returns remaining data
            auto [ec2, bufs2] = co_await abs.pull(arr);
            if(ec2)
                co_return;
            BOOST_TEST_EQ(bufs2.size(), 1u);
            BOOST_TEST_EQ(bufs2[0].size(), 6u); // " world"

            // Consume rest
            abs.consume(6);

            // Third pull returns eof (exhausted)
            auto [ec3, bufs3] = co_await abs.pull(arr);
            if(ec3 != capy::cond::eof)
                co_return;
            BOOST_TEST(bufs3.empty());
        });
        BOOST_TEST(r.success);
    }

    void
    testPullWithoutConsume()
    {
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            test::buffer_source bs(f);
            bs.provide("test");

            any_buffer_source abs(&bs);

            const_buffer arr[detail::max_iovec_];

            // Pull returns data
            auto [ec1, bufs1] = co_await abs.pull(arr);
            if(ec1)
                co_return;
            BOOST_TEST_EQ(bufs1.size(), 1u);
            BOOST_TEST_EQ(bufs1[0].size(), 4u);

            // Pull again without consume returns same data
            auto [ec2, bufs2] = co_await abs.pull(arr);
            if(ec2)
                co_return;
            BOOST_TEST_EQ(bufs2.size(), 1u);
            BOOST_TEST_EQ(bufs2[0].size(), 4u);

            abs.consume(4);
        });
        BOOST_TEST(r.success);
    }

    void
    testPullMultiple()
    {
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            test::buffer_source bs(f, 5); // max 5 bytes per pull
            bs.provide("hello world");

            any_buffer_source abs(&bs);

            std::size_t total = 0;
            for(;;)
            {
                const_buffer arr[detail::max_iovec_];
                auto [ec, bufs] = co_await abs.pull(arr);
                if(ec == capy::cond::eof)
                    break;
                if(ec)
                    co_return;
                for(auto const& buf : bufs)
                {
                    total += buf.size();
                    abs.consume(buf.size());
                }
            }

            BOOST_TEST_EQ(total, 11u);
        });
        BOOST_TEST(r.success);
    }

    void
    testPullEmpty()
    {
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            test::buffer_source bs(f);
            // No data provided

            any_buffer_source abs(&bs);

            const_buffer arr[detail::max_iovec_];
            auto [ec, bufs] = co_await abs.pull(arr);
            if(ec != capy::cond::eof)
                co_return;
            BOOST_TEST(bufs.empty());
        });
        BOOST_TEST(r.success);
    }

    //------------------------------------------------------
    // Synthesized ReadSource tests (BufferSource-only mock)

    void
    testSynthesizedReadSome()
    {
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            test::buffer_source bs(f);
            bs.provide("hello world");

            any_buffer_source abs(&bs);

            char buf[64];
            auto [ec, n] = co_await abs.read_some(
                mutable_buffer(buf, sizeof(buf)));
            if(ec)
                co_return;

            BOOST_TEST(n > 0);
            BOOST_TEST(n <= 11u);
            BOOST_TEST_EQ(
                std::string_view(buf, n),
                std::string_view("hello world", n));
        });
        BOOST_TEST(r.success);
    }

    void
    testSynthesizedRead()
    {
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            test::buffer_source bs(f);
            bs.provide("hello world");

            any_buffer_source abs(&bs);

            char buf[11];
            auto [ec, n] = co_await abs.read(
                mutable_buffer(buf, sizeof(buf)));
            if(ec)
                co_return;

            BOOST_TEST_EQ(n, 11u);
            BOOST_TEST_EQ(
                std::string_view(buf, n),
                "hello world");
        });
        BOOST_TEST(r.success);
    }

    void
    testSynthesizedReadSomeEmpty()
    {
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            test::buffer_source bs(f);
            bs.provide("data");

            any_buffer_source abs(&bs);

            // Empty buffer returns 0 immediately
            auto [ec, n] = co_await abs.read_some(
                mutable_buffer(nullptr, 0));
            BOOST_TEST(!ec);
            BOOST_TEST_EQ(n, 0u);
        });
        BOOST_TEST(r.success);
    }

    //------------------------------------------------------
    // Native ReadSource tests (BufferSource+ReadSource mock)

    void
    testNativeReadSome()
    {
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            buffer_read_source brs(f);
            brs.provide("hello world");

            any_buffer_source abs(&brs);

            char buf[64];
            auto [ec, n] = co_await abs.read_some(
                mutable_buffer(buf, sizeof(buf)));
            if(ec)
                co_return;

            BOOST_TEST(n > 0);
            BOOST_TEST(n <= 11u);
            BOOST_TEST(brs.read_api_used());
            BOOST_TEST_EQ(
                std::string_view(buf, n),
                std::string_view("hello world", n));
        });
        BOOST_TEST(r.success);
    }

    void
    testNativeRead()
    {
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            buffer_read_source brs(f);
            brs.provide("hello world");

            any_buffer_source abs(&brs);

            char buf[11];
            auto [ec, n] = co_await abs.read(
                mutable_buffer(buf, sizeof(buf)));
            if(ec)
                co_return;

            BOOST_TEST_EQ(n, 11u);
            BOOST_TEST(brs.read_api_used());
            BOOST_TEST_EQ(
                std::string_view(buf, n),
                "hello world");
        });
        BOOST_TEST(r.success);
    }

    void
    testNativeReadSomeEmpty()
    {
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            buffer_read_source brs(f);
            brs.provide("data");

            any_buffer_source abs(&brs);

            // Empty buffer returns 0 immediately
            auto [ec, n] = co_await abs.read_some(
                mutable_buffer(nullptr, 0));
            BOOST_TEST(!ec);
            BOOST_TEST_EQ(n, 0u);
            // ReadSource API should NOT be called for empty buffers
            BOOST_TEST(!brs.read_api_used());
        });
        BOOST_TEST(r.success);
    }

    void
    testNativePullAndConsume()
    {
        // Verify that pull/consume still works even when
        // the wrapped type satisfies ReadSource
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            buffer_read_source brs(f);
            brs.provide("hello");

            any_buffer_source abs(&brs);

            const_buffer arr[detail::max_iovec_];
            auto [ec, bufs] = co_await abs.pull(arr);
            if(ec)
                co_return;

            BOOST_TEST_EQ(bufs.size(), 1u);
            BOOST_TEST_EQ(bufs[0].size(), 5u);
            abs.consume(5);

            // Read API should NOT be used for pull/consume
            BOOST_TEST(!brs.read_api_used());
        });
        BOOST_TEST(r.success);
    }

    void
    testNativeOwning()
    {
        // Verify owning construction forwards native ReadSource
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            buffer_read_source brs(f);
            brs.provide("hello world");

            any_buffer_source abs(std::move(brs));

            char buf[11];
            auto [ec, n] = co_await abs.read(
                mutable_buffer(buf, sizeof(buf)));
            if(ec)
                co_return;

            BOOST_TEST_EQ(n, 11u);
            BOOST_TEST_EQ(
                std::string_view(buf, n),
                "hello world");
        });
        BOOST_TEST(r.success);
    }

    void
    testNativeReadEof()
    {
        // Verify EOF handling through native path
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            buffer_read_source brs(f);
            brs.provide("hi");

            any_buffer_source abs(&brs);

            // Try to read more than available
            char buf[64];
            auto [ec, n] = co_await abs.read(
                mutable_buffer(buf, sizeof(buf)));

            // Fuse may inject a non-eof error
            if(ec && ec != capy::cond::eof)
                co_return;

            // Should get partial data + EOF
            BOOST_TEST(ec == capy::cond::eof);
            BOOST_TEST_EQ(n, 2u);
            BOOST_TEST(brs.read_api_used());
            BOOST_TEST_EQ(std::string_view(buf, n), "hi");
        });
        BOOST_TEST(r.success);
    }

    void
    testNativeReadSomeChunked()
    {
        // Verify chunked native read_some
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            buffer_read_source brs(f, 3);
            brs.provide("hello world");

            any_buffer_source abs(&brs);

            std::string result;
            for(;;)
            {
                char buf[64];
                auto [ec, n] = co_await abs.read_some(
                    mutable_buffer(buf, sizeof(buf)));
                if(ec == capy::cond::eof)
                    break;
                if(ec)
                    co_return;
                result.append(buf, n);
            }

            BOOST_TEST(brs.read_api_used());
            BOOST_TEST_EQ(result, "hello world");
        });
        BOOST_TEST(r.success);
    }

    //------------------------------------------------------
    // push_to tests

    void
    testPushTo()
    {
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            test::buffer_source bs(f);
            bs.provide("hello world");

            test::write_sink ws(f);

            auto [ec, n] = co_await push_to(bs, ws);
            if(ec)
                co_return;

            BOOST_TEST_EQ(n, 11u);
            BOOST_TEST_EQ(ws.data(), "hello world");
            BOOST_TEST(ws.eof_called());
        });
        BOOST_TEST(r.success);
    }

    void
    testPushToTypeErased()
    {
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            test::buffer_source bs(f);
            bs.provide("hello world");

            any_buffer_source abs(&bs);

            test::write_sink ws(f);

            auto [ec, n] = co_await push_to(abs, ws);
            if(ec)
                co_return;

            BOOST_TEST_EQ(n, 11u);
            BOOST_TEST_EQ(ws.data(), "hello world");
            BOOST_TEST(ws.eof_called());
        });
        BOOST_TEST(r.success);
    }

    void
    testPushToChunked()
    {
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            test::buffer_source bs(f, 5); // max 5 bytes per pull
            bs.provide("hello world");

            test::write_sink ws(f);

            auto [ec, n] = co_await push_to(bs, ws);
            if(ec)
                co_return;

            BOOST_TEST_EQ(n, 11u);
            BOOST_TEST_EQ(ws.data(), "hello world");
            BOOST_TEST(ws.eof_called());
        });
        BOOST_TEST(r.success);
    }

    void
    testPushToEmpty()
    {
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            test::buffer_source bs(f);
            // No data provided

            test::write_sink ws(f);

            auto [ec, n] = co_await push_to(bs, ws);
            if(ec)
                co_return;

            BOOST_TEST_EQ(n, 0u);
            BOOST_TEST(ws.data().empty());
            BOOST_TEST(ws.eof_called());
        });
        BOOST_TEST(r.success);
    }

    void
    run()
    {
        testConstruct();
        testMove();
        testMoveNative();
        testPull();
        testConsume();
        testPullWithoutConsume();
        testPullMultiple();
        testPullEmpty();
        testSynthesizedReadSome();
        testSynthesizedRead();
        testSynthesizedReadSomeEmpty();
        testNativeReadSome();
        testNativeRead();
        testNativeReadSomeEmpty();
        testNativePullAndConsume();
        testNativeOwning();
        testNativeReadEof();
        testNativeReadSomeChunked();
        testPushTo();
        testPushToTypeErased();
        testPushToChunked();
        testPushToEmpty();
    }
};

TEST_SUITE(any_buffer_source_test, "boost.capy.io.any_buffer_source");

} // namespace
} // namespace capy
} // namespace boost
