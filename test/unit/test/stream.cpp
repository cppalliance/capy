//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Test that header file is self-contained.
#include <boost/capy/test/stream.hpp>

#include <boost/capy/buffers/make_buffer.hpp>
#include <boost/capy/concept/read_stream.hpp>
#include <boost/capy/concept/stream.hpp>
#include <boost/capy/concept/write_stream.hpp>
#include <boost/capy/cond.hpp>
#include <boost/capy/error.hpp>
#include <boost/capy/ex/executor_ref.hpp>
#include <boost/capy/ex/io_env.hpp>
#include <boost/capy/ex/run_async.hpp>
#include <boost/capy/ex/thread_pool.hpp>
#include <boost/capy/io_task.hpp>
#include <boost/capy/task.hpp>
#include <boost/capy/test/run_blocking.hpp>
#include <boost/capy/when_all.hpp>

#include "test/unit/test_helpers.hpp"

#include <array>
#include <latch>
#include <queue>
#include <stop_token>
#include <string_view>
#include <thread>

namespace boost {
namespace capy {
namespace test {

class stream_test
{
public:

    //--------------------------------------------
    //
    // Read operations
    //
    //--------------------------------------------

    void
    testReadSome()
    {
        fuse f;
        auto r = f.armed([&](fuse&) -> task<> {
            auto [a, b] = make_stream_pair(f);
            b.provide("hello world");

            char buf[32] = {};
            auto [ec, n] = co_await a.read_some(
                make_buffer(buf));
            if(ec)
                co_return;
            BOOST_TEST_EQ(n, 11u);
            BOOST_TEST_EQ(
                std::string_view(buf, n), "hello world");
        });
        BOOST_TEST(r.success);
    }

    void
    testReadSomePartial()
    {
        fuse f;
        auto r = f.armed([&](fuse&) -> task<> {
            auto [a, b] = make_stream_pair(f);
            b.provide("hello world");

            char buf[5] = {};
            auto [ec, n] = co_await a.read_some(
                make_buffer(buf));
            if(ec)
                co_return;
            BOOST_TEST_EQ(n, 5u);
            BOOST_TEST_EQ(
                std::string_view(buf, n), "hello");
        });
        BOOST_TEST(r.success);
    }

    void
    testReadSomeMultiple()
    {
        fuse f;
        auto r = f.armed([&](fuse&) -> task<> {
            auto [a, b] = make_stream_pair(f);
            b.provide("abcdefghij");

            char buf[3] = {};

            auto [ec1, n1] = co_await a.read_some(
                make_buffer(buf));
            if(ec1)
                co_return;
            BOOST_TEST_EQ(n1, 3u);
            BOOST_TEST_EQ(
                std::string_view(buf, n1), "abc");

            auto [ec2, n2] = co_await a.read_some(
                make_buffer(buf));
            if(ec2)
                co_return;
            BOOST_TEST_EQ(n2, 3u);
            BOOST_TEST_EQ(
                std::string_view(buf, n2), "def");

            auto [ec3, n3] = co_await a.read_some(
                make_buffer(buf));
            if(ec3)
                co_return;
            BOOST_TEST_EQ(n3, 3u);
            BOOST_TEST_EQ(
                std::string_view(buf, n3), "ghi");

            auto [ec4, n4] = co_await a.read_some(
                make_buffer(buf));
            if(ec4)
                co_return;
            BOOST_TEST_EQ(n4, 1u);
            BOOST_TEST_EQ(
                std::string_view(buf, n4), "j");
        });
        BOOST_TEST(r.success);
    }

    void
    testReadSomeEof()
    {
        run_blocking()([]() -> task<> {
            auto [a, b] = make_stream_pair();
            b.close();

            char buf[32] = {};
            auto [ec, n] = co_await a.read_some(
                make_buffer(buf));
            BOOST_TEST(ec == cond::eof);
            BOOST_TEST_EQ(n, 0u);
        }());
    }

    void
    testReadSomeBufferSequence()
    {
        fuse f;
        auto r = f.armed([&](fuse&) -> task<> {
            auto [a, b] = make_stream_pair(f);
            b.provide("helloworld");

            char buf1[5] = {};
            char buf2[5] = {};
            std::array<mutable_buffer, 2> buffers = {{
                make_buffer(buf1),
                make_buffer(buf2)
            }};

            auto [ec, n] = co_await a.read_some(buffers);
            if(ec)
                co_return;
            BOOST_TEST_EQ(n, 10u);
            BOOST_TEST_EQ(
                std::string_view(buf1, 5), "hello");
            BOOST_TEST_EQ(
                std::string_view(buf2, 5), "world");
        });
        BOOST_TEST(r.success);
    }

    void
    testReadSomeEmpty()
    {
        fuse f;
        auto r = f.armed([&](fuse&) -> task<> {
            auto [a, b] = make_stream_pair(f);
            b.provide("data");

            auto [ec, n] = co_await a.read_some(
                mutable_buffer());
            if(ec)
                co_return;
            BOOST_TEST_EQ(n, 0u);

            // Data is preserved
            char buf[32] = {};
            auto [ec2, n2] = co_await a.read_some(
                make_buffer(buf));
            if(ec2)
                co_return;
            BOOST_TEST_EQ(n2, 4u);
            BOOST_TEST_EQ(
                std::string_view(buf, n2), "data");
        });
        BOOST_TEST(r.success);
    }

    void
    testReadSomeEmptyNoData()
    {
        // Empty buffers must complete immediately even
        // when no data is available (should not suspend).
        fuse f;
        auto r = f.armed([&](fuse&) -> task<> {
            auto [a, b] = make_stream_pair(f);

            auto [ec, n] = co_await a.read_some(
                mutable_buffer());
            if(ec)
                co_return;
            BOOST_TEST_EQ(n, 0u);
        });
        BOOST_TEST(r.success);
    }

    void
    testMaxReadSize()
    {
        fuse f;
        auto r = f.armed([&](fuse&) -> task<> {
            auto [a, b] = make_stream_pair(f);
            a.set_max_read_size(3);
            b.provide("hello world");

            char buf[32] = {};
            auto [ec, n] = co_await a.read_some(
                make_buffer(buf));
            if(ec)
                co_return;
            BOOST_TEST_EQ(n, 3u);
            BOOST_TEST_EQ(
                std::string_view(buf, n), "hel");
        });
        BOOST_TEST(r.success);
    }

    void
    testMaxReadSizeMultiple()
    {
        fuse f;
        auto r = f.armed([&](fuse&) -> task<> {
            auto [a, b] = make_stream_pair(f);
            a.set_max_read_size(4);
            b.provide("abcdefghij");

            char buf[32] = {};

            auto [ec1, n1] = co_await a.read_some(
                make_buffer(buf));
            if(ec1)
                co_return;
            BOOST_TEST_EQ(n1, 4u);
            BOOST_TEST_EQ(
                std::string_view(buf, n1), "abcd");

            auto [ec2, n2] = co_await a.read_some(
                make_buffer(buf));
            if(ec2)
                co_return;
            BOOST_TEST_EQ(n2, 4u);
            BOOST_TEST_EQ(
                std::string_view(buf, n2), "efgh");

            auto [ec3, n3] = co_await a.read_some(
                make_buffer(buf));
            if(ec3)
                co_return;
            BOOST_TEST_EQ(n3, 2u);
            BOOST_TEST_EQ(
                std::string_view(buf, n3), "ij");
        });
        BOOST_TEST(r.success);
    }

    //--------------------------------------------
    //
    // Write operations
    //
    //--------------------------------------------

    void
    testWriteSome()
    {
        fuse f;
        auto r = f.armed([&](fuse&) -> task<> {
            auto [a, b] = make_stream_pair(f);

            auto [ec, n] = co_await a.write_some(
                make_buffer("hello world", 11));
            if(ec)
                co_return;
            BOOST_TEST_EQ(n, 11u);

            auto [ec2, ok] = b.expect("hello world");
            if(ec2)
                co_return;
            BOOST_TEST(ok);
        });
        BOOST_TEST(r.success);
    }

    void
    testWriteSomeMultiple()
    {
        fuse f;
        auto r = f.armed([&](fuse&) -> task<> {
            auto [a, b] = make_stream_pair(f);

            auto [ec1, n1] = co_await a.write_some(
                make_buffer("hello", 5));
            if(ec1)
                co_return;
            BOOST_TEST_EQ(n1, 5u);

            auto [ec2, n2] = co_await a.write_some(
                make_buffer(" ", 1));
            if(ec2)
                co_return;
            BOOST_TEST_EQ(n2, 1u);

            auto [ec3, n3] = co_await a.write_some(
                make_buffer("world", 5));
            if(ec3)
                co_return;
            BOOST_TEST_EQ(n3, 5u);

            auto [ec4, ok] = b.expect("hello world");
            if(ec4)
                co_return;
            BOOST_TEST(ok);
        });
        BOOST_TEST(r.success);
    }

    void
    testWriteSomeBufferSequence()
    {
        fuse f;
        auto r = f.armed([&](fuse&) -> task<> {
            auto [a, b] = make_stream_pair(f);

            std::array<const_buffer, 2> buffers = {{
                make_buffer("hello", 5),
                make_buffer("world", 5)
            }};

            auto [ec, n] = co_await a.write_some(buffers);
            if(ec)
                co_return;
            BOOST_TEST_EQ(n, 10u);

            auto [ec2, ok] = b.expect("helloworld");
            if(ec2)
                co_return;
            BOOST_TEST(ok);
        });
        BOOST_TEST(r.success);
    }

    void
    testWriteSomeEmpty()
    {
        fuse f;
        auto r = f.armed([&](fuse&) -> task<> {
            auto [a, b] = make_stream_pair(f);

            auto [ec, n] = co_await a.write_some(
                const_buffer());
            BOOST_TEST(! ec);
            BOOST_TEST_EQ(n, 0u);
        });
        BOOST_TEST(r.success);
    }

    void
    testExpect()
    {
        fuse f;
        auto r = f.armed([&](fuse&) -> task<> {
            auto [a, b] = make_stream_pair(f);

            auto [ec, n] = co_await a.write_some(
                make_buffer("hello", 5));
            if(ec)
                co_return;
            BOOST_TEST_EQ(n, 5u);

            auto [ec2, ok] = b.expect("hello");
            if(ec2)
                co_return;
            BOOST_TEST(ok);
        });
        BOOST_TEST(r.success);
    }

    void
    testExpectMismatch()
    {
        fuse f;
        auto r = f.armed([&](fuse&) -> task<> {
            auto [a, b] = make_stream_pair(f);

            auto [ec, n] = co_await a.write_some(
                make_buffer("world", 5));
            if(ec)
                co_return;

            auto [ec2, ok] = b.expect("hello");
            if(ec2)
                co_return;
            BOOST_TEST(! ok);
        });
        BOOST_TEST(r.success);
    }

    void
    testExpectPartialMatch()
    {
        fuse f;
        auto r = f.armed([&](fuse&) -> task<> {
            auto [a, b] = make_stream_pair(f);

            auto [ec1, n1] = co_await a.write_some(
                make_buffer("hello", 5));
            if(ec1)
                co_return;
            BOOST_TEST_EQ(n1, 5u);

            auto [ec2, n2] = co_await a.write_some(
                make_buffer("world", 5));
            if(ec2)
                co_return;
            BOOST_TEST_EQ(n2, 5u);

            auto [ec3, ok] = b.expect("helloworld");
            if(ec3)
                co_return;
            BOOST_TEST(ok);
        });
        BOOST_TEST(r.success);
    }

    void
    testExpectExcessData()
    {
        fuse f;
        auto r = f.armed([&](fuse&) -> task<> {
            auto [a, b] = make_stream_pair(f);

            auto [ec1, n1] = co_await a.write_some(
                make_buffer("hi there", 8));
            if(ec1)
                co_return;
            BOOST_TEST_EQ(n1, 8u);

            auto [ec2, ok] = b.expect("hi");
            if(ec2)
                co_return;
            BOOST_TEST(ok);

            // Remaining data still readable
            char buf[32] = {};
            auto [ec3, n3] = co_await b.read_some(
                make_buffer(buf));
            if(ec3)
                co_return;
            BOOST_TEST_EQ(n3, 6u);
            BOOST_TEST_EQ(
                std::string_view(buf, n3), " there");
        });
        BOOST_TEST(r.success);
    }

    void
    testMaxWriteSize()
    {
        fuse f;
        auto r = f.armed([&](fuse&) -> task<> {
            auto [a, b] = make_stream_pair(f);
            b.set_max_read_size(3);

            auto [ec1, n1] = co_await a.write_some(
                make_buffer("hello world", 11));
            if(ec1)
                co_return;
            BOOST_TEST_EQ(n1, 11u);

            char buf[32] = {};
            auto [ec2, n2] = co_await b.read_some(
                make_buffer(buf));
            if(ec2)
                co_return;
            BOOST_TEST_EQ(n2, 3u);
            BOOST_TEST_EQ(
                std::string_view(buf, n2), "hel");
        });
        BOOST_TEST(r.success);
    }

    void
    testMaxWriteSizeMultiple()
    {
        fuse f;
        auto r = f.armed([&](fuse&) -> task<> {
            auto [a, b] = make_stream_pair(f);
            b.set_max_read_size(4);

            auto [ec1, n1] = co_await a.write_some(
                make_buffer("abcdefghij", 10));
            if(ec1)
                co_return;
            BOOST_TEST_EQ(n1, 10u);

            char buf[32] = {};

            auto [ec2, n2] = co_await b.read_some(
                make_buffer(buf));
            if(ec2)
                co_return;
            BOOST_TEST_EQ(n2, 4u);
            BOOST_TEST_EQ(
                std::string_view(buf, n2), "abcd");

            auto [ec3, n3] = co_await b.read_some(
                make_buffer(buf));
            if(ec3)
                co_return;
            BOOST_TEST_EQ(n3, 4u);
            BOOST_TEST_EQ(
                std::string_view(buf, n3), "efgh");

            auto [ec4, n4] = co_await b.read_some(
                make_buffer(buf));
            if(ec4)
                co_return;
            BOOST_TEST_EQ(n4, 2u);
            BOOST_TEST_EQ(
                std::string_view(buf, n4), "ij");
        });
        BOOST_TEST(r.success);
    }

    //--------------------------------------------
    //
    // Combined operations
    //
    //--------------------------------------------

    void
    testReadWrite()
    {
        fuse f;
        auto r = f.armed([&](fuse&) -> task<> {
            auto [a, b] = make_stream_pair(f);
            b.provide("request");

            char buf[32] = {};
            auto [ec1, n1] = co_await a.read_some(
                make_buffer(buf));
            if(ec1)
                co_return;
            BOOST_TEST_EQ(n1, 7u);
            BOOST_TEST_EQ(
                std::string_view(buf, n1), "request");

            auto [ec2, n2] = co_await a.write_some(
                make_buffer("response", 8));
            if(ec2)
                co_return;
            BOOST_TEST_EQ(n2, 8u);

            auto [ec3, ok] = b.expect("response");
            if(ec3)
                co_return;
            BOOST_TEST(ok);
        });
        BOOST_TEST(r.success);
    }

    void
    testLoopback()
    {
        fuse f;
        auto r = f.armed([&](fuse&) -> task<> {
            auto [a, b] = make_stream_pair(f);
            a.set_max_read_size(4);
            b.provide("hello world!");

            char buf[4];

            for(int i = 0; i < 3; ++i)
            {
                auto [ec, n] = co_await a.read_some(
                    make_buffer(buf));
                if(ec)
                    co_return;
                auto [ec2, n2] = co_await a.write_some(
                    make_buffer(buf, n));
                if(ec2)
                    co_return;
            }

            auto [ec, ok] = b.expect("hello world!");
            if(ec)
                co_return;
            BOOST_TEST(ok);
        });
        BOOST_TEST(r.success);
    }

    void
    testFuseReadErrorInjection()
    {
        int read_success_count = 0;
        int read_error_count = 0;

        fuse f;
        auto r = f.armed([&](fuse&) -> task<> {
            auto [a, b] = make_stream_pair(f);
            b.provide("test data");

            char buf[32] = {};
            auto [ec, n] = co_await a.read_some(
                make_buffer(buf));
            if(ec)
            {
                ++read_error_count;
                co_return;
            }
            ++read_success_count;
        });

        BOOST_TEST(r.success);
        BOOST_TEST(read_error_count > 0);
        BOOST_TEST(read_success_count > 0);
    }

    void
    testFuseWriteErrorInjection()
    {
        int write_success_count = 0;
        int write_error_count = 0;

        fuse f;
        auto r = f.armed([&](fuse&) -> task<> {
            auto [a, b] = make_stream_pair(f);

            auto [ec, n] = co_await a.write_some(
                make_buffer("test data", 9));
            if(ec)
            {
                ++write_error_count;
                co_return;
            }
            ++write_success_count;
        });

        BOOST_TEST(r.success);
        BOOST_TEST(write_error_count > 0);
        BOOST_TEST(write_success_count > 0);
    }

    //--------------------------------------------
    //
    // Cancellation
    //
    //--------------------------------------------

    void
    testReadSomeCancellationWhileSuspended()
    {
        // A read that suspends waiting for the peer is resumed with
        // error::canceled when the environment's stop token fires.
        // Driven manually on a queuing executor for deterministic,
        // non-blocking control (mirrors async_mutex's cancellation test).
        auto [a, b] = make_stream_pair();
        std::queue<std::coroutine_handle<>> q;
        queuing_executor ex(q);
        std::stop_source ss;

        std::error_code reader_ec;
        bool reader_done = false;

        auto reader = [](stream& s,
            std::error_code& out_ec, bool& done) -> task<>
        {
            char buf[32] = {};
            auto [ec, n] = co_await s.read_some(make_buffer(buf));
            out_ec = ec;
            (void)n;
            done = true;
        }(b, reader_ec, reader_done);

        auto h = reader.handle();
        reader.release();
        io_env env;
        env.executor = executor_ref(ex);
        env.stop_token = ss.get_token();
        h.promise().set_environment(&env);

        // No data available: the read suspends.
        h.resume();
        BOOST_TEST(! reader_done);

        // Requesting stop posts the continuation through the executor.
        ss.request_stop();
        BOOST_TEST(! q.empty());

        q.front().resume();
        q.pop();

        BOOST_TEST(reader_done);
        BOOST_TEST(reader_ec == make_error_code(error::canceled));

        h.destroy();
    }

    void
    testReadSomeStopRequestedBeforeSuspend()
    {
        // A read that would block (no data) but whose stop token is
        // already requested resolves to error::canceled without parking.
        std::stop_source ss;
        ss.request_stop();
        bool ran = false;
        run_blocking(ss.get_token())(
            [&]() -> task<>
            {
                auto [a, b] = make_stream_pair();
                // No data provided: the read would otherwise suspend.
                char buf[32] = {};
                auto [ec, n] = co_await b.read_some(make_buffer(buf));
                ran = true;
                BOOST_TEST(ec == cond::canceled);
                BOOST_TEST_EQ(n, 0u);
            }());
        BOOST_TEST(ran);
    }

    void
    testReadSomeDestroyWhileSuspended()
    {
        // Destroying a coroutine while its read is parked must tear down
        // the stop callback and unlink the pending slot, so a later peer
        // operation does not touch a freed claim flag.
        auto [a, b] = make_stream_pair();
        std::queue<std::coroutine_handle<>> q;
        queuing_executor ex(q);
        std::stop_source ss;

        auto reader = [](stream& s) -> task<>
        {
            char buf[32] = {};
            auto [ec, n] = co_await s.read_some(make_buffer(buf));
            (void)ec;
            (void)n;
        }(b);

        auto h = reader.handle();
        reader.release();
        io_env env;
        env.executor = executor_ref(ex);
        env.stop_token = ss.get_token();
        h.promise().set_environment(&env);

        h.resume();
        BOOST_TEST(q.empty());

        // Destroy the suspended reader; the awaitable destructor runs.
        h.destroy();

        // The pending slot is unlinked: a subsequent peer write finds no
        // parked reader and does not dereference the freed claim flag.
        a.provide("late");
        BOOST_TEST(true);
    }

    void
    testReadSomeCancellationCrossThread()
    {
        // A read suspended on one thread is cancelled by a stop request
        // issued from another, exercising the stop-callback path under
        // real concurrency (validated by TSan). Only the stop callback
        // crosses threads; the stream itself is touched solely by the
        // pool thread, honoring its single-threaded contract.
        thread_pool pool(1);
        std::latch done(1);
        std::latch suspended(1);
        std::stop_source ss;

        auto [a, b] = make_stream_pair();
        std::error_code reader_ec;

        auto reader = [&]() -> task<>
        {
            char buf[32] = {};
            suspended.count_down();
            auto [ec, n] = co_await b.read_some(make_buffer(buf));
            reader_ec = ec;
            (void)n;
        };

        run_async(pool.get_executor(), ss.get_token(),
            [&]() { done.count_down(); },
            [&](std::exception_ptr) { done.count_down(); })(reader());

        suspended.wait();
        // Give await_suspend time to register the stop callback.
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        ss.request_stop();

        done.wait();
        BOOST_TEST(reader_ec == make_error_code(error::canceled));
    }

    void
    testReadSomeStopWithDataAvailable()
    {
        // When data is already buffered, a read completes normally even
        // if the stop token is set: cancellation only applies to a read
        // that would otherwise block.
        std::stop_source ss;
        ss.request_stop();
        bool ran = false;
        run_blocking(ss.get_token())(
            [&]() -> task<>
            {
                auto [a, b] = make_stream_pair();
                a.provide("hello");

                char buf[32] = {};
                auto [ec, n] = co_await b.read_some(make_buffer(buf));
                ran = true;
                BOOST_TEST(! ec);
                BOOST_TEST_EQ(n, 5u);
                BOOST_TEST_EQ(std::string_view(buf, n), "hello");
            }());
        BOOST_TEST(ran);
    }

    void
    testWriteSomeCancellation()
    {
        // write_some never blocks, so it honors the stop token up front:
        // an already-requested stop yields error::canceled.
        std::stop_source ss;
        ss.request_stop();
        bool ran = false;
        run_blocking(ss.get_token())(
            [&]() -> task<>
            {
                auto [a, b] = make_stream_pair();
                auto [ec, n] = co_await a.write_some(
                    const_buffer("hi", 2));
                ran = true;
                BOOST_TEST(ec == cond::canceled);
                BOOST_TEST_EQ(n, 0u);
            }());
        BOOST_TEST(ran);
    }

    void
    run()
    {
        // Read operations
        testReadSome();
        testReadSomePartial();
        testReadSomeMultiple();
        testReadSomeEof();
        testReadSomeBufferSequence();
        testReadSomeEmpty();
        testReadSomeEmptyNoData();
        testMaxReadSize();
        testMaxReadSizeMultiple();

        // Write operations
        testWriteSome();
        testWriteSomeMultiple();
        testWriteSomeBufferSequence();
        testWriteSomeEmpty();
        testExpect();
        testExpectMismatch();
        testExpectPartialMatch();
        testExpectExcessData();
        testMaxWriteSize();
        testMaxWriteSizeMultiple();

        // Combined operations
        testReadWrite();
        testLoopback();
        testFuseReadErrorInjection();
        testFuseWriteErrorInjection();

        // Cancellation
        testReadSomeCancellationWhileSuspended();
        testReadSomeStopRequestedBeforeSuspend();
        testReadSomeDestroyWhileSuspended();
        testReadSomeCancellationCrossThread();
        testReadSomeStopWithDataAvailable();
        testWriteSomeCancellation();
    }
};

TEST_SUITE(stream_test, "boost.capy.test.stream");

//--------------------------------------------

static_assert(Stream<stream>);
static_assert(ReadStream<stream>);
static_assert(WriteStream<stream>);

class stream_pair_test
{
public:
    void
    testWriteThenRead()
    {
        fuse f;
        auto r = f.armed([&](fuse&) -> task<> {
            auto [a, b] = make_stream_pair(f);

            auto [ec1, n1] = co_await a.write_some(
                make_buffer("hello", 5));
            if(ec1)
                co_return;
            BOOST_TEST_EQ(n1, 5u);

            char buf[32] = {};
            auto [ec2, n2] = co_await b.read_some(
                make_buffer(buf));
            if(ec2)
                co_return;
            BOOST_TEST_EQ(n2, 5u);
            BOOST_TEST_EQ(
                std::string_view(buf, n2), "hello");
        });
        BOOST_TEST(r.success);
    }

    void
    testReadThenWrite()
    {
        // Suspension path: reader suspends, writer wakes it.
        // Guard closes peer on fuse error so no deadlock.
        fuse f;
        auto r = f.armed([&](fuse&) -> task<> {
            auto [a, b] = make_stream_pair(f);

            (void) co_await when_all(
                [](stream a) -> io_task<> {
                    char buf[32] = {};
                    auto [ec, n] = co_await a.read_some(
                        make_buffer(buf));
                    if(ec)
                        co_return io_result<>{ec};
                    BOOST_TEST_EQ(n, 5u);
                    BOOST_TEST_EQ(
                        std::string_view(buf, n),
                        "hello");
                    co_return io_result<>{};
                }(std::move(a)),
                [](stream b) -> io_task<> {
                    auto [ec, n] = co_await b.write_some(
                        make_buffer("hello", 5));
                    if(ec)
                        co_return io_result<>{ec};
                    BOOST_TEST_EQ(n, 5u);
                    co_return io_result<>{};
                }(std::move(b))
            );
        });
        BOOST_TEST(r.success);
    }

    void
    testBidirectional()
    {
        fuse f;
        auto r = f.armed([&](fuse&) -> task<> {
            auto [a, b] = make_stream_pair(f);

            // a -> b
            auto [ec1, n1] = co_await a.write_some(
                make_buffer("from-a", 6));
            if(ec1)
                co_return;
            BOOST_TEST_EQ(n1, 6u);

            // b -> a
            auto [ec2, n2] = co_await b.write_some(
                make_buffer("from-b", 6));
            if(ec2)
                co_return;
            BOOST_TEST_EQ(n2, 6u);

            // read on b (data from a)
            char buf1[32] = {};
            auto [ec3, n3] = co_await b.read_some(
                make_buffer(buf1));
            if(ec3)
                co_return;
            BOOST_TEST_EQ(n3, 6u);
            BOOST_TEST_EQ(
                std::string_view(buf1, n3), "from-a");

            // read on a (data from b)
            char buf2[32] = {};
            auto [ec4, n4] = co_await a.read_some(
                make_buffer(buf2));
            if(ec4)
                co_return;
            BOOST_TEST_EQ(n4, 6u);
            BOOST_TEST_EQ(
                std::string_view(buf2, n4), "from-b");
        });
        BOOST_TEST(r.success);
    }

    void
    testPartialRead()
    {
        fuse f;
        auto r = f.armed([&](fuse&) -> task<> {
            auto [a, b] = make_stream_pair(f);

            auto [ec1, n1] = co_await a.write_some(
                make_buffer("hello world", 11));
            if(ec1)
                co_return;
            BOOST_TEST_EQ(n1, 11u);

            // Small buffer reads partial data
            char buf[5] = {};
            auto [ec2, n2] = co_await b.read_some(
                make_buffer(buf));
            if(ec2)
                co_return;
            BOOST_TEST_EQ(n2, 5u);
            BOOST_TEST_EQ(
                std::string_view(buf, n2), "hello");

            // Remainder still available
            char buf2[32] = {};
            auto [ec3, n3] = co_await b.read_some(
                make_buffer(buf2));
            if(ec3)
                co_return;
            BOOST_TEST_EQ(n3, 6u);
            BOOST_TEST_EQ(
                std::string_view(buf2, n3), " world");
        });
        BOOST_TEST(r.success);
    }

    void
    testMultipleWritesAccumulate()
    {
        fuse f;
        auto r = f.armed([&](fuse&) -> task<> {
            auto [a, b] = make_stream_pair(f);

            auto [ec1, n1] = co_await a.write_some(
                make_buffer("aaa", 3));
            if(ec1)
                co_return;
            BOOST_TEST_EQ(n1, 3u);

            auto [ec2, n2] = co_await a.write_some(
                make_buffer("bbb", 3));
            if(ec2)
                co_return;
            BOOST_TEST_EQ(n2, 3u);

            char buf[32] = {};
            auto [ec3, n3] = co_await b.read_some(
                make_buffer(buf));
            if(ec3)
                co_return;
            BOOST_TEST_EQ(n3, 6u);
            BOOST_TEST_EQ(
                std::string_view(buf, n3), "aaabbb");
        });
        BOOST_TEST(r.success);
    }

    void
    testEmptyWrite()
    {
        fuse f;
        auto r = f.armed([&](fuse&) -> task<> {
            auto [a, b] = make_stream_pair(f);

            auto [ec, n] = co_await a.write_some(
                const_buffer());
            // Empty write does not consult fuse
            BOOST_TEST(! ec);
            BOOST_TEST_EQ(n, 0u);
        });
        BOOST_TEST(r.success);
    }

    void
    testBufferSequenceRead()
    {
        fuse f;
        auto r = f.armed([&](fuse&) -> task<> {
            auto [a, b] = make_stream_pair(f);

            auto [ec1, n1] = co_await a.write_some(
                make_buffer("helloworld", 10));
            if(ec1)
                co_return;

            char buf1[5] = {};
            char buf2[5] = {};
            std::array<mutable_buffer, 2> bufs = {{
                make_buffer(buf1),
                make_buffer(buf2)
            }};

            auto [ec2, n2] = co_await b.read_some(bufs);
            if(ec2)
                co_return;
            BOOST_TEST_EQ(n2, 10u);
            BOOST_TEST_EQ(
                std::string_view(buf1, 5), "hello");
            BOOST_TEST_EQ(
                std::string_view(buf2, 5), "world");
        });
        BOOST_TEST(r.success);
    }

    void
    testBufferSequenceWrite()
    {
        fuse f;
        auto r = f.armed([&](fuse&) -> task<> {
            auto [a, b] = make_stream_pair(f);
            std::array<const_buffer, 2> bufs = {{
                make_buffer("hello", 5),
                make_buffer("world", 5)
            }};

            auto [ec1, n1] = co_await a.write_some(bufs);
            if(ec1)
                co_return;
            BOOST_TEST_EQ(n1, 10u);

            char buf[32] = {};
            auto [ec2, n2] = co_await b.read_some(
                make_buffer(buf));
            if(ec2)
                co_return;
            BOOST_TEST_EQ(n2, 10u);
            BOOST_TEST_EQ(
                std::string_view(buf, n2), "helloworld");
        });
        BOOST_TEST(r.success);
    }

    void
    testSuspendedReadWithPartialBuffer()
    {
        fuse f;
        auto r = f.armed([&](fuse&) -> task<> {
            auto [a, b] = make_stream_pair(f);

            (void) co_await when_all(
                [](stream a) -> io_task<> {
                    char buf[3] = {};
                    auto [ec, n] = co_await a.read_some(
                        make_buffer(buf));
                    if(ec)
                        co_return io_result<>{ec};
                    BOOST_TEST_EQ(n, 3u);
                    BOOST_TEST_EQ(
                        std::string_view(buf, n),
                        "hel");
                    co_return io_result<>{};
                }(std::move(a)),
                [](stream b) -> io_task<> {
                    auto [ec, n] = co_await b.write_some(
                        make_buffer("hello", 5));
                    if(ec)
                        co_return io_result<>{ec};
                    BOOST_TEST_EQ(n, 5u);
                    co_return io_result<>{};
                }(std::move(b))
            );
        });
        BOOST_TEST(r.success);
    }

    void
    testFuseWriteErrorInjection()
    {
        int write_success_count = 0;
        int write_error_count = 0;

        fuse f;
        auto r = f.armed([&](fuse&) -> task<> {
            auto [a, b] = make_stream_pair(f);

            auto [ec, n] = co_await a.write_some(
                make_buffer("data", 4));
            if(ec)
            {
                ++write_error_count;
                co_return;
            }
            ++write_success_count;
        });

        BOOST_TEST(r.success);
        BOOST_TEST(write_error_count > 0);
        BOOST_TEST(write_success_count > 0);
    }

    void
    testFuseReadErrorInjection()
    {
        int read_success_count = 0;
        int read_error_count = 0;

        fuse f;
        auto r = f.armed([&](fuse&) -> task<> {
            auto [a, b] = make_stream_pair(f);

            // Write first so data is available
            auto [ec1, n1] = co_await a.write_some(
                make_buffer("data", 4));
            if(ec1)
                co_return;

            char buf[32] = {};
            auto [ec2, n2] = co_await b.read_some(
                make_buffer(buf));
            if(ec2)
            {
                ++read_error_count;
                co_return;
            }
            ++read_success_count;
        });

        BOOST_TEST(r.success);
        BOOST_TEST(read_error_count > 0);
        BOOST_TEST(read_success_count > 0);
    }

    void
    testClose()
    {
        // close() resumes a suspended reader with eof
        fuse f;
        run_blocking()([&]() -> task<> {
            auto [a, b] = make_stream_pair(f);

            (void) co_await when_all(
                [](stream a) -> io_task<> {
                    char buf[32] = {};
                    auto [ec, n] = co_await a.read_some(
                        make_buffer(buf));
                    BOOST_TEST(ec == cond::eof);
                    BOOST_TEST_EQ(n, 0u);
                    co_return io_result<>{ec};
                }(std::move(a)),
                [](stream b) -> io_task<> {
                    b.close();
                    co_return io_result<>{};
                }(std::move(b))
            );
        }());
    }

    void
    testCloseSubsequentOps()
    {
        // Directional close: a.close() signals eof to
        // b's reads, but b can still write to a.
        fuse f;
        run_blocking()([&]() -> task<> {
            auto [a, b] = make_stream_pair(f);
            a.close();

            // b reads eof (no data was buffered)
            char buf[32] = {};
            auto [ec1, n1] = co_await b.read_some(
                make_buffer(buf));
            BOOST_TEST(ec1 == cond::eof);
            BOOST_TEST_EQ(n1, 0u);

            // b can still write to a
            auto [ec2, n2] = co_await b.write_some(
                make_buffer("data", 4));
            BOOST_TEST(! ec2);
            BOOST_TEST_EQ(n2, 4u);

            // a can still read b's writes
            auto [ec3, n3] = co_await a.read_some(
                make_buffer(buf));
            BOOST_TEST(! ec3);
            BOOST_TEST_EQ(n3, 4u);
            BOOST_TEST_EQ(
                std::string_view(buf, n3), "data");
        }());
    }

    void
    testFuseClosesOtherEnd()
    {
        // Fuse error on writer closes the suspended reader
        fuse f;
        auto r = f.armed([&](fuse&) -> task<> {
            auto [a, b] = make_stream_pair(f);

            (void) co_await when_all(
                [](stream a) -> io_task<> {
                    // Reader suspends waiting for data.
                    // Gets data, eof from peer's guard,
                    // or its own fuse error on resume.
                    char buf[32] = {};
                    auto [ec, n] = co_await a.read_some(
                        make_buffer(buf));
                    if(ec)
                        co_return io_result<>{ec};
                    BOOST_TEST_EQ(n, 5u);
                    co_return io_result<>{};
                }(std::move(a)),
                [](stream b) -> io_task<> {
                    // Writer may get fuse error, which
                    // closes the peer via the guard
                    auto [ec, n] = co_await b.write_some(
                        make_buffer("hello", 5));
                    if(ec)
                        co_return io_result<>{ec};
                    BOOST_TEST_EQ(n, 5u);
                    co_return io_result<>{};
                }(std::move(b))
            );
        });
        BOOST_TEST(r.success);
    }

    void
    run()
    {
        testWriteThenRead();
        testReadThenWrite();
        testBidirectional();
        testPartialRead();
        testMultipleWritesAccumulate();
        testEmptyWrite();
        testBufferSequenceRead();
        testBufferSequenceWrite();
        testSuspendedReadWithPartialBuffer();
        testFuseWriteErrorInjection();
        testFuseReadErrorInjection();
        testClose();
        testCloseSubsequentOps();
        testFuseClosesOtherEnd();
    }
};

TEST_SUITE(
    stream_pair_test,
    "boost.capy.test.stream_pair");

} // test
} // capy
} // boost
