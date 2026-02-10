//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Test that header file is self-contained.
#include <boost/capy/io/any_read_source.hpp>

#include <boost/capy/buffers/make_buffer.hpp>
#include <boost/capy/cond.hpp>
#include <boost/capy/ex/executor_ref.hpp>
#include <boost/capy/ex/io_env.hpp>
#include <boost/capy/io_result.hpp>
#include <boost/capy/task.hpp>
#include <boost/capy/test/read_source.hpp>
#include <boost/capy/test/run_blocking.hpp>

#include "test/unit/test_helpers.hpp"

#include <boost/capy/detail/config.hpp>

#include <array>
#include <coroutine>
#include <stop_token>
#include <string_view>

namespace boost {
namespace capy {

static_assert(ReadSource<any_read_source>);

namespace {

struct pending_source_awaitable
{
    int* counter_;
    pending_source_awaitable(int* c) : counter_(c) {}
    pending_source_awaitable(pending_source_awaitable&& o) noexcept
        : counter_(std::exchange(o.counter_, nullptr)) {}
    ~pending_source_awaitable() { if(counter_) ++(*counter_); }
    bool await_ready() const noexcept { return false; }
    std::coroutine_handle<> await_suspend(std::coroutine_handle<>, io_env const&)
        { return std::noop_coroutine(); }
    io_result<std::size_t> await_resume()
        { return {{}, 0}; }
};

struct pending_read_source
{
    int* counter_;
    pending_source_awaitable read_some(
        MutableBufferSequence auto)
        { return pending_source_awaitable{counter_}; }
    pending_source_awaitable read(
        MutableBufferSequence auto)
        { return pending_source_awaitable{counter_}; }
};

class any_read_source_test
{
public:
    void
    testConstruct()
    {
        // Default construct
        {
            any_read_source ars;
            BOOST_TEST(!ars.has_value());
            BOOST_TEST(!ars);
        }

        // Construct from source
        {
            test::fuse f;
            test::read_source rs(f);
            any_read_source ars(&rs);
            BOOST_TEST(ars.has_value());
            BOOST_TEST(static_cast<bool>(ars));
        }
    }

    void
    testConstructOwning()
    {
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            test::read_source rs(f);
            rs.provide("owned");

            any_read_source ars(std::move(rs));
            BOOST_TEST(ars.has_value());
            BOOST_TEST(static_cast<bool>(ars));

            char buf[5] = {};
            auto [ec, n] = co_await ars.read_some(make_buffer(buf));
            if(ec)
                co_return;

            BOOST_TEST_EQ(n, 5u);
            BOOST_TEST_EQ(std::string_view(buf, n), "owned");
        });
        BOOST_TEST(r.success);
    }

    void
    testMove()
    {
        test::fuse f;
        test::read_source rs(f);

        any_read_source ars1(&rs);
        BOOST_TEST(ars1.has_value());

        // Move construct
        any_read_source ars2(std::move(ars1));
        BOOST_TEST(ars2.has_value());
        BOOST_TEST(!ars1.has_value());

        // Move assign to empty
        any_read_source ars3;
        ars3 = std::move(ars2);
        BOOST_TEST(ars3.has_value());
        BOOST_TEST(!ars2.has_value());
    }

    void
    testMoveAssignNonEmpty()
    {
        test::fuse f;
        test::read_source rs1(f);
        test::read_source rs2(f);

        any_read_source ars1(&rs1);
        any_read_source ars2(&rs2);
        BOOST_TEST(ars1.has_value());
        BOOST_TEST(ars2.has_value());

        // Move assign over non-empty target
        ars1 = std::move(ars2);
        BOOST_TEST(ars1.has_value());
        BOOST_TEST(!ars2.has_value());
    }

    void
    testSelfAssign()
    {
        test::fuse f;
        test::read_source rs(f);

        any_read_source ars(&rs);
        BOOST_TEST(ars.has_value());

        // Indirect self-assignment should be a no-op
        auto& ref = ars;
        ars = std::move(ref);
        BOOST_TEST(ars.has_value());
    }

    void
    testReadSome()
    {
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            test::read_source rs(f);
            rs.provide("hello world");

            any_read_source ars(&rs);

            char buf[32] = {};
            auto [ec, n] = co_await ars.read_some(make_buffer(buf));
            if(ec)
                co_return;

            BOOST_TEST_EQ(n, 11u);
            BOOST_TEST_EQ(std::string_view(buf, n), "hello world");
        });
        BOOST_TEST(r.success);
    }

    void
    testReadSomePartial()
    {
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            test::read_source rs(f);
            rs.provide("hello world");

            any_read_source ars(&rs);

            char buf[5] = {};
            auto [ec, n] = co_await ars.read_some(make_buffer(buf));
            if(ec)
                co_return;

            BOOST_TEST_EQ(n, 5u);
            BOOST_TEST_EQ(std::string_view(buf, n), "hello");
            BOOST_TEST_EQ(rs.available(), 6u);
        });
        BOOST_TEST(r.success);
    }

    void
    testReadSomeMultiple()
    {
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            test::read_source rs(f);
            rs.provide("abcdefghij");

            any_read_source ars(&rs);

            char buf[3] = {};

            auto [ec1, n1] = co_await ars.read_some(make_buffer(buf));
            if(ec1)
                co_return;
            BOOST_TEST_EQ(n1, 3u);
            BOOST_TEST_EQ(std::string_view(buf, n1), "abc");

            auto [ec2, n2] = co_await ars.read_some(make_buffer(buf));
            if(ec2)
                co_return;
            BOOST_TEST_EQ(n2, 3u);
            BOOST_TEST_EQ(std::string_view(buf, n2), "def");

            auto [ec3, n3] = co_await ars.read_some(make_buffer(buf));
            if(ec3)
                co_return;
            BOOST_TEST_EQ(n3, 3u);
            BOOST_TEST_EQ(std::string_view(buf, n3), "ghi");
        });
        BOOST_TEST(r.success);
    }

    void
    testReadSomeEof()
    {
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            test::read_source rs(f);

            any_read_source ars(&rs);

            char buf[32] = {};
            auto [ec, n] = co_await ars.read_some(make_buffer(buf));
            if(ec && ec != cond::eof)
                co_return;

            BOOST_TEST(ec == cond::eof);
            BOOST_TEST_EQ(n, 0u);
        });
        BOOST_TEST(r.success);
    }

    void
    testReadSomeBufferSequence()
    {
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            test::read_source rs(f);
            rs.provide("helloworld");

            any_read_source ars(&rs);

            char buf1[5] = {};
            char buf2[5] = {};
            std::array<mutable_buffer, 2> buffers = {{
                make_buffer(buf1),
                make_buffer(buf2)
            }};

            auto [ec, n] = co_await ars.read_some(buffers);
            if(ec)
                co_return;

            BOOST_TEST_EQ(n, 10u);
            BOOST_TEST_EQ(std::string_view(buf1, 5), "hello");
            BOOST_TEST_EQ(std::string_view(buf2, 5), "world");
        });
        BOOST_TEST(r.success);
    }

    void
    testReadSomeEmptyBuffer()
    {
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            test::read_source rs(f);
            rs.provide("data");

            any_read_source ars(&rs);

            auto [ec, n] = co_await ars.read_some(mutable_buffer());
            if(ec)
                co_return;

            BOOST_TEST_EQ(n, 0u);
            BOOST_TEST_EQ(rs.available(), 4u);
        });
        BOOST_TEST(r.success);
    }

    void
    testRead()
    {
        // Buffer exactly matches available data
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            test::read_source rs(f);
            rs.provide("hello world");

            any_read_source ars(&rs);

            char buf[11] = {};
            auto [ec, n] = co_await ars.read(make_buffer(buf, 11));
            if(ec)
                co_return;

            BOOST_TEST_EQ(n, 11u);
            BOOST_TEST_EQ(std::string_view(buf, n), "hello world");
        });
        BOOST_TEST(r.success);
    }

    void
    testReadPartial()
    {
        // Buffer smaller than available data - fills completely
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            test::read_source rs(f);
            rs.provide("hello world");

            any_read_source ars(&rs);

            char buf[5] = {};
            auto [ec, n] = co_await ars.read(make_buffer(buf));
            if(ec)
                co_return;

            BOOST_TEST_EQ(n, 5u);
            BOOST_TEST_EQ(std::string_view(buf, n), "hello");
            BOOST_TEST_EQ(rs.available(), 6u);
        });
        BOOST_TEST(r.success);
    }

    void
    testReadMultiple()
    {
        // Multiple reads that exactly consume available data
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            test::read_source rs(f);
            rs.provide("abcdefghi");

            any_read_source ars(&rs);

            char buf[3] = {};

            auto [ec1, n1] = co_await ars.read(make_buffer(buf));
            if(ec1)
                co_return;
            BOOST_TEST_EQ(n1, 3u);
            BOOST_TEST_EQ(std::string_view(buf, n1), "abc");

            auto [ec2, n2] = co_await ars.read(make_buffer(buf));
            if(ec2)
                co_return;
            BOOST_TEST_EQ(n2, 3u);
            BOOST_TEST_EQ(std::string_view(buf, n2), "def");

            auto [ec3, n3] = co_await ars.read(make_buffer(buf));
            if(ec3)
                co_return;
            BOOST_TEST_EQ(n3, 3u);
            BOOST_TEST_EQ(std::string_view(buf, n3), "ghi");
        });
        BOOST_TEST(r.success);
    }

    void
    testReadInsufficientData()
    {
        // Buffer larger than available data - fails with EOF
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            test::read_source rs(f);
            rs.provide("hi");

            any_read_source ars(&rs);

            char buf[10] = {};
            auto [ec, n] = co_await ars.read(make_buffer(buf));
            // Should fail because buffer can't be filled
            if(ec && ec != cond::eof)
                co_return; // fuse-injected error
            BOOST_TEST(ec == cond::eof);
            BOOST_TEST_EQ(n, 2u); // 2 bytes read before EOF
        });
        BOOST_TEST(r.success);
    }

    void
    testReadEof()
    {
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            test::read_source rs(f);
            // No data provided - should get EOF

            any_read_source ars(&rs);

            char buf[32] = {};
            auto [ec, n] = co_await ars.read(make_buffer(buf));
            if(ec && ec != cond::eof)
                co_return;

            BOOST_TEST(ec == cond::eof);
            BOOST_TEST_EQ(n, 0u);
        });
        BOOST_TEST(r.success);
    }

    void
    testReadEofAfterData()
    {
        // Read exact amount, then get EOF on next read
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            test::read_source rs(f);
            rs.provide("x");

            any_read_source ars(&rs);

            char buf[1] = {};

            auto [ec1, n1] = co_await ars.read(make_buffer(buf));
            if(ec1)
                co_return;
            BOOST_TEST_EQ(n1, 1u);

            auto [ec2, n2] = co_await ars.read(make_buffer(buf));
            // Should get EOF because no more data
            if(ec2 && ec2 != cond::eof)
                co_return; // fuse-injected error
            BOOST_TEST(ec2 == cond::eof);
            BOOST_TEST_EQ(n2, 0u);
        });
        BOOST_TEST(r.success);
    }

    void
    testReadBufferSequence()
    {
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            test::read_source rs(f);
            rs.provide("helloworld");

            any_read_source ars(&rs);

            char buf1[5] = {};
            char buf2[5] = {};
            std::array<mutable_buffer, 2> buffers = {{
                make_buffer(buf1),
                make_buffer(buf2)
            }};

            auto [ec, n] = co_await ars.read(buffers);
            if(ec)
                co_return;

            BOOST_TEST_EQ(n, 10u);
            BOOST_TEST_EQ(std::string_view(buf1, 5), "hello");
            BOOST_TEST_EQ(std::string_view(buf2, 5), "world");
        });
        BOOST_TEST(r.success);
    }

    void
    testReadSingleBuffer()
    {
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            test::read_source rs(f);
            rs.provide("hello world");

            any_read_source ars(&rs);

            char buf[11] = {};
            auto [ec, n] = co_await ars.read(make_buffer(buf));
            if(ec)
                co_return;

            BOOST_TEST_EQ(n, 11u);
            BOOST_TEST_EQ(std::string_view(buf, n), "hello world");
        });
        BOOST_TEST(r.success);
    }

    void
    testReadArray()
    {
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            test::read_source rs(f);
            rs.provide("helloworld");

            any_read_source ars(&rs);

            char buf1[5] = {};
            char buf2[5] = {};
            std::array<mutable_buffer, 2> buffers = {{
                make_buffer(buf1),
                make_buffer(buf2)
            }};

            auto [ec, n] = co_await ars.read(buffers);
            if(ec)
                co_return;

            BOOST_TEST_EQ(n, 10u);
            BOOST_TEST_EQ(std::string_view(buf1, 5), "hello");
            BOOST_TEST_EQ(std::string_view(buf2, 5), "world");
        });
        BOOST_TEST(r.success);
    }

    void
    testReadEmpty()
    {
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            test::read_source rs(f);
            rs.provide("data");

            any_read_source ars(&rs);

            auto [ec, n] = co_await ars.read(mutable_buffer());
            if(ec)
                co_return;

            BOOST_TEST_EQ(n, 0u);
            BOOST_TEST_EQ(rs.available(), 4u);
        });
        BOOST_TEST(r.success);
    }

    void
    testReadWithMaxReadSize()
    {
        // Verify read forwards to underlying source's read which
        // fills the buffer ignoring max_read_size
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            test::read_source rs(f, 5);
            rs.provide("hello world");

            any_read_source ars(&rs);

            char buf[11] = {};
            auto [ec, n] = co_await ars.read(make_buffer(buf));
            if(ec)
                co_return;

            BOOST_TEST_EQ(n, 11u);
            BOOST_TEST_EQ(std::string_view(buf, n), "hello world");
        });
        BOOST_TEST(r.success);
    }

    void
    testReadWithMaxReadSizeMultiple()
    {
        // Verify multiple reads forward to underlying source's read
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            test::read_source rs(f, 3);
            rs.provide("abcdefghij");

            any_read_source ars(&rs);

            char buf[5] = {};

            auto [ec1, n1] = co_await ars.read(make_buffer(buf));
            if(ec1)
                co_return;
            BOOST_TEST_EQ(n1, 5u);
            BOOST_TEST_EQ(std::string_view(buf, n1), "abcde");

            auto [ec2, n2] = co_await ars.read(make_buffer(buf));
            if(ec2)
                co_return;
            BOOST_TEST_EQ(n2, 5u);
            BOOST_TEST_EQ(std::string_view(buf, n2), "fghij");
        });
        BOOST_TEST(r.success);
    }

    void
    testReadManyBuffers()
    {
        // Buffer sequence exceeds max_iovec_ -- verifies the
        // windowed loop fills every buffer in the sequence.
        constexpr unsigned N = detail::max_iovec_ + 4;

        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            // Build data: "abcd..." repeating, one byte per buffer
            std::string data;
            for(unsigned i = 0; i < N; ++i)
                data.push_back(static_cast<char>('a' + (i % 26)));

            test::read_source rs(f);
            rs.provide(data);

            any_read_source ars(&rs);

            char storage[N] = {};
            std::array<mutable_buffer, N> buffers;
            for(unsigned i = 0; i < N; ++i)
                buffers[i] = mutable_buffer(&storage[i], 1);

            auto [ec, n] = co_await ars.read(buffers);
            if(ec)
                co_return;

            BOOST_TEST_EQ(n, std::size_t(N));
            for(unsigned i = 0; i < N; ++i)
                BOOST_TEST_EQ(storage[i], data[i]);
        });
        BOOST_TEST(r.success);
    }

    void
    testReadManyBuffersEof()
    {
        // Buffer sequence exceeds max_iovec_ but data runs out
        // mid-way through the second window.
        constexpr unsigned N = detail::max_iovec_ + 4;
        constexpr unsigned avail = detail::max_iovec_ + 2;

        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            std::string data;
            for(unsigned i = 0; i < avail; ++i)
                data.push_back(static_cast<char>('a' + (i % 26)));

            test::read_source rs(f);
            rs.provide(data);

            any_read_source ars(&rs);

            char storage[N] = {};
            std::array<mutable_buffer, N> buffers;
            for(unsigned i = 0; i < N; ++i)
                buffers[i] = mutable_buffer(&storage[i], 1);

            auto [ec, n] = co_await ars.read(buffers);
            if(ec && ec != cond::eof)
                co_return;

            BOOST_TEST(ec == cond::eof);
            BOOST_TEST_EQ(n, std::size_t(avail));
            for(unsigned i = 0; i < avail; ++i)
                BOOST_TEST_EQ(storage[i], data[i]);
        });
        BOOST_TEST(r.success);
    }

    void
    testDestroyWithActiveAwaitable()
    {
        // Split vtable: active_ops_ set in await_suspend.
        int destroyed = 0;
        pending_read_source ps{&destroyed};
        {
            any_read_source ars(&ps);
            char buf[1];
            auto aw = ars.read_some(mutable_buffer(buf, 1));
            BOOST_TEST(!aw.await_ready());

            test::inline_executor ex;
            aw.await_suspend(
                std::noop_coroutine(), io_env{executor_ref(ex), {}});
        }
        BOOST_TEST_EQ(destroyed, 1);
    }

    void
    testMoveAssignWithActiveAwaitable()
    {
        int destroyed = 0;
        pending_read_source ps{&destroyed};
        {
            any_read_source ars(&ps);
            char buf[1];
            auto aw = ars.read_some(mutable_buffer(buf, 1));
            BOOST_TEST(!aw.await_ready());

            test::inline_executor ex;
            aw.await_suspend(
                std::noop_coroutine(), io_env{executor_ref(ex), {}});

            any_read_source empty;
            ars = std::move(empty);
            BOOST_TEST_EQ(destroyed, 1);
        }
    }

    void
    run()
    {
        testConstruct();
        testConstructOwning();
        testMove();
        testMoveAssignNonEmpty();
        testSelfAssign();
        testReadSome();
        testReadSomePartial();
        testReadSomeMultiple();
        testReadSomeEof();
        testReadSomeBufferSequence();
        testReadSomeEmptyBuffer();
        testRead();
        testReadPartial();
        testReadMultiple();
        testReadInsufficientData();
        testReadEof();
        testReadEofAfterData();
        testReadBufferSequence();
        testReadSingleBuffer();
        testReadArray();
        testReadEmpty();
        testReadWithMaxReadSize();
        testReadWithMaxReadSizeMultiple();
        testReadManyBuffers();
        testReadManyBuffersEof();
        testDestroyWithActiveAwaitable();
        testMoveAssignWithActiveAwaitable();
    }
};

TEST_SUITE(any_read_source_test, "boost.capy.io.any_read_source");

} // namespace
} // namespace capy
} // namespace boost
