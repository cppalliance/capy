//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Test that header file is self-contained.
#include <boost/capy/io/any_read_stream.hpp>

#include <boost/capy/buffers/make_buffer.hpp>
#include <boost/capy/cond.hpp>
#include <boost/capy/ex/io_env.hpp>
#include <boost/capy/io_result.hpp>
#include <boost/capy/task.hpp>
#include <boost/capy/test/read_stream.hpp>
#include <boost/capy/test/run_blocking.hpp>

#include "test/unit/test_helpers.hpp"

#include <boost/capy/detail/config.hpp>

#include <array>
#include <coroutine>
#include <span>
#include <string_view>
#include <vector>

namespace boost {
namespace capy {

static_assert(ReadStream<any_read_stream>);

namespace {

struct pending_read_awaitable
{
    int* counter_;
    pending_read_awaitable(int* c) : counter_(c) {}
    pending_read_awaitable(pending_read_awaitable&& o) noexcept
        : counter_(std::exchange(o.counter_, nullptr)) {}
    ~pending_read_awaitable() { if(counter_) ++(*counter_); }
    bool await_ready() const noexcept { return false; }
    std::coroutine_handle<> await_suspend(std::coroutine_handle<>, io_env const*)
        { return std::noop_coroutine(); }
    io_result<std::size_t> await_resume()
        { return {{}, 0}; }
};

struct pending_read_stream
{
    int* counter_;
    pending_read_awaitable read_some(
        MutableBufferSequence auto)
        { return pending_read_awaitable{counter_}; }
};

// Reports not-ready, then resumes the awaiting coroutine from
// await_suspend. This exercises the type-erased await_suspend
// thunk, which the always-ready test mocks never reach.
struct resuming_read_awaitable
{
    bool await_ready() const noexcept { return false; }
    std::coroutine_handle<>
    await_suspend(std::coroutine_handle<> h, io_env const*) noexcept
        { return h; }
    io_result<std::size_t> await_resume() { return {{}, 7}; }
};

struct resuming_read_stream
{
    resuming_read_awaitable read_some(
        MutableBufferSequence auto)
        { return {}; }
};

// Move constructor throws so owning construction fails after storage
// is allocated but before the stream is constructed. The destructor
// touches a member, so destroying a null instance would fault.
struct throwing_move_read_stream
{
    int* destroyed_;
    explicit throwing_move_read_stream(int* d) : destroyed_(d) {}
    throwing_move_read_stream(throwing_move_read_stream&& o) : destroyed_(o.destroyed_)
        { throw_test_exception_opaque("move ctor"); }
    ~throwing_move_read_stream() { if(destroyed_) ++(*destroyed_); }
    resuming_read_awaitable read_some(
        MutableBufferSequence auto)
        { return {}; }
};

class any_read_stream_test
{
public:
    void
    testConstruct()
    {
        // Default construct
        {
            any_read_stream ars;
            BOOST_TEST(!ars.has_value());
            BOOST_TEST(!ars);
        }

        // Construct from stream
        {
            test::fuse f;
            test::read_stream rs(f);
            any_read_stream ars(&rs);
            BOOST_TEST(ars.has_value());
            BOOST_TEST(static_cast<bool>(ars));
        }

        // Owning construct
        {
            test::fuse f;
            any_read_stream ars(test::read_stream{f});
            BOOST_TEST(ars.has_value());
            BOOST_TEST(static_cast<bool>(ars));
        }
    }

    void
    testMove()
    {
        test::fuse f;
        test::read_stream rs(f);

        any_read_stream ars1(&rs);
        BOOST_TEST(ars1.has_value());

        // Move construct
        any_read_stream ars2(std::move(ars1));
        BOOST_TEST(ars2.has_value());
        BOOST_TEST(!ars1.has_value());

        // Move assign
        any_read_stream ars3;
        ars3 = std::move(ars2);
        BOOST_TEST(ars3.has_value());
        BOOST_TEST(!ars2.has_value());

        // Move assign over live wrapper
        {
            test::fuse f2;
            test::read_stream rs2(f2);

            any_read_stream a(&rs);
            any_read_stream b(&rs2);
            BOOST_TEST(a.has_value());
            BOOST_TEST(b.has_value());

            a = std::move(b);
            BOOST_TEST(a.has_value());
            BOOST_TEST(!b.has_value());
        }
    }

    void
    testReadSome()
    {
        // Test with f.armed to exercise failure injection
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            test::read_stream rs(f);
            rs.provide("hello world");

            any_read_stream ars(&rs);

            char buf[32] = {};
            mutable_buffer mb(buf, sizeof(buf));
            auto [ec, n] = co_await ars.read_some(std::span(&mb, 1));
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
            test::read_stream rs(f);
            rs.provide("hello world");

            any_read_stream ars(&rs);

            char buf[5] = {};
            mutable_buffer mb(buf, sizeof(buf));
            auto [ec, n] = co_await ars.read_some(std::span(&mb, 1));
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
            test::read_stream rs(f);
            rs.provide("abcdefghij");

            any_read_stream ars(&rs);

            char buf[3] = {};
            mutable_buffer mb(buf, sizeof(buf));

            auto [ec1, n1] = co_await ars.read_some(std::span(&mb, 1));
            if(ec1)
                co_return;
            BOOST_TEST_EQ(n1, 3u);
            BOOST_TEST_EQ(std::string_view(buf, n1), "abc");

            auto [ec2, n2] = co_await ars.read_some(std::span(&mb, 1));
            if(ec2)
                co_return;
            BOOST_TEST_EQ(n2, 3u);
            BOOST_TEST_EQ(std::string_view(buf, n2), "def");

            auto [ec3, n3] = co_await ars.read_some(std::span(&mb, 1));
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
            test::read_stream rs(f);
            // No data provided - should get EOF

            any_read_stream ars(&rs);

            char buf[32] = {};
            mutable_buffer mb(buf, sizeof(buf));
            auto [ec, n] = co_await ars.read_some(std::span(&mb, 1));
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
            test::read_stream rs(f);
            rs.provide("helloworld");

            any_read_stream ars(&rs);

            char buf1[5] = {};
            char buf2[5] = {};
            std::array<mutable_buffer, 2> buffers = {{
                mutable_buffer(buf1, sizeof(buf1)),
                mutable_buffer(buf2, sizeof(buf2))
            }};

            auto [ec, n] = co_await ars.read_some(
                std::span<mutable_buffer const>(buffers));
            if(ec)
                co_return;

            BOOST_TEST_EQ(n, 10u);
            BOOST_TEST_EQ(std::string_view(buf1, 5), "hello");
            BOOST_TEST_EQ(std::string_view(buf2, 5), "world");
        });
        BOOST_TEST(r.success);
    }

    void
    testReadSomeSingleBuffer()
    {
        // Single buffer passed directly (not wrapped in span)
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            test::read_stream rs(f);
            rs.provide("hello world");

            any_read_stream ars(&rs);

            char buf[32] = {};
            mutable_buffer mb(buf, sizeof(buf));
            auto [ec, n] = co_await ars.read_some(mb);
            if(ec)
                co_return;

            BOOST_TEST_EQ(n, 11u);
            BOOST_TEST_EQ(std::string_view(buf, n), "hello world");
        });
        BOOST_TEST(r.success);
    }

    void
    testReadSomeArray()
    {
        // Array of buffers passed directly (not converted to span)
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            test::read_stream rs(f);
            rs.provide("helloworld");

            any_read_stream ars(&rs);

            char buf1[5] = {};
            char buf2[5] = {};
            std::array<mutable_buffer, 2> buffers = {{
                mutable_buffer(buf1, sizeof(buf1)),
                mutable_buffer(buf2, sizeof(buf2))
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
            test::read_stream rs(f);
            rs.provide("data");

            any_read_stream ars(&rs);

            auto [ec, n] = co_await ars.read_some(mutable_buffer());
            if(ec)
                co_return;

            BOOST_TEST_EQ(n, 0u);
            BOOST_TEST_EQ(rs.available(), 4u);
        });
        BOOST_TEST(r.success);
    }

    // Trichotomy conformance: success implies !ec and n >= 1
    void
    testTrichotomySuccess()
    {
        test::fuse f;
        auto r = f.inert([&](test::fuse&) -> task<> {
            test::read_stream rs(f);
            rs.provide("hello");

            any_read_stream ars(&rs);

            char buf[32] = {};
            auto [ec, n] = co_await ars.read_some(
                mutable_buffer(buf, sizeof(buf)));
            BOOST_TEST(!ec);
            BOOST_TEST_GE(n, 1u);
            BOOST_TEST_EQ(n, 5u);
        });
        BOOST_TEST(r.success);
    }

    // Trichotomy conformance: error implies n == 0
    void
    testTrichotomyError()
    {
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            test::read_stream rs(f);
            rs.provide("hello");

            any_read_stream ars(&rs);

            char buf[32] = {};
            auto [ec, n] = co_await ars.read_some(
                mutable_buffer(buf, sizeof(buf)));
            if(ec)
            {
                BOOST_TEST_EQ(n, 0u);
                co_return;
            }
            BOOST_TEST_GE(n, 1u);
        });
        BOOST_TEST(r.success);
    }

    // Trichotomy conformance: EOF after draining data
    // returns {eof, 0}, not {eof, n}
    void
    testTrichotomyEofAfterDrain()
    {
        test::fuse f;
        auto r = f.inert([&](test::fuse&) -> task<> {
            test::read_stream rs(f);
            rs.provide("hi");

            any_read_stream ars(&rs);

            // Drain all data
            char buf[32] = {};
            auto [ec1, n1] = co_await ars.read_some(
                mutable_buffer(buf, sizeof(buf)));
            BOOST_TEST(!ec1);
            BOOST_TEST_EQ(n1, 2u);

            // Next read discovers EOF
            auto [ec2, n2] = co_await ars.read_some(
                mutable_buffer(buf, sizeof(buf)));
            BOOST_TEST(ec2 == cond::eof);
            BOOST_TEST_EQ(n2, 0u);
        });
        BOOST_TEST(r.success);
    }

    // Trichotomy conformance: empty buffer on exhausted
    // stream returns {success, 0}, not {eof, 0}
    void
    testTrichotomyEmptyBufferExhausted()
    {
        test::fuse f;
        auto r = f.inert([&](test::fuse&) -> task<> {
            test::read_stream rs(f);
            rs.provide("hi");

            any_read_stream ars(&rs);

            // Drain all data
            char buf[32] = {};
            auto [ec1, n1] = co_await ars.read_some(
                mutable_buffer(buf, sizeof(buf)));
            BOOST_TEST(!ec1);
            BOOST_TEST_EQ(n1, 2u);

            // Empty buffer on exhausted stream is a no-op
            auto [ec2, n2] = co_await ars.read_some(
                mutable_buffer());
            BOOST_TEST(!ec2);
            BOOST_TEST_EQ(n2, 0u);
        });
        BOOST_TEST(r.success);
    }

    void
    testReadSomeManyBuffers()
    {
        // read_some is a partial operation — with more than
        // max_iovec_ buffers it processes only the first window.
        constexpr unsigned N = detail::max_iovec_ + 4;

        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            std::string data;
            for(unsigned i = 0; i < N; ++i)
                data.push_back(static_cast<char>('a' + (i % 26)));

            test::read_stream rs(f);
            rs.provide(data);

            any_read_stream ars(&rs);

            char storage[N] = {};
            std::vector<mutable_buffer> buffers;
            for(unsigned i = 0; i < N; ++i)
                buffers.emplace_back(&storage[i], 1);

            auto [ec, n] = co_await ars.read_some(buffers);
            if(ec)
                co_return;

            // Partial — at most max_iovec_ bytes
            BOOST_TEST(!ec);
            BOOST_TEST(n >= 1u);
            BOOST_TEST(n <= std::size_t(detail::max_iovec_));
        });
        BOOST_TEST(r.success);
    }

    void
    testDestroyWithActiveAwaitable()
    {
        // Verify destructor cleans up an in-flight awaitable.
        // Flat vtable: await_ready constructs the inner awaitable
        // and sets awaitable_active_ = true.
        int destroyed = 0;
        pending_read_stream ps{&destroyed};
        {
            any_read_stream ars(&ps);
            char buf[1];
            auto aw = ars.read_some(mutable_buffer(buf, 1));
            BOOST_TEST(!aw.await_ready());
        }
        BOOST_TEST_EQ(destroyed, 1);
    }

    void
    testMoveAssignWithActiveAwaitable()
    {
        int destroyed = 0;
        pending_read_stream ps{&destroyed};
        {
            any_read_stream ars(&ps);
            char buf[1];
            auto aw = ars.read_some(mutable_buffer(buf, 1));
            BOOST_TEST(!aw.await_ready());

            any_read_stream empty;
            ars = std::move(empty);
            BOOST_TEST_EQ(destroyed, 1);
        }
    }

    void
    testMoveAssignOwning()
    {
        // Move-assign over an owning wrapper to exercise the
        // storage_ teardown branch in operator=.
        test::fuse f1;
        test::fuse f2;
        any_read_stream a(test::read_stream{f1});
        any_read_stream b(test::read_stream{f2});
        BOOST_TEST(a.has_value());

        a = std::move(b);
        BOOST_TEST(a.has_value());
        BOOST_TEST(!b.has_value());
    }

    void
    testConstructThrows()
    {
        // Owning construction whose stream move-ctor throws must not
        // run the stream destructor on a null pointer.
        int destroyed = 0;
        BOOST_TEST_THROWS(
            any_read_stream(throwing_move_read_stream{&destroyed}),
            test_exception);
        BOOST_TEST_EQ(destroyed, 1);
    }

    void
    testReadSomeSuspends()
    {
        // Drive a read whose awaitable suspends and then resumes,
        // covering the type-erased await_suspend forwarding.
        resuming_read_stream rs;
        any_read_stream ars(&rs);

        auto coro = [&]() -> task<std::size_t> {
            char buf[1];
            auto [ec, n] = co_await ars.read_some(mutable_buffer(buf, 1));
            if(ec)
                co_return 0;
            co_return n;
        };

        std::size_t result{};
        test::run_blocking([&](std::size_t v) { result = v; })(coro());
        BOOST_TEST_EQ(result, 7u);
    }

    void
    run()
    {
        testConstruct();
        testMove();
        testReadSome();
        testReadSomePartial();
        testReadSomeMultiple();
        testReadSomeEof();
        testReadSomeBufferSequence();
        testReadSomeSingleBuffer();
        testReadSomeArray();
        testReadSomeEmptyBuffer();
        testTrichotomySuccess();
        testTrichotomyError();
        testTrichotomyEofAfterDrain();
        testTrichotomyEmptyBufferExhausted();
        testReadSomeManyBuffers();
        testDestroyWithActiveAwaitable();
        testMoveAssignWithActiveAwaitable();
        testMoveAssignOwning();
        testConstructThrows();
        testReadSomeSuspends();
    }
};

TEST_SUITE(any_read_stream_test, "boost.capy.io.any_read_stream");

} // namespace
} // namespace capy
} // namespace boost
