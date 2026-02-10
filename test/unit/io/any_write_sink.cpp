//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Test that header file is self-contained.
#include <boost/capy/io/any_write_sink.hpp>

#include <boost/capy/buffers/make_buffer.hpp>
#include <boost/capy/ex/executor_ref.hpp>
#include <boost/capy/ex/io_env.hpp>
#include <boost/capy/io_result.hpp>
#include <boost/capy/task.hpp>
#include <boost/capy/test/run_blocking.hpp>
#include <boost/capy/test/write_sink.hpp>

#include "test/unit/test_helpers.hpp"

#include <boost/capy/detail/config.hpp>

#include <array>
#include <coroutine>
#include <stop_token>
#include <string_view>
#include <vector>

namespace boost {
namespace capy {

static_assert(WriteSink<any_write_sink>);

namespace {

struct pending_sink_awaitable
{
    int* counter_;
    pending_sink_awaitable(int* c) : counter_(c) {}
    pending_sink_awaitable(pending_sink_awaitable&& o) noexcept
        : counter_(std::exchange(o.counter_, nullptr)) {}
    ~pending_sink_awaitable() { if(counter_) ++(*counter_); }
    bool await_ready() const noexcept { return false; }
    std::coroutine_handle<> await_suspend(std::coroutine_handle<>, io_env const&)
        { return std::noop_coroutine(); }
    io_result<std::size_t> await_resume()
        { return {{}, 0}; }
};

struct pending_sink_eof_awaitable
{
    int* counter_;
    pending_sink_eof_awaitable(int* c) : counter_(c) {}
    pending_sink_eof_awaitable(pending_sink_eof_awaitable&& o) noexcept
        : counter_(std::exchange(o.counter_, nullptr)) {}
    ~pending_sink_eof_awaitable() { if(counter_) ++(*counter_); }
    bool await_ready() const noexcept { return false; }
    std::coroutine_handle<> await_suspend(std::coroutine_handle<>, io_env const&)
        { return std::noop_coroutine(); }
    io_result<> await_resume()
        { return {}; }
};

struct pending_write_sink
{
    int* counter_;
    pending_sink_awaitable write_some(
        ConstBufferSequence auto)
        { return pending_sink_awaitable{counter_}; }
    pending_sink_awaitable write(
        ConstBufferSequence auto)
        { return pending_sink_awaitable{counter_}; }
    pending_sink_awaitable write_eof(
        ConstBufferSequence auto)
        { return pending_sink_awaitable{counter_}; }
    pending_sink_eof_awaitable write_eof()
        { return pending_sink_eof_awaitable{counter_}; }
};

class any_write_sink_test
{
public:
    void
    testConstruct()
    {
        // Default construct
        {
            any_write_sink aws;
            BOOST_TEST(!aws.has_value());
            BOOST_TEST(!aws);
        }

        // Construct from sink
        {
            test::fuse f;
            test::write_sink ws(f);
            any_write_sink aws(&ws);
            BOOST_TEST(aws.has_value());
            BOOST_TEST(static_cast<bool>(aws));
        }
    }

    void
    testMove()
    {
        test::fuse f;
        test::write_sink ws(f);

        any_write_sink aws1(&ws);
        BOOST_TEST(aws1.has_value());

        // Move construct
        any_write_sink aws2(std::move(aws1));
        BOOST_TEST(aws2.has_value());
        BOOST_TEST(!aws1.has_value());

        // Move assign
        any_write_sink aws3;
        aws3 = std::move(aws2);
        BOOST_TEST(aws3.has_value());
        BOOST_TEST(!aws2.has_value());
    }

    void
    testWriteSome()
    {
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            test::write_sink ws(f);

            any_write_sink aws(&ws);

            auto [ec, n] = co_await aws.write_some(
                make_buffer("hello world", 11));
            if(ec)
                co_return;

            BOOST_TEST_EQ(n, 11u);
            BOOST_TEST_EQ(ws.data(), "hello world");
        });
        BOOST_TEST(r.success);
    }

    void
    testWriteSomePartial()
    {
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            test::write_sink ws(f, 5);

            any_write_sink aws(&ws);

            auto [ec, n] = co_await aws.write_some(
                make_buffer("hello world", 11));
            if(ec)
                co_return;

            BOOST_TEST_EQ(n, 5u);
            BOOST_TEST_EQ(ws.data(), "hello");
        });
        BOOST_TEST(r.success);
    }

    void
    testWriteSomeMultiple()
    {
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            test::write_sink ws(f);

            any_write_sink aws(&ws);

            auto [ec1, n1] = co_await aws.write_some(
                make_buffer("hello", 5));
            if(ec1)
                co_return;
            BOOST_TEST_EQ(n1, 5u);

            auto [ec2, n2] = co_await aws.write_some(
                make_buffer(" ", 1));
            if(ec2)
                co_return;
            BOOST_TEST_EQ(n2, 1u);

            auto [ec3, n3] = co_await aws.write_some(
                make_buffer("world", 5));
            if(ec3)
                co_return;
            BOOST_TEST_EQ(n3, 5u);

            BOOST_TEST_EQ(ws.data(), "hello world");
        });
        BOOST_TEST(r.success);
    }

    void
    testWriteSomeEmptyBuffer()
    {
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            test::write_sink ws(f);

            any_write_sink aws(&ws);

            auto [ec, n] = co_await aws.write_some(const_buffer());
            if(ec)
                co_return;

            BOOST_TEST_EQ(n, 0u);
            BOOST_TEST(ws.data().empty());
        });
        BOOST_TEST(r.success);
    }

    void
    testWriteEmptyBuffer()
    {
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            test::write_sink ws(f);

            any_write_sink aws(&ws);

            auto [ec, n] = co_await aws.write(const_buffer());
            if(ec)
                co_return;

            BOOST_TEST_EQ(n, 0u);
            BOOST_TEST(ws.data().empty());
        });
        BOOST_TEST(r.success);
    }

    void
    testWrite()
    {
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            test::write_sink ws(f, 5); // max 5 bytes per write

            any_write_sink aws(&ws);

            auto [ec, n] = co_await aws.write(
                make_buffer("hello world", 11));
            if(ec)
                co_return;

            BOOST_TEST_EQ(n, 11u);
            BOOST_TEST_EQ(ws.data(), "hello world");
            BOOST_TEST(!ws.eof_called());
        });
        BOOST_TEST(r.success);
    }

    void
    testWriteMultiple()
    {
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            test::write_sink ws(f);

            any_write_sink aws(&ws);

            auto [ec1, n1] = co_await aws.write(
                make_buffer("hello", 5));
            if(ec1)
                co_return;
            BOOST_TEST_EQ(n1, 5u);

            auto [ec2, n2] = co_await aws.write(
                make_buffer(" ", 1));
            if(ec2)
                co_return;
            BOOST_TEST_EQ(n2, 1u);

            auto [ec3, n3] = co_await aws.write(
                make_buffer("world", 5));
            if(ec3)
                co_return;
            BOOST_TEST_EQ(n3, 5u);

            BOOST_TEST_EQ(ws.data(), "hello world");
        });
        BOOST_TEST(r.success);
    }

    void
    testWriteBufferSequence()
    {
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            test::write_sink ws(f);

            any_write_sink aws(&ws);

            std::array<const_buffer, 2> buffers = {{
                make_buffer("hello", 5),
                make_buffer("world", 5)
            }};

            auto [ec, n] = co_await aws.write(buffers);
            if(ec)
                co_return;

            BOOST_TEST_EQ(n, 10u);
            BOOST_TEST_EQ(ws.data(), "helloworld");
        });
        BOOST_TEST(r.success);
    }

    void
    testWriteSingleBuffer()
    {
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            test::write_sink ws(f);

            any_write_sink aws(&ws);

            auto [ec, n] = co_await aws.write(
                make_buffer("hello world", 11));
            if(ec)
                co_return;

            BOOST_TEST_EQ(n, 11u);
            BOOST_TEST_EQ(ws.data(), "hello world");
        });
        BOOST_TEST(r.success);
    }

    void
    testWriteEofWithBuffers()
    {
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            test::write_sink ws(f);

            any_write_sink aws(&ws);

            auto [ec, n] = co_await aws.write_eof(
                make_buffer("hello", 5));
            if(ec)
                co_return;

            BOOST_TEST_EQ(n, 5u);
            BOOST_TEST_EQ(ws.data(), "hello");
            BOOST_TEST(ws.eof_called());
        });
        BOOST_TEST(r.success);
    }

    void
    testWriteEofWithEmptyBuffers()
    {
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            test::write_sink ws(f);

            any_write_sink aws(&ws);

            auto [ec, n] = co_await aws.write_eof(const_buffer());
            if(ec)
                co_return;

            BOOST_TEST_EQ(n, 0u);
            BOOST_TEST(ws.data().empty());
            BOOST_TEST(ws.eof_called());
        });
        BOOST_TEST(r.success);
    }

    void
    testWriteEof()
    {
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            test::write_sink ws(f);

            any_write_sink aws(&ws);

            auto [ec] = co_await aws.write_eof();
            if(ec)
                co_return;

            BOOST_TEST(ws.eof_called());
        });
        BOOST_TEST(r.success);
    }

    void
    testWriteThenWriteEof()
    {
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            test::write_sink ws(f);

            any_write_sink aws(&ws);

            auto [ec1, n] = co_await aws.write(
                make_buffer("hello", 5));
            if(ec1)
                co_return;
            BOOST_TEST_EQ(n, 5u);
            BOOST_TEST(!ws.eof_called());

            auto [ec2] = co_await aws.write_eof();
            if(ec2)
                co_return;
            BOOST_TEST(ws.eof_called());
            BOOST_TEST_EQ(ws.data(), "hello");
        });
        BOOST_TEST(r.success);
    }

    void
    testWriteArray()
    {
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            test::write_sink ws(f);

            any_write_sink aws(&ws);

            std::array<const_buffer, 2> buffers = {{
                make_buffer("hello", 5),
                make_buffer("world", 5)
            }};

            auto [ec, n] = co_await aws.write(buffers);
            if(ec)
                co_return;

            BOOST_TEST_EQ(n, 10u);
            BOOST_TEST_EQ(ws.data(), "helloworld");
        });
        BOOST_TEST(r.success);
    }

    void
    testWritePartial()
    {
        // Verify that any_write_sink loops to consume all data
        // even when underlying sink has max_write_size
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            test::write_sink ws(f, 5); // max 5 bytes per write

            any_write_sink aws(&ws);

            auto [ec, n] = co_await aws.write(
                make_buffer("hello world", 11));
            if(ec)
                co_return;

            BOOST_TEST_EQ(n, 11u);
            BOOST_TEST_EQ(ws.data(), "hello world");
        });
        BOOST_TEST(r.success);
    }

    void
    testWriteEofWithBuffersPartial()
    {
        // Verify that any_write_sink loops to consume all data
        // and signals eof even when underlying sink has max_write_size
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            test::write_sink ws(f, 5); // max 5 bytes per write

            any_write_sink aws(&ws);

            auto [ec, n] = co_await aws.write_eof(
                make_buffer("hello world", 11));
            if(ec)
                co_return;

            BOOST_TEST_EQ(n, 11u);
            BOOST_TEST_EQ(ws.data(), "hello world");
            BOOST_TEST(ws.eof_called());
        });
        BOOST_TEST(r.success);
    }

    void
    testConstructOwning()
    {
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            test::write_sink ws(f);
            any_write_sink aws{std::move(ws)};
            BOOST_TEST(aws.has_value());

            auto [ec, n] = co_await aws.write_some(
                make_buffer("hello", 5));
            if(ec)
                co_return;
            BOOST_TEST_EQ(n, 5u);
        });
        BOOST_TEST(r.success);
    }

    void
    testWriteManyBuffers()
    {
        // Buffer sequence exceeds max_iovec_ -- verifies the
        // windowed loop writes every buffer in the sequence.
        constexpr unsigned N = detail::max_iovec_ + 4;

        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            test::write_sink ws(f);
            any_write_sink aws(&ws);

            std::string expected;
            std::vector<std::string> strings;
            std::vector<const_buffer> buffers;
            for(unsigned i = 0; i < N; ++i)
            {
                strings.push_back(std::string(1,
                    static_cast<char>('a' + (i % 26))));
                expected += strings.back();
            }
            for(auto const& s : strings)
                buffers.emplace_back(s.data(), s.size());

            auto [ec, n] = co_await aws.write(buffers);
            if(ec)
                co_return;

            BOOST_TEST_EQ(n, std::size_t(N));
            BOOST_TEST_EQ(ws.data(), expected);
            BOOST_TEST(!ws.eof_called());
        });
        BOOST_TEST(r.success);
    }

    void
    testWriteEofManyBuffers()
    {
        // Buffer sequence exceeds max_iovec_ -- verifies the
        // last window is sent atomically with EOF via write_eof(buffers).
        constexpr unsigned N = detail::max_iovec_ + 4;

        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            test::write_sink ws(f);
            any_write_sink aws(&ws);

            std::string expected;
            std::vector<std::string> strings;
            std::vector<const_buffer> buffers;
            for(unsigned i = 0; i < N; ++i)
            {
                strings.push_back(std::string(1,
                    static_cast<char>('a' + (i % 26))));
                expected += strings.back();
            }
            for(auto const& s : strings)
                buffers.emplace_back(s.data(), s.size());

            auto [ec, n] = co_await aws.write_eof(buffers);
            if(ec)
                co_return;

            BOOST_TEST_EQ(n, std::size_t(N));
            BOOST_TEST_EQ(ws.data(), expected);
            BOOST_TEST(ws.eof_called());
        });
        BOOST_TEST(r.success);
    }

    void
    testDestroyWithActiveWriteAwaitable()
    {
        // Split vtable: active_write_ops_ set in await_suspend.
        int destroyed = 0;
        pending_write_sink ps{&destroyed};
        {
            any_write_sink aws(&ps);
            char const data[] = "x";
            auto aw = aws.write_some(const_buffer(data, 1));
            BOOST_TEST(!aw.await_ready());

            test::blocking_context bctx;
            auto ex = bctx.get_executor();
            aw.await_suspend(
                std::noop_coroutine(), io_env{executor_ref(ex), {}});
        }
        BOOST_TEST_EQ(destroyed, 1);
    }

    void
    testDestroyWithActiveEofAwaitable()
    {
        // Split vtable: active_eof_ops_ set in await_suspend.
        int destroyed = 0;
        pending_write_sink ps{&destroyed};
        {
            any_write_sink aws(&ps);
            auto aw = aws.write_eof();
            BOOST_TEST(!aw.await_ready());

            test::blocking_context bctx;
            auto ex = bctx.get_executor();
            aw.await_suspend(
                std::noop_coroutine(), io_env{executor_ref(ex), {}});
        }
        BOOST_TEST_EQ(destroyed, 1);
    }

    void
    testMoveAssignWithActiveAwaitable()
    {
        int destroyed = 0;
        pending_write_sink ps{&destroyed};
        {
            any_write_sink aws(&ps);
            char const data[] = "x";
            auto aw = aws.write_some(const_buffer(data, 1));
            BOOST_TEST(!aw.await_ready());

            test::blocking_context bctx;
            auto ex = bctx.get_executor();
            aw.await_suspend(
                std::noop_coroutine(), io_env{executor_ref(ex), {}});

            any_write_sink empty;
            aws = std::move(empty);
            BOOST_TEST_EQ(destroyed, 1);
        }
    }

    void
    run()
    {
        testConstruct();
        testConstructOwning();
        testMove();
        testWriteSome();
        testWriteSomePartial();
        testWriteSomeMultiple();
        testWriteSomeEmptyBuffer();
        testWriteEmptyBuffer();
        testWrite();
        testWriteMultiple();
        testWriteBufferSequence();
        testWriteSingleBuffer();
        testWriteManyBuffers();
        testWriteEofWithBuffers();
        testWriteEofWithEmptyBuffers();
        testWriteEof();
        testWriteThenWriteEof();
        testWriteArray();
        testWritePartial();
        testWriteEofWithBuffersPartial();
        testWriteEofManyBuffers();
        testDestroyWithActiveWriteAwaitable();
        testDestroyWithActiveEofAwaitable();
        testMoveAssignWithActiveAwaitable();
    }
};

TEST_SUITE(any_write_sink_test, "boost.capy.io.any_write_sink");

} // namespace
} // namespace capy
} // namespace boost
