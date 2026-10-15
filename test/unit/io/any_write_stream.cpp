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
#include <boost/capy/ex/abandon.hpp>
#include <boost/capy/ex/run_async.hpp>
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
#include <memory>
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
        { return {std::error_code(), 0}; }
};

// Parks the awaiting coroutine for the test to resume or abandon,
// and counts how often the awaitable is destroyed.
struct parking_write_awaitable
{
    parked* slot_;
    int* down_;
    parking_write_awaitable(parked* s, int* d) : slot_(s), down_(d) {}
    parking_write_awaitable(parking_write_awaitable&& o) noexcept
        : slot_(o.slot_), down_(std::exchange(o.down_, nullptr)) {}
    ~parking_write_awaitable() { if(down_) ++(*down_); }
    bool await_ready() const noexcept { return false; }
    std::coroutine_handle<>
    await_suspend(std::coroutine_handle<> h, io_env const* env) noexcept
    {
        return park_awaitable{slot_}.await_suspend(h, env);
    }
    io_result<std::size_t> await_resume()
        { return {std::error_code(), 0}; }
};

struct parking_write_stream
{
    parked* slot_;
    int* down_;
    parking_write_awaitable write_some(
        ConstBufferSequence auto)
        { return parking_write_awaitable{slot_, down_}; }
};

struct pending_write_stream
{
    int* counter_;
    pending_write_awaitable write_some(
        ConstBufferSequence auto)
        { return pending_write_awaitable{counter_}; }
};

// Reports readiness without consulting the io_env, like a stream
// whose data is already in user-space memory.
struct ready_write_awaitable
{
    int* suspended_;
    bool await_ready() const noexcept { return true; }
    std::coroutine_handle<> await_suspend(std::coroutine_handle<>, io_env const*)
    {
        ++(*suspended_);
        return std::noop_coroutine();
    }
    io_result<std::size_t> await_resume()
        { return {std::error_code(), 7}; }
};

struct ready_write_stream
{
    int* suspended_;
    ready_write_awaitable write_some(
        ConstBufferSequence auto)
        { return ready_write_awaitable{suspended_}; }
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
    testReadyStreamSkipsSuspension()
    {
        // A concrete awaitable that reports readiness completes the
        // erased write without suspending the coroutine: the wrapper
        // forwards await_ready and never calls await_suspend.
        int suspended = 0;
        ready_write_stream rs{&suspended};
        any_write_stream aws(&rs);

        char const data[] = "x";
        auto aw = aws.write_some(const_buffer(data, 1));
        BOOST_TEST(aw.await_ready());
        auto [ec, n] = aw.await_resume();
        BOOST_TEST(!ec);
        BOOST_TEST_EQ(n, 7u);
        BOOST_TEST_EQ(suspended, 0);
    }

    void
    testDestroyWithActiveAwaitable()
    {
        // await_ready constructs the inner awaitable; a pending
        // stream then suspends through await_suspend.
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

    // A started write moved to another awaitable takes its claim with
    // it, so the wrapper's teardown leaves the moved-to awaitable owning
    // nothing.
    void
    testMoveStartedAwaitable()
    {
        int destroyed = 0;
        pending_write_stream ps{&destroyed};
        {
            auto s = std::make_unique<any_write_stream>(&ps);
            char const data[] = "x";
            auto aw = s->write_some(const_buffer(data, 1));
            BOOST_TEST(!aw.await_ready());
            auto moved = std::move(aw);
            s.reset();
            BOOST_TEST_EQ(destroyed, 1);
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

    // A chain unwound while parked in a type-erased write destroys the
    // write it waited on, as it would the stream's own awaitable.
    void
    testUnwindDestroysInnerWrite()
    {
        parked slot;
        int down = 0;
        parking_write_stream ps{&slot, &down};
        any_write_stream s(&ps);
        int dc = 0;
        test_executor ex(dc);
        run_async(ex)([](any_write_stream& s) -> task<void>
        {
            char c = 0;
            std::ignore = co_await s.write_some(make_buffer(&c, 1));
        }(s));
        BOOST_TEST(static_cast<bool>(slot));
        {
            // Another write, made but never awaited, owns nothing.
            char d = 0;
            auto unused = s.write_some(make_buffer(&d, 1));
        }
        BOOST_TEST_EQ(down, 0);
        capy::abandon(slot);
        BOOST_TEST_EQ(down, 1);
    }

    // A wrapper destroyed while a write is parked on it has already
    // destroyed that write; unwinding the chain afterwards must not
    // touch the wrapper.
    void
    testUnwindAfterWrapperDestroyed()
    {
        parked slot;
        int down = 0;
        parking_write_stream ps{&slot, &down};
        auto s = std::make_unique<any_write_stream>(&ps);
        int dc = 0;
        test_executor ex(dc);
        run_async(ex)([](any_write_stream& s) -> task<void>
        {
            char c = 0;
            std::ignore = co_await s.write_some(make_buffer(&c, 1));
        }(*s));
        BOOST_TEST(static_cast<bool>(slot));
        s.reset();
        BOOST_TEST_EQ(down, 1);
        capy::abandon(slot);
        BOOST_TEST_EQ(down, 1);
    }

    // A write unwound after its stream was assigned to leaves the
    // write the stream started since then alone.
    void
    testUnwindAfterStreamAssigned()
    {
        parked first;
        parked second;
        int down = 0;
        parking_write_stream ps1{&first, &down};
        parking_write_stream ps2{&second, &down};
        any_write_stream s(&ps1);
        int dc = 0;
        test_executor ex(dc);
        auto op = [](any_write_stream& s) -> task<void>
        {
            char c = 0;
            std::ignore = co_await s.write_some(make_buffer(&c, 1));
        };
        run_async(ex)(op(s));
        BOOST_TEST(static_cast<bool>(first));
        s = any_write_stream(&ps2);
        BOOST_TEST_EQ(down, 1);
        run_async(ex)(op(s));
        BOOST_TEST(static_cast<bool>(second));
        capy::abandon(first);
        BOOST_TEST_EQ(down, 1);
        capy::abandon(second);
        BOOST_TEST_EQ(down, 2);
    }

    void
    run()
    {
        testConstruct();
        testMove();
        testWriteSome();
        testUnwindDestroysInnerWrite();
        testUnwindAfterStreamAssigned();
        testUnwindAfterWrapperDestroyed();
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
        testReadyStreamSkipsSuspension();
        testDestroyWithActiveAwaitable();
        testMoveAssignWithActiveAwaitable();
        testMoveStartedAwaitable();
        testMoveAssignOwning();
        testConstructThrows();
    }
};

TEST_SUITE(any_write_stream_test, "boost.capy.io.any_write_stream");

} // namespace
} // namespace capy
} // namespace boost
