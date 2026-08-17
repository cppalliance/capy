//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Test that header file is self-contained.
#include <boost/capy/io/write_now.hpp>

#include <boost/capy/buffers/make_buffer.hpp>
#include <boost/capy/error.hpp>
#include <boost/capy/ex/io_env.hpp>
#include <boost/capy/test/fuse.hpp>
#include <boost/capy/test/run_blocking.hpp>
#include <boost/capy/test/write_stream.hpp>

#include "test/unit/test_helpers.hpp"

#include <array>
#include <coroutine>
#include <string>
#include <string_view>

namespace boost {
namespace capy {
namespace {

// A write stream whose write awaitable suspends, then resumes from
// await_suspend. This drives write_now's await_transform wrapper
// forwarding that the always-ready test stream never reaches.
struct suspending_write_awaitable
{
    bool await_ready() const noexcept { return false; }
    std::coroutine_handle<>
    await_suspend(std::coroutine_handle<> h, io_env const*) noexcept
        { return h; }
    io_result<std::size_t> await_resume() { return {std::error_code(), 5}; }
};

struct suspending_write_stream
{
    suspending_write_awaitable write_some(ConstBufferSequence auto)
        { return {}; }
};

class write_now_test
{
public:
    void
    testSingleBuffer()
    {
        // Complete write
        {
            test::fuse f;
            auto r = f.armed([&](test::fuse&) -> task<> {
                test::write_stream ws(f);
                write_now wn(ws);

                std::string_view sv("hello world");
                auto [ec, n] = co_await wn(make_buffer(sv));
                if(ec)
                    co_return;

                BOOST_TEST_EQ(n, 11u);
                BOOST_TEST_EQ(ws.data(), "hello world");
            });
            BOOST_TEST(r.success);
        }

        // Exact size
        {
            test::fuse f;
            auto r = f.armed([&](test::fuse&) -> task<> {
                test::write_stream ws(f);
                write_now wn(ws);

                std::string_view sv("exact");
                auto [ec, n] = co_await wn(make_buffer(sv));
                if(ec)
                    co_return;

                BOOST_TEST_EQ(n, 5u);
                BOOST_TEST_EQ(ws.data(), "exact");
            });
            BOOST_TEST(r.success);
        }
    }

    void
    testEmptyBuffer()
    {
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            test::write_stream ws(f);
            write_now wn(ws);

            auto [ec, n] = co_await wn(const_buffer());
            if(ec)
                co_return;

            BOOST_TEST_EQ(n, 0u);
            BOOST_TEST(ws.data().empty());
        });
        BOOST_TEST(r.success);
    }

    void
    testBufferArray()
    {
        // Two buffers
        {
            test::fuse f;
            auto r = f.armed([&](test::fuse&) -> task<> {
                test::write_stream ws(f);
                write_now wn(ws);

                std::string s1("hello");
                std::string s2("world");
                std::array<const_buffer, 2> bufs{{
                    const_buffer(s1.data(), s1.size()),
                    const_buffer(s2.data(), s2.size())
                }};

                auto [ec, n] = co_await wn(bufs);
                if(ec)
                    co_return;

                BOOST_TEST_EQ(n, 10u);
                BOOST_TEST_EQ(ws.data(), "helloworld");
            });
            BOOST_TEST(r.success);
        }

        // First buffer empty
        {
            test::fuse f;
            auto r = f.armed([&](test::fuse&) -> task<> {
                test::write_stream ws(f);
                write_now wn(ws);

                std::string s2("world");
                std::array<const_buffer, 2> bufs{{
                    const_buffer(),
                    const_buffer(s2.data(), s2.size())
                }};

                auto [ec, n] = co_await wn(bufs);
                if(ec)
                    co_return;

                BOOST_TEST_EQ(n, 5u);
                BOOST_TEST_EQ(ws.data(), "world");
            });
            BOOST_TEST(r.success);
        }
    }

    void
    testBufferPair()
    {
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            test::write_stream ws(f);
            write_now wn(ws);

            std::string s1("ab");
            std::string s2("cdefgh");
            std::array<const_buffer, 2> bp{{
                const_buffer(s1.data(), s1.size()),
                const_buffer(s2.data(), s2.size())
            }};

            auto [ec, n] = co_await wn(bp);
            if(ec)
                co_return;

            BOOST_TEST_EQ(n, 8u);
            BOOST_TEST_EQ(ws.data(), "abcdefgh");
        });
        BOOST_TEST(r.success);
    }

    void
    testLargeData()
    {
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            test::write_stream ws(f);
            write_now wn(ws);

            std::string large(10000, 'x');
            auto [ec, n] = co_await wn(make_buffer(large));
            if(ec)
                co_return;

            BOOST_TEST_EQ(n, 10000u);
            BOOST_TEST_EQ(ws.data().size(), 10000u);
            BOOST_TEST(ws.data() == large);
        });
        BOOST_TEST(r.success);
    }

    void
    testChunkedWrite()
    {
        // write_stream with max_write_size forces partial writes,
        // which means multiple loop iterations in the fast path.
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            test::write_stream ws(f, 3);
            write_now wn(ws);

            std::string_view sv("hello world");
            auto [ec, n] = co_await wn(make_buffer(sv));
            if(ec)
                co_return;

            BOOST_TEST_EQ(n, 11u);
            BOOST_TEST_EQ(ws.data(), "hello world");
        });
        BOOST_TEST(r.success);
    }

    void
    testFuseError()
    {
        int error_count = 0;
        int success_count = 0;

        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            test::write_stream ws(f);
            write_now wn(ws);

            std::string_view sv("test data");
            auto [ec, n] = co_await wn(make_buffer(sv));
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
    testFrameReuse()
    {
        // Multiple calls on the same write_now to exercise
        // the frame cache.
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            test::write_stream ws(f);
            write_now wn(ws);

            std::string_view sv1("first");
            auto [ec1, n1] = co_await wn(make_buffer(sv1));
            if(ec1)
                co_return;

            std::string_view sv2("second");
            auto [ec2, n2] = co_await wn(make_buffer(sv2));
            if(ec2)
                co_return;

            BOOST_TEST_EQ(n1, 5u);
            BOOST_TEST_EQ(n2, 6u);
            BOOST_TEST_EQ(ws.data(), "firstsecond");
        });
        BOOST_TEST(r.success);
    }

    void
    testSuspendingWrite()
    {
        // Drive a write whose inner awaitable suspends, exercising the
        // await_transform wrapper's await_suspend forwarding.
        suspending_write_stream ws;
        write_now wn(ws);

        auto coro = [&]() -> task<std::size_t> {
            char const data[] = "hello";
            auto [ec, n] = co_await wn(const_buffer(data, 5));
            if(ec)
                co_return 0;
            co_return n;
        };

        std::size_t result{};
        test::run_blocking([&](std::size_t v) { result = v; })(coro());
        BOOST_TEST_EQ(result, 5u);
    }

    void
    testFrameRealloc()
    {
        // The internal coroutine stores its buffer sequence in the frame
        // by value, so an operation with a much larger sequence than the
        // cached one forces the frame cache to reallocate, freeing the
        // smaller frame. Each op is scoped so its coroutine is destroyed
        // before the next allocation reclaims the cached frame.
        suspending_write_stream ws;
        write_now wn(ws);

        std::array<const_buffer, 1> small_bufs{};
        std::array<const_buffer, 256> large_bufs{};
        { auto op = wn(small_bufs); (void)op; }  // caches the smaller frame
        { auto op = wn(large_bufs); (void)op; }  // larger frame: realloc + free
        BOOST_TEST(true);
    }

    void
    run()
    {
        testFrameRealloc();
        testSuspendingWrite();
        testSingleBuffer();
        testEmptyBuffer();
        testBufferArray();
        testBufferPair();
        testLargeData();
        testChunkedWrite();
        testFuseError();
        testFrameReuse();
    }
};

TEST_SUITE(write_now_test, "boost.capy.io.write_now");

} // namespace
} // capy
} // boost
