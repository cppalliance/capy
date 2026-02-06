//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Test that header file is self-contained.
#include <boost/capy/io/any_buffer_sink.hpp>

// Test that pull_from header is self-contained.
#include <boost/capy/io/pull_from.hpp>

#include <boost/capy/buffers/make_buffer.hpp>
#include <boost/capy/task.hpp>
#include <boost/capy/test/buffer_sink.hpp>
#include <boost/capy/test/read_source.hpp>
#include <boost/capy/test/read_stream.hpp>
#include <boost/capy/test/write_sink.hpp>

#include "test/unit/test_helpers.hpp"

#include <algorithm>
#include <cstring>
#include <string>
#include <string_view>

namespace boost {
namespace capy {
namespace {

// Static assert that any_buffer_sink satisfies BufferSink and WriteSink
static_assert(BufferSink<any_buffer_sink>);
static_assert(WriteSink<any_buffer_sink>);

//----------------------------------------------------------
// Mock satisfying both BufferSink and WriteSink.
// Tracks which API was used so tests can verify native
// forwarding vs. synthesized path.

class buffer_write_sink
{
    test::fuse f_;
    std::string data_;
    std::string prepare_buf_;
    std::size_t prepare_size_ = 0;
    std::size_t max_prepare_size_;
    bool eof_called_ = false;
    bool write_api_used_ = false;

public:
    explicit buffer_write_sink(
        test::fuse f = {},
        std::size_t max_prepare_size = 4096) noexcept
        : f_(std::move(f))
        , max_prepare_size_(max_prepare_size)
    {
        prepare_buf_.resize(max_prepare_size_);
    }

    std::string_view
    data() const noexcept
    {
        return data_;
    }

    bool
    eof_called() const noexcept
    {
        return eof_called_;
    }

    /// Return true if the WriteSink API was used.
    bool
    write_api_used() const noexcept
    {
        return write_api_used_;
    }

    //------------------------------------------------------
    // BufferSink interface

    std::span<mutable_buffer>
    prepare(std::span<mutable_buffer> dest)
    {
        if(dest.empty())
            return {};
        prepare_size_ = max_prepare_size_;
        dest[0] = make_buffer(prepare_buf_.data(), prepare_size_);
        return dest.first(1);
    }

    auto
    commit(std::size_t n)
    {
        struct awaitable
        {
            buffer_write_sink* self_;
            std::size_t n_;

            bool await_ready() const noexcept { return true; }
            void await_suspend(coro, executor_ref, std::stop_token) const noexcept {}

            io_result<>
            await_resume()
            {
                auto ec = self_->f_.maybe_fail();
                if(ec) return {ec};
                std::size_t to_commit = (std::min)(n_, self_->prepare_size_);
                self_->data_.append(self_->prepare_buf_.data(), to_commit);
                self_->prepare_size_ = 0;
                return {};
            }
        };
        return awaitable{this, n};
    }

    auto
    commit_eof(std::size_t n)
    {
        struct awaitable
        {
            buffer_write_sink* self_;
            std::size_t n_;

            bool await_ready() const noexcept { return true; }
            void await_suspend(coro, executor_ref, std::stop_token) const noexcept {}

            io_result<>
            await_resume()
            {
                auto ec = self_->f_.maybe_fail();
                if(ec) return {ec};
                std::size_t to_commit = (std::min)(n_, self_->prepare_size_);
                self_->data_.append(self_->prepare_buf_.data(), to_commit);
                self_->prepare_size_ = 0;
                self_->eof_called_ = true;
                return {};
            }
        };
        return awaitable{this, n};
    }

    //------------------------------------------------------
    // WriteSink interface

    template<ConstBufferSequence CB>
    auto
    write_some(CB buffers)
    {
        struct awaitable
        {
            buffer_write_sink* self_;
            CB buffers_;

            bool await_ready() const noexcept { return true; }
            void await_suspend(coro, executor_ref, std::stop_token) const noexcept {}

            io_result<std::size_t>
            await_resume()
            {
                self_->write_api_used_ = true;
                if(buffer_empty(buffers_))
                    return {{}, 0};
                auto ec = self_->f_.maybe_fail();
                if(ec) return {ec, 0};

                std::size_t n = buffer_size(buffers_);
                std::size_t const old_size = self_->data_.size();
                self_->data_.resize(old_size + n);
                buffer_copy(make_buffer(
                    self_->data_.data() + old_size, n), buffers_, n);
                return {{}, n};
            }
        };
        return awaitable{this, buffers};
    }

    template<ConstBufferSequence CB>
    auto
    write(CB buffers)
    {
        struct awaitable
        {
            buffer_write_sink* self_;
            CB buffers_;

            bool await_ready() const noexcept { return true; }
            void await_suspend(coro, executor_ref, std::stop_token) const noexcept {}

            io_result<std::size_t>
            await_resume()
            {
                self_->write_api_used_ = true;
                auto ec = self_->f_.maybe_fail();
                if(ec) return {ec, 0};

                std::size_t n = buffer_size(buffers_);
                if(n == 0) return {{}, 0};
                std::size_t const old_size = self_->data_.size();
                self_->data_.resize(old_size + n);
                buffer_copy(make_buffer(
                    self_->data_.data() + old_size, n), buffers_);
                return {{}, n};
            }
        };
        return awaitable{this, buffers};
    }

    template<ConstBufferSequence CB>
    auto
    write_eof(CB buffers)
    {
        struct awaitable
        {
            buffer_write_sink* self_;
            CB buffers_;

            bool await_ready() const noexcept { return true; }
            void await_suspend(coro, executor_ref, std::stop_token) const noexcept {}

            io_result<std::size_t>
            await_resume()
            {
                self_->write_api_used_ = true;
                auto ec = self_->f_.maybe_fail();
                if(ec) return {ec, 0};

                std::size_t n = buffer_size(buffers_);
                if(n > 0)
                {
                    std::size_t const old_size = self_->data_.size();
                    self_->data_.resize(old_size + n);
                    buffer_copy(make_buffer(
                        self_->data_.data() + old_size, n), buffers_);
                }
                self_->eof_called_ = true;
                return {{}, n};
            }
        };
        return awaitable{this, buffers};
    }

    auto
    write_eof()
    {
        struct awaitable
        {
            buffer_write_sink* self_;

            bool await_ready() const noexcept { return true; }
            void await_suspend(coro, executor_ref, std::stop_token) const noexcept {}

            io_result<>
            await_resume()
            {
                self_->write_api_used_ = true;
                auto ec = self_->f_.maybe_fail();
                if(ec) return {ec};
                self_->eof_called_ = true;
                return {};
            }
        };
        return awaitable{this};
    }
};

// Verify concepts at compile time
static_assert(BufferSink<buffer_write_sink>);
static_assert(WriteSink<buffer_write_sink>);

// Verify BufferSink-only mock does NOT satisfy WriteSink
static_assert(!WriteSink<test::buffer_sink>);

//----------------------------------------------------------

class any_buffer_sink_test
{
public:
    void
    testConstruct()
    {
        // Default construct
        {
            any_buffer_sink abs;
            BOOST_TEST(!abs.has_value());
            BOOST_TEST(!abs);
        }

        // Construct from BufferSink-only (reference)
        {
            test::fuse f;
            test::buffer_sink bs(f);
            any_buffer_sink abs(&bs);
            BOOST_TEST(abs.has_value());
            BOOST_TEST(static_cast<bool>(abs));
        }

        // Construct from BufferSink+WriteSink (reference)
        {
            test::fuse f;
            buffer_write_sink bws(f);
            any_buffer_sink abs(&bws);
            BOOST_TEST(abs.has_value());
            BOOST_TEST(static_cast<bool>(abs));
        }
    }

    void
    testConstructOwning()
    {
        // Owning construct by value (BufferSink-only)
        {
            test::fuse f;
            test::buffer_sink bs(f);
            any_buffer_sink abs(std::move(bs));
            BOOST_TEST(abs.has_value());
        }

        // Owning construct by value (BufferSink+WriteSink)
        {
            test::fuse f;
            buffer_write_sink bws(f);
            any_buffer_sink abs(std::move(bws));
            BOOST_TEST(abs.has_value());
        }

        // Owning construct, then use
        {
            test::fuse f;
            auto r = f.armed([&](test::fuse&) -> task<> {
                test::buffer_sink bs(f);
                any_buffer_sink abs(std::move(bs));

                mutable_buffer arr[detail::max_iovec_];
                auto bufs = abs.prepare(arr);
                BOOST_TEST_EQ(bufs.size(), 1u);

                std::memcpy(bufs[0].data(), "owned", 5);

                auto [ec] = co_await abs.commit_eof(5);
                if(ec)
                    co_return;
            });
            BOOST_TEST(r.success);
        }
    }

    void
    testMove()
    {
        test::fuse f;
        test::buffer_sink bs(f);

        any_buffer_sink abs1(&bs);
        BOOST_TEST(abs1.has_value());

        // Move construct
        any_buffer_sink abs2(std::move(abs1));
        BOOST_TEST(abs2.has_value());
        BOOST_TEST(!abs1.has_value());

        // Move assign into empty
        any_buffer_sink abs3;
        abs3 = std::move(abs2);
        BOOST_TEST(abs3.has_value());
        BOOST_TEST(!abs2.has_value());
    }

    void
    testMoveAssignOverExisting()
    {
        test::fuse f;
        test::buffer_sink bs1(f);
        test::buffer_sink bs2(f);

        any_buffer_sink abs1(&bs1);
        any_buffer_sink abs2(&bs2);
        BOOST_TEST(abs1.has_value());
        BOOST_TEST(abs2.has_value());

        // Move assign over a wrapper that already holds a sink
        abs1 = std::move(abs2);
        BOOST_TEST(abs1.has_value());
        BOOST_TEST(!abs2.has_value());
    }

    void
    testPrepareCommit()
    {
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            test::buffer_sink bs(f);
            any_buffer_sink abs(&bs);

            mutable_buffer arr[detail::max_iovec_];
            auto bufs = abs.prepare(arr);
            BOOST_TEST_EQ(bufs.size(), 1u);
            BOOST_TEST(bufs[0].size() > 0);

            // Write data into the buffer
            std::memcpy(bufs[0].data(), "hello", 5);

            auto [ec] = co_await abs.commit(5);
            if(ec)
                co_return;

            BOOST_TEST_EQ(bs.data(), "hello");
        });
        BOOST_TEST(r.success);
    }

    void
    testCommitWithEof()
    {
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            test::buffer_sink bs(f);
            any_buffer_sink abs(&bs);

            mutable_buffer arr[detail::max_iovec_];
            auto bufs = abs.prepare(arr);
            BOOST_TEST_EQ(bufs.size(), 1u);

            std::memcpy(bufs[0].data(), "world", 5);

            auto [ec] = co_await abs.commit_eof(5);
            if(ec)
                co_return;

            BOOST_TEST_EQ(bs.data(), "world");
            BOOST_TEST(bs.eof_called());
        });
        BOOST_TEST(r.success);
    }

    void
    testCommitEof()
    {
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            test::buffer_sink bs(f);
            any_buffer_sink abs(&bs);

            mutable_buffer arr[detail::max_iovec_];
            auto bufs = abs.prepare(arr);
            BOOST_TEST_EQ(bufs.size(), 1u);

            std::memcpy(bufs[0].data(), "data", 4);

            auto [ec1] = co_await abs.commit(4);
            if(ec1)
                co_return;

            auto [ec2] = co_await abs.commit_eof(0);
            if(ec2)
                co_return;

            BOOST_TEST_EQ(bs.data(), "data");
            BOOST_TEST(bs.eof_called());
        });
        BOOST_TEST(r.success);
    }

    void
    testMultipleCommits()
    {
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            test::buffer_sink bs(f);
            any_buffer_sink abs(&bs);

            // First write
            {
                mutable_buffer arr[detail::max_iovec_];
                auto bufs = abs.prepare(arr);
                BOOST_TEST_EQ(bufs.size(), 1u);

                std::memcpy(bufs[0].data(), "hello ", 6);

                auto [ec] = co_await abs.commit(6);
                if(ec)
                    co_return;
            }

            // Second write
            {
                mutable_buffer arr[detail::max_iovec_];
                auto bufs = abs.prepare(arr);
                BOOST_TEST_EQ(bufs.size(), 1u);

                std::memcpy(bufs[0].data(), "world", 5);

                auto [ec] = co_await abs.commit_eof(5);
                if(ec)
                    co_return;
            }

            BOOST_TEST_EQ(bs.data(), "hello world");
            BOOST_TEST(bs.eof_called());
        });
        BOOST_TEST(r.success);
    }

    void
    testEmptyCommit()
    {
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            test::buffer_sink bs(f);
            any_buffer_sink abs(&bs);

            auto [ec] = co_await abs.commit_eof(0);
            if(ec)
                co_return;

            BOOST_TEST(bs.data().empty());
            BOOST_TEST(bs.eof_called());
        });
        BOOST_TEST(r.success);
    }

    //------------------------------------------------------
    // Synthesized WriteSink tests (BufferSink-only)

    void
    testWriteSome()
    {
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            test::buffer_sink bs(f);
            any_buffer_sink abs(&bs);

            auto buf = make_buffer("hello", 5);
            auto [ec, n] = co_await abs.write_some(buf);
            if(ec)
                co_return;

            BOOST_TEST(n > 0);
            BOOST_TEST(n <= 5u);
            BOOST_TEST_EQ(bs.data(),
                std::string_view("hello", n));
        });
        BOOST_TEST(r.success);
    }

    void
    testWrite()
    {
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            test::buffer_sink bs(f);
            any_buffer_sink abs(&bs);

            auto buf = make_buffer("hello world", 11);
            auto [ec, n] = co_await abs.write(buf);
            if(ec)
                co_return;

            BOOST_TEST_EQ(n, 11u);
            BOOST_TEST_EQ(bs.data(), "hello world");
        });
        BOOST_TEST(r.success);
    }

    void
    testWriteEofWithBuffers()
    {
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            test::buffer_sink bs(f);
            any_buffer_sink abs(&bs);

            auto buf = make_buffer("final", 5);
            auto [ec, n] = co_await abs.write_eof(buf);
            if(ec)
                co_return;

            BOOST_TEST_EQ(n, 5u);
            BOOST_TEST_EQ(bs.data(), "final");
            BOOST_TEST(bs.eof_called());
        });
        BOOST_TEST(r.success);
    }

    void
    testWriteEofNoArg()
    {
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            test::buffer_sink bs(f);
            any_buffer_sink abs(&bs);

            auto [ec] = co_await abs.write_eof();
            if(ec)
                co_return;

            BOOST_TEST(bs.data().empty());
            BOOST_TEST(bs.eof_called());
        });
        BOOST_TEST(r.success);
    }

    void
    testWriteThenEof()
    {
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            test::buffer_sink bs(f);
            any_buffer_sink abs(&bs);

            auto buf = make_buffer("payload", 7);
            auto [ec1, n] = co_await abs.write(buf);
            if(ec1)
                co_return;

            BOOST_TEST_EQ(n, 7u);
            BOOST_TEST(!bs.eof_called());

            auto [ec2] = co_await abs.write_eof();
            if(ec2)
                co_return;

            BOOST_TEST_EQ(bs.data(), "payload");
            BOOST_TEST(bs.eof_called());
        });
        BOOST_TEST(r.success);
    }

    void
    testFuseErrorCommit()
    {
        int success_count = 0;
        int error_count = 0;

        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            test::buffer_sink bs(f);
            any_buffer_sink abs(&bs);

            mutable_buffer arr[detail::max_iovec_];
            auto bufs = abs.prepare(arr);
            std::memcpy(bufs[0].data(), "data", 4);

            auto [ec] = co_await abs.commit(4);
            if(ec)
            {
                ++error_count;
                co_return;
            }
            ++success_count;
        });

        BOOST_TEST(r.success);
        BOOST_TEST(error_count > 0);
        BOOST_TEST(success_count > 0);
    }

    void
    testFuseErrorCommitEof()
    {
        int success_count = 0;
        int error_count = 0;

        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            test::buffer_sink bs(f);
            any_buffer_sink abs(&bs);

            auto [ec] = co_await abs.commit_eof(0);
            if(ec)
            {
                ++error_count;
                co_return;
            }
            ++success_count;
        });

        BOOST_TEST(r.success);
        BOOST_TEST(error_count > 0);
        BOOST_TEST(success_count > 0);
    }

    void
    testWriteSomeEmpty()
    {
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            test::buffer_sink bs(f);
            any_buffer_sink abs(&bs);

            auto [ec, n] = co_await abs.write_some(const_buffer{});
            BOOST_TEST(!ec);
            BOOST_TEST_EQ(n, 0u);
            BOOST_TEST(bs.data().empty());
        });
        BOOST_TEST(r.success);
    }

    void
    testWriteEmpty()
    {
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            test::buffer_sink bs(f);
            any_buffer_sink abs(&bs);

            auto [ec, n] = co_await abs.write(const_buffer{});
            BOOST_TEST(!ec);
            BOOST_TEST_EQ(n, 0u);
            BOOST_TEST(bs.data().empty());
        });
        BOOST_TEST(r.success);
    }

    void
    testWriteEofEmpty()
    {
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            test::buffer_sink bs(f);
            any_buffer_sink abs(&bs);

            auto [ec, n] = co_await abs.write_eof(const_buffer{});
            if(ec)
                co_return;

            BOOST_TEST_EQ(n, 0u);
            BOOST_TEST(bs.data().empty());
            BOOST_TEST(bs.eof_called());
        });
        BOOST_TEST(r.success);
    }

    void
    testFuseErrorWriteSome()
    {
        int success_count = 0;
        int error_count = 0;

        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            test::buffer_sink bs(f);
            any_buffer_sink abs(&bs);

            auto buf = make_buffer("hello", 5);
            auto [ec, n] = co_await abs.write_some(buf);
            if(ec)
            {
                ++error_count;
                co_return;
            }
            ++success_count;
        });

        BOOST_TEST(r.success);
        BOOST_TEST(error_count > 0);
        BOOST_TEST(success_count > 0);
    }

    void
    testFuseErrorWrite()
    {
        int success_count = 0;
        int error_count = 0;

        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            test::buffer_sink bs(f);
            any_buffer_sink abs(&bs);

            auto buf = make_buffer("hello world", 11);
            auto [ec, n] = co_await abs.write(buf);
            if(ec)
            {
                ++error_count;
                co_return;
            }
            ++success_count;
        });

        BOOST_TEST(r.success);
        BOOST_TEST(error_count > 0);
        BOOST_TEST(success_count > 0);
    }

    void
    testFuseErrorWriteEofBuffers()
    {
        int success_count = 0;
        int error_count = 0;

        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            test::buffer_sink bs(f);
            any_buffer_sink abs(&bs);

            auto buf = make_buffer("final", 5);
            auto [ec, n] = co_await abs.write_eof(buf);
            if(ec)
            {
                ++error_count;
                co_return;
            }
            ++success_count;
        });

        BOOST_TEST(r.success);
        BOOST_TEST(error_count > 0);
        BOOST_TEST(success_count > 0);
    }

    void
    testFuseErrorWriteEof()
    {
        int success_count = 0;
        int error_count = 0;

        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            test::buffer_sink bs(f);
            any_buffer_sink abs(&bs);

            auto [ec] = co_await abs.write_eof();
            if(ec)
            {
                ++error_count;
                co_return;
            }
            ++success_count;
        });

        BOOST_TEST(r.success);
        BOOST_TEST(error_count > 0);
        BOOST_TEST(success_count > 0);
    }

    void
    testMoveOwning()
    {
        // Move construct from owning
        {
            test::fuse f;
            test::buffer_sink bs(f);
            any_buffer_sink abs1(std::move(bs));
            BOOST_TEST(abs1.has_value());

            any_buffer_sink abs2(std::move(abs1));
            BOOST_TEST(abs2.has_value());
            BOOST_TEST(!abs1.has_value());
        }

        // Move assign from owning into empty
        {
            test::fuse f;
            test::buffer_sink bs(f);
            any_buffer_sink abs1(std::move(bs));

            any_buffer_sink abs2;
            abs2 = std::move(abs1);
            BOOST_TEST(abs2.has_value());
            BOOST_TEST(!abs1.has_value());
        }

        // Move assign from owning over existing
        {
            test::fuse f;
            test::buffer_sink bs1(f);
            test::buffer_sink bs2(f);
            any_buffer_sink abs1(std::move(bs1));
            any_buffer_sink abs2(std::move(bs2));

            abs1 = std::move(abs2);
            BOOST_TEST(abs1.has_value());
            BOOST_TEST(!abs2.has_value());
        }
    }

    void
    testSelfMoveAssign()
    {
        test::fuse f;
        test::buffer_sink bs(f);
        any_buffer_sink abs(&bs);
        BOOST_TEST(abs.has_value());

        any_buffer_sink* p = &abs;
        any_buffer_sink* q = p;
        *p = std::move(*q);
        BOOST_TEST(abs.has_value());
    }

    //------------------------------------------------------
    // Native WriteSink forwarding tests (BufferSink+WriteSink)

    void
    testNativeWriteSome()
    {
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            buffer_write_sink bws(f);
            any_buffer_sink abs(&bws);

            auto buf = make_buffer("hello", 5);
            auto [ec, n] = co_await abs.write_some(buf);
            if(ec)
                co_return;

            BOOST_TEST(n > 0);
            BOOST_TEST(n <= 5u);
            BOOST_TEST(bws.write_api_used());
            BOOST_TEST_EQ(bws.data(),
                std::string_view("hello", n));
        });
        BOOST_TEST(r.success);
    }

    void
    testNativeWrite()
    {
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            buffer_write_sink bws(f);
            any_buffer_sink abs(&bws);

            auto buf = make_buffer("hello world", 11);
            auto [ec, n] = co_await abs.write(buf);
            if(ec)
                co_return;

            BOOST_TEST_EQ(n, 11u);
            BOOST_TEST(bws.write_api_used());
            BOOST_TEST_EQ(bws.data(), "hello world");
        });
        BOOST_TEST(r.success);
    }

    void
    testNativeWriteEofWithBuffers()
    {
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            buffer_write_sink bws(f);
            any_buffer_sink abs(&bws);

            auto buf = make_buffer("final", 5);
            auto [ec, n] = co_await abs.write_eof(buf);
            if(ec)
                co_return;

            BOOST_TEST_EQ(n, 5u);
            BOOST_TEST(bws.write_api_used());
            BOOST_TEST_EQ(bws.data(), "final");
            BOOST_TEST(bws.eof_called());
        });
        BOOST_TEST(r.success);
    }

    void
    testNativeWriteEofNoArg()
    {
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            buffer_write_sink bws(f);
            any_buffer_sink abs(&bws);

            auto [ec] = co_await abs.write_eof();
            if(ec)
                co_return;

            BOOST_TEST(bws.write_api_used());
            BOOST_TEST(bws.data().empty());
            BOOST_TEST(bws.eof_called());
        });
        BOOST_TEST(r.success);
    }

    void
    testNativeWriteThenEof()
    {
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            buffer_write_sink bws(f);
            any_buffer_sink abs(&bws);

            auto buf = make_buffer("payload", 7);
            auto [ec1, n] = co_await abs.write(buf);
            if(ec1)
                co_return;

            BOOST_TEST_EQ(n, 7u);
            BOOST_TEST(!bws.eof_called());

            auto [ec2] = co_await abs.write_eof();
            if(ec2)
                co_return;

            BOOST_TEST_EQ(bws.data(), "payload");
            BOOST_TEST(bws.eof_called());
        });
        BOOST_TEST(r.success);
    }

    void
    testNativeWriteSomeEmpty()
    {
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            buffer_write_sink bws(f);
            any_buffer_sink abs(&bws);

            auto [ec, n] = co_await abs.write_some(const_buffer{});
            BOOST_TEST(!ec);
            BOOST_TEST_EQ(n, 0u);
        });
        BOOST_TEST(r.success);
    }

    void
    testNativeWriteEmpty()
    {
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            buffer_write_sink bws(f);
            any_buffer_sink abs(&bws);

            auto [ec, n] = co_await abs.write(const_buffer{});
            BOOST_TEST(!ec);
            BOOST_TEST_EQ(n, 0u);
        });
        BOOST_TEST(r.success);
    }

    void
    testNativeWriteEofEmpty()
    {
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            buffer_write_sink bws(f);
            any_buffer_sink abs(&bws);

            auto [ec, n] = co_await abs.write_eof(const_buffer{});
            if(ec)
                co_return;

            BOOST_TEST_EQ(n, 0u);
            BOOST_TEST(bws.eof_called());
        });
        BOOST_TEST(r.success);
    }

    void
    testNativeOwning()
    {
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            buffer_write_sink bws(f);
            any_buffer_sink abs(std::move(bws));

            auto buf = make_buffer("owned", 5);
            auto [ec, n] = co_await abs.write_eof(buf);
            if(ec)
                co_return;

            BOOST_TEST_EQ(n, 5u);
        });
        BOOST_TEST(r.success);
    }

    void
    testNativePrepareCommit()
    {
        // BufferSink API still works when WriteSink is also present
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            buffer_write_sink bws(f);
            any_buffer_sink abs(&bws);

            mutable_buffer arr[detail::max_iovec_];
            auto bufs = abs.prepare(arr);
            BOOST_TEST_EQ(bufs.size(), 1u);

            std::memcpy(bufs[0].data(), "buf-api", 7);

            auto [ec] = co_await abs.commit(7);
            if(ec)
                co_return;

            // BufferSink API used, not WriteSink
            BOOST_TEST(!bws.write_api_used());
            BOOST_TEST_EQ(bws.data(), "buf-api");
        });
        BOOST_TEST(r.success);
    }

    //------------------------------------------------------
    // pull_from tests

    void
    testPullFromReadStream()
    {
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            test::read_stream src(f);
            src.provide("hello world");

            test::buffer_sink sink(f);

            auto [ec, n] = co_await pull_from(src, sink);
            if(ec)
                co_return;

            BOOST_TEST_EQ(n, 11u);
            BOOST_TEST_EQ(sink.data(), "hello world");
            BOOST_TEST(sink.eof_called());
        });
        BOOST_TEST(r.success);
    }

    void
    testPullFromReadStreamTypeErased()
    {
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            test::read_stream src(f);
            src.provide("hello world");

            test::buffer_sink sink(f);
            any_buffer_sink abs(&sink);

            auto [ec, n] = co_await pull_from(src, abs);
            if(ec)
                co_return;

            BOOST_TEST_EQ(n, 11u);
            BOOST_TEST_EQ(sink.data(), "hello world");
            BOOST_TEST(sink.eof_called());
        });
        BOOST_TEST(r.success);
    }

    void
    testPullFromReadStreamChunked()
    {
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            test::read_stream src(f, 5); // max 5 bytes per read
            src.provide("hello world");

            test::buffer_sink sink(f);

            auto [ec, n] = co_await pull_from(src, sink);
            if(ec)
                co_return;

            BOOST_TEST_EQ(n, 11u);
            BOOST_TEST_EQ(sink.data(), "hello world");
            BOOST_TEST(sink.eof_called());
        });
        BOOST_TEST(r.success);
    }

    void
    testPullFromReadStreamEmpty()
    {
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            test::read_stream src(f);
            // No data provided

            test::buffer_sink sink(f);

            auto [ec, n] = co_await pull_from(src, sink);
            if(ec)
                co_return;

            BOOST_TEST_EQ(n, 0u);
            BOOST_TEST(sink.data().empty());
            BOOST_TEST(sink.eof_called());
        });
        BOOST_TEST(r.success);
    }

    void
    testPullFromReadSource()
    {
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            test::read_source src(f);
            src.provide("hello world");

            test::buffer_sink sink(f);

            auto [ec, n] = co_await pull_from(src, sink);
            if(ec)
                co_return;

            BOOST_TEST_EQ(n, 11u);
            BOOST_TEST_EQ(sink.data(), "hello world");
            BOOST_TEST(sink.eof_called());
        });
        BOOST_TEST(r.success);
    }

    void
    testPullFromReadSourceTypeErased()
    {
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            test::read_source src(f);
            src.provide("hello world");

            test::buffer_sink sink(f);
            any_buffer_sink abs(&sink);

            auto [ec, n] = co_await pull_from(src, abs);
            if(ec)
                co_return;

            BOOST_TEST_EQ(n, 11u);
            BOOST_TEST_EQ(sink.data(), "hello world");
            BOOST_TEST(sink.eof_called());
        });
        BOOST_TEST(r.success);
    }

    void
    testPullFromReadSourceChunked()
    {
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            test::read_source src(f, 5); // max 5 bytes per read
            src.provide("hello world");

            test::buffer_sink sink(f);

            auto [ec, n] = co_await pull_from(src, sink);
            if(ec)
                co_return;

            BOOST_TEST_EQ(n, 11u);
            BOOST_TEST_EQ(sink.data(), "hello world");
            BOOST_TEST(sink.eof_called());
        });
        BOOST_TEST(r.success);
    }

    void
    testPullFromReadSourceEmpty()
    {
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            test::read_source src(f);
            // No data provided

            test::buffer_sink sink(f);

            auto [ec, n] = co_await pull_from(src, sink);
            if(ec)
                co_return;

            BOOST_TEST_EQ(n, 0u);
            BOOST_TEST(sink.data().empty());
            BOOST_TEST(sink.eof_called());
        });
        BOOST_TEST(r.success);
    }

    void
    run()
    {
        testConstruct();
        testConstructOwning();
        testMove();
        testMoveAssignOverExisting();
        testPrepareCommit();
        testCommitWithEof();
        testCommitEof();
        testMultipleCommits();
        testEmptyCommit();
        testWriteSome();
        testWrite();
        testWriteEofWithBuffers();
        testWriteEofNoArg();
        testWriteThenEof();
        testFuseErrorCommit();
        testFuseErrorCommitEof();
        testWriteSomeEmpty();
        testWriteEmpty();
        testWriteEofEmpty();
        testFuseErrorWriteSome();
        testFuseErrorWrite();
        testFuseErrorWriteEofBuffers();
        testFuseErrorWriteEof();
        testMoveOwning();
        testSelfMoveAssign();
        testNativeWriteSome();
        testNativeWrite();
        testNativeWriteEofWithBuffers();
        testNativeWriteEofNoArg();
        testNativeWriteThenEof();
        testNativeWriteSomeEmpty();
        testNativeWriteEmpty();
        testNativeWriteEofEmpty();
        testNativeOwning();
        testNativePrepareCommit();
        testPullFromReadStream();
        testPullFromReadStreamTypeErased();
        testPullFromReadStreamChunked();
        testPullFromReadStreamEmpty();
        testPullFromReadSource();
        testPullFromReadSourceTypeErased();
        testPullFromReadSourceChunked();
        testPullFromReadSourceEmpty();
    }
};

TEST_SUITE(any_buffer_sink_test, "boost.capy.io.any_buffer_sink");

} // namespace
} // namespace capy
} // namespace boost
