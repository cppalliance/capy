//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Test that header file is self-contained.
#include <boost/capy/io/any_write_stream.hpp>

#include <boost/capy/buffers/make_buffer.hpp>
#include <boost/capy/ex/executor_ref.hpp>
#include <boost/capy/ex/io_env.hpp>
#include <boost/capy/io_result.hpp>
#include <boost/capy/task.hpp>
#include <boost/capy/test/run_blocking.hpp>
#include <boost/capy/test/write_stream.hpp>

#include "test/unit/test_helpers.hpp"

#include <boost/capy/detail/config.hpp>

#include <array>
#include <coroutine>
#include <span>
#include <string_view>
#include <vector>

namespace boost {
namespace capy {

static_assert(WriteStream<any_write_stream>);

namespace {

struct pending_write_awaitable
{
    int* counter_;
    pending_write_awaitable(int* c) : counter_(c) {}
    pending_write_awaitable(pending_write_awaitable&& o) noexcept
        : counter_(std::exchange(o.counter_, nullptr)) {}
    ~pending_write_awaitable() { if(counter_) ++(*counter_); }
    bool await_ready() const noexcept { return false; }
    std::coroutine_handle<> await_suspend(std::coroutine_handle<>, io_env const*)
        { return std::noop_coroutine(); }
    io_result<std::size_t> await_resume()
        { return {{}, 0}; }
};

struct pending_write_stream
{
    int* counter_;
    pending_write_awaitable write_some(
        ConstBufferSequence auto)
        { return pending_write_awaitable{counter_}; }
};

// Move constructor throws so owning construction fails after storage
// is allocated but before the stream is constructed.
struct throwing_move_write_stream
{
    int* destroyed_;
    explicit throwing_move_write_stream(int* d) : destroyed_(d) {}
    throwing_move_write_stream(throwing_move_write_stream&& o) : destroyed_(o.destroyed_)
        { throw_test_exception_opaque("move ctor"); }
    ~throwing_move_write_stream() { if(destroyed_) ++(*destroyed_); }
    pending_write_awaitable write_some(
        ConstBufferSequence auto)
        { return pending_write_awaitable{nullptr}; }
};

class any_write_stream_test
{
public:
    void
    testConstruct()
    {
        // Default construct
        {
            any_write_stream aws;
            BOOST_TEST(!aws.has_value());
            BOOST_TEST(!aws);
        }

        // Construct from stream
        {
            test::fuse f;
            test::write_stream ws(f);
            any_write_stream aws(&ws);
            BOOST_TEST(aws.has_value());
            BOOST_TEST(static_cast<bool>(aws));
        }

        // Owning construct
        {
            test::fuse f;
            any_write_stream aws(test::write_stream{f});
            BOOST_TEST(aws.has_value());
            BOOST_TEST(static_cast<bool>(aws));
        }
    }

    void
    testMove()
    {
        test::fuse f;
        test::write_stream ws(f);

        any_write_stream aws1(&ws);
        BOOST_TEST(aws1.has_value());

        // Move construct
        any_write_stream aws2(std::move(aws1));
        BOOST_TEST(aws2.has_value());
        BOOST_TEST(!aws1.has_value());

        // Move assign
        any_write_stream aws3;
        aws3 = std::move(aws2);
        BOOST_TEST(aws3.has_value());
        BOOST_TEST(!aws2.has_value());

        // Move assign over live wrapper
        {
            test::fuse f2;
            test::write_stream ws2(f2);

            any_write_stream a(&ws);
            any_write_stream b(&ws2);
            BOOST_TEST(a.has_value());
            BOOST_TEST(b.has_value());

            a = std::move(b);
            BOOST_TEST(a.has_value());
            BOOST_TEST(!b.has_value());
        }
    }

    void
    testMoveAssignOwning()
    {
        // Move-assign over an owning wrapper to exercise the storage_
        // teardown branch in operator=.
        test::fuse f1;
        test::fuse f2;
        any_write_stream a(test::write_stream{f1});
        any_write_stream b(test::write_stream{f2});
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
            any_write_stream(throwing_move_write_stream{&destroyed}),
            test_exception);
        BOOST_TEST_EQ(destroyed, 1);
    }

    void
    testWriteSome()
    {
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            test::write_stream ws(f);

            any_write_stream aws(&ws);

            char const data[] = "hello world";
            const_buffer cb(data, 11);
            auto [ec, n] = co_await aws.write_some(std::span(&cb, 1));
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
            test::write_stream ws(f, 5); // max 5 bytes per write

            any_write_stream aws(&ws);

            char const data[] = "hello world";
            const_buffer cb(data, 11);
            auto [ec, n] = co_await aws.write_some(std::span(&cb, 1));
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
            test::write_stream ws(f);

            any_write_stream aws(&ws);

            char const data1[] = "hello";
            const_buffer cb1(data1, 5);
            auto [ec1, n1] = co_await aws.write_some(std::span(&cb1, 1));
            if(ec1)
                co_return;
            BOOST_TEST_EQ(n1, 5u);

            char const data2[] = " ";
            const_buffer cb2(data2, 1);
            auto [ec2, n2] = co_await aws.write_some(std::span(&cb2, 1));
            if(ec2)
                co_return;
            BOOST_TEST_EQ(n2, 1u);

            char const data3[] = "world";
            const_buffer cb3(data3, 5);
            auto [ec3, n3] = co_await aws.write_some(std::span(&cb3, 1));
            if(ec3)
                co_return;
            BOOST_TEST_EQ(n3, 5u);

            BOOST_TEST_EQ(ws.data(), "hello world");
        });
        BOOST_TEST(r.success);
    }

    void
    testWriteSomeBufferSequence()
    {
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            test::write_stream ws(f);

            any_write_stream aws(&ws);

            char const data1[] = "hello";
            char const data2[] = "world";
            std::array<const_buffer, 2> buffers = {{
                const_buffer(data1, 5),
                const_buffer(data2, 5)
            }};

            auto [ec, n] = co_await aws.write_some(
                std::span<const_buffer const>(buffers));
            if(ec)
                co_return;

            BOOST_TEST_EQ(n, 10u);
            BOOST_TEST_EQ(ws.data(), "helloworld");
        });
        BOOST_TEST(r.success);
    }

    void
    testWriteSomeSingleBuffer()
    {
        // Single buffer passed directly (not wrapped in span)
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            test::write_stream ws(f);

            any_write_stream aws(&ws);

            char const data[] = "hello world";
            const_buffer cb(data, 11);
            auto [ec, n] = co_await aws.write_some(cb);
            if(ec)
                co_return;

            BOOST_TEST_EQ(n, 11u);
            BOOST_TEST_EQ(ws.data(), "hello world");
        });
        BOOST_TEST(r.success);
    }

    void
    testWriteSomeArray()
    {
        // Array of buffers passed directly (not converted to span)
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            test::write_stream ws(f);

            any_write_stream aws(&ws);

            char const data1[] = "hello";
            char const data2[] = "world";
            std::array<const_buffer, 2> buffers = {{
                const_buffer(data1, 5),
                const_buffer(data2, 5)
            }};

            auto [ec, n] = co_await aws.write_some(buffers);
            if(ec)
                co_return;

            BOOST_TEST_EQ(n, 10u);
            BOOST_TEST_EQ(ws.data(), "helloworld");
        });
        BOOST_TEST(r.success);
    }

    void
    testWriteSomeEmptyBuffer()
    {
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            test::write_stream ws(f);

            any_write_stream aws(&ws);

            // Empty span of buffers
            auto [ec, n] = co_await aws.write_some(
                std::span<const_buffer const>{});
            BOOST_TEST(!ec);
            BOOST_TEST_EQ(n, 0u);
            BOOST_TEST_EQ(ws.data(), "");
        });
        BOOST_TEST(r.success);
    }

    void
    testWriteSomeZeroSizedBuffer()
    {
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            test::write_stream ws(f);

            any_write_stream aws(&ws);

            // Buffer with zero size
            const_buffer cb(nullptr, 0);
            auto [ec, n] = co_await aws.write_some(
                std::span(&cb, 1));
            BOOST_TEST(!ec);
            BOOST_TEST_EQ(n, 0u);
            BOOST_TEST_EQ(ws.data(), "");
        });
        BOOST_TEST(r.success);
    }

    // Trichotomy conformance: success implies !ec and n >= 1
    void
    testTrichotomySuccess()
    {
        test::fuse f;
        auto r = f.inert([&](test::fuse&) -> task<> {
            test::write_stream ws(f);

            any_write_stream aws(&ws);

            char const data[] = "hello";
            const_buffer cb(data, 5);
            auto [ec, n] = co_await aws.write_some(
                std::span(&cb, 1));
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
            test::write_stream ws(f);

            any_write_stream aws(&ws);

            char const data[] = "hello";
            const_buffer cb(data, 5);
            auto [ec, n] = co_await aws.write_some(
                std::span(&cb, 1));
            if(ec)
            {
                BOOST_TEST_EQ(n, 0u);
                co_return;
            }
            BOOST_TEST_GE(n, 1u);
        });
        BOOST_TEST(r.success);
    }

    void
    testWriteSomeManyBuffers()
    {
        // write_some is a partial operation — with more than
        // max_iovec_ buffers it processes only the first window.
        constexpr unsigned N = detail::max_iovec_ + 4;

        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            test::write_stream ws(f);
            any_write_stream aws(&ws);

            std::vector<std::string> strings;
            std::vector<const_buffer> buffers;
            for(unsigned i = 0; i < N; ++i)
            {
                strings.push_back(std::string(1,
                    static_cast<char>('a' + (i % 26))));
            }
            for(auto const& s : strings)
                buffers.emplace_back(s.data(), s.size());

            auto [ec, n] = co_await aws.write_some(buffers);
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
        // Flat vtable, construct-in-await_suspend variant:
        // await_suspend constructs the inner awaitable.
        int destroyed = 0;
        pending_write_stream ps{&destroyed};
        {
            any_write_stream aws(&ps);
            char const data[] = "x";
            auto aw = aws.write_some(const_buffer(data, 1));
            BOOST_TEST(!aw.await_ready());

            test::blocking_context bctx;
            auto ex = bctx.get_executor();
            io_env env{executor_ref(ex), {}};
            aw.await_suspend(std::noop_coroutine(), &env);
        }
        BOOST_TEST_EQ(destroyed, 1);
    }

    void
    testMoveAssignWithActiveAwaitable()
    {
        int destroyed = 0;
        pending_write_stream ps{&destroyed};
        {
            any_write_stream aws(&ps);
            char const data[] = "x";
            auto aw = aws.write_some(const_buffer(data, 1));
            BOOST_TEST(!aw.await_ready());

            test::blocking_context bctx;
            auto ex = bctx.get_executor();
            io_env env{executor_ref(ex), {}};
            aw.await_suspend(std::noop_coroutine(), &env);

            any_write_stream empty;
            aws = std::move(empty);
            BOOST_TEST_EQ(destroyed, 1);
        }
    }

    void
    run()
    {
        testConstruct();
        testMove();
        testWriteSome();
        testWriteSomePartial();
        testWriteSomeMultiple();
        testWriteSomeBufferSequence();
        testWriteSomeSingleBuffer();
        testWriteSomeArray();
        testWriteSomeEmptyBuffer();
        testWriteSomeZeroSizedBuffer();
        testWriteSomeManyBuffers();
        testTrichotomySuccess();
        testTrichotomyError();
        testDestroyWithActiveAwaitable();
        testMoveAssignWithActiveAwaitable();
        testMoveAssignOwning();
        testConstructThrows();
    }
};

TEST_SUITE(any_write_stream_test, "boost.capy.io.any_write_stream");

} // namespace
} // namespace capy
} // namespace boost
