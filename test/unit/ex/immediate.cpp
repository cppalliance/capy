//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Test that header file is self-contained.
#include <boost/capy/ex/immediate.hpp>

#include <boost/capy/concept/io_awaitable.hpp>
#include <boost/capy/ex/io_env.hpp>
#include <boost/capy/io_task.hpp>
#include <boost/capy/test/run_blocking.hpp>

#include "test/unit/test_helpers.hpp"

#include <string>

namespace boost {
namespace capy {

static_assert(IoAwaitable<immediate<int>>);
static_assert(IoAwaitable<immediate<io_result<>>>);
static_assert(IoAwaitable<immediate<io_result<std::size_t>>>);

struct immediate_test
{
    void
    testAwaitReady()
    {
        // immediate<int> is always ready
        {
            immediate<int> im{42};
            BOOST_TEST(im.await_ready());
        }

        // immediate<io_result<>> is always ready
        {
            immediate<io_result<>> im{{}};
            BOOST_TEST(im.await_ready());
        }

        // immediate<io_result<std::size_t>> is always ready
        {
            immediate<io_result<std::size_t>> im{{{}, 100}};
            BOOST_TEST(im.await_ready());
        }
    }

    void
    testAwaitResume()
    {
        // immediate<int> returns value
        {
            immediate<int> im{42};
            BOOST_TEST_EQ(im.await_resume(), 42);
        }

        // immediate<std::string> returns value
        {
            immediate<std::string> im{"hello"};
            BOOST_TEST_EQ(im.await_resume(), "hello");
        }

        // immediate<io_result<>> returns result
        {
            immediate<io_result<>> im{{}};
            auto r = im.await_resume();
            BOOST_TEST(!r.ec);
        }

        // immediate<io_result<std::size_t>> returns result with value
        {
            immediate<io_result<std::size_t>> im{{{}, 42}};
            auto r = im.await_resume();
            BOOST_TEST(!r.ec);
            BOOST_TEST_EQ(r.t1, 42u);
        }
    }

    void
    testCoAwait()
    {
        // co_await immediate<int>
        {
            auto coro = []() -> task<int> {
                co_return co_await immediate<int>{42};
            };
            int result{};
            test::run_blocking([&](int v) { result = v; })(coro());
            BOOST_TEST_EQ(result, 42);
        }

        // co_await immediate<io_result<std::size_t>>
        {
            auto coro = []() -> io_task<std::size_t> {
                co_return co_await immediate<io_result<std::size_t>>{{{}, 100}};
            };
            io_result<std::size_t> result{};
            test::run_blocking([&](io_result<std::size_t> v) { result = v; })(coro());
            BOOST_TEST(!result.ec);
            BOOST_TEST_EQ(result.t1, 100u);
        }

        // Structured binding with co_await
        {
            auto coro = []() -> task<std::size_t> {
                auto [ec, n] = co_await immediate<io_result<std::size_t>>{{{}, 50}};
                if(ec)
                    co_return 0;
                co_return n;
            };
            std::size_t result{};
            test::run_blocking([&](std::size_t v) { result = v; })(coro());
            BOOST_TEST_EQ(result, 50u);
        }
    }

    void
    testReadyVoid()
    {
        // ready() creates successful void result
        {
            auto im = ready();
            BOOST_TEST(im.await_ready());
            auto r = im.await_resume();
            BOOST_TEST(!r.ec);
        }

        // co_await ready()
        {
            auto coro = []() -> task<bool> {
                auto [ec] = co_await ready();
                co_return !ec;
            };
            bool result{};
            test::run_blocking([&](bool v) { result = v; })(coro());
            BOOST_TEST(result);
        }
    }

    void
    testReadySingleValue()
    {
        // ready(T1) creates successful single-value result
        {
            auto im = ready(std::size_t{42});
            BOOST_TEST(im.await_ready());
            auto r = im.await_resume();
            BOOST_TEST(!r.ec);
            BOOST_TEST_EQ(r.t1, 42u);
        }

        // co_await ready(n)
        {
            auto coro = []() -> task<std::size_t> {
                auto [ec, n] = co_await ready(std::size_t{100});
                if(ec)
                    co_return 0;
                co_return n;
            };
            std::size_t result{};
            test::run_blocking([&](std::size_t v) { result = v; })(coro());
            BOOST_TEST_EQ(result, 100u);
        }
    }

    void
    testReadyTwoValues()
    {
        // ready(T1, T2) creates successful two-value result
        {
            auto im = ready(42, 3.14);
            BOOST_TEST(im.await_ready());
            auto r = im.await_resume();
            BOOST_TEST(!r.ec);
            BOOST_TEST_EQ(r.t1, 42);
            BOOST_TEST_EQ(r.t2, 3.14);
        }

        // co_await ready(a, b)
        {
            auto coro = []() -> task<double> {
                auto [ec, a, b] = co_await ready(10, 2.5);
                if(ec)
                    co_return 0.0;
                co_return a * b;
            };
            double result{};
            test::run_blocking([&](double v) { result = v; })(coro());
            BOOST_TEST_EQ(result, 25.0);
        }
    }

    void
    testReadyThreeValues()
    {
        // ready(T1, T2, T3) creates successful three-value result
        {
            auto im = ready(1, 2, 3);
            BOOST_TEST(im.await_ready());
            auto r = im.await_resume();
            BOOST_TEST(!r.ec);
            BOOST_TEST_EQ(r.t1, 1);
            BOOST_TEST_EQ(r.t2, 2);
            BOOST_TEST_EQ(r.t3, 3);
        }

        // co_await ready(a, b, c)
        {
            auto coro = []() -> task<int> {
                auto [ec, a, b, c] = co_await ready(10, 20, 30);
                if(ec)
                    co_return 0;
                co_return a + b + c;
            };
            int result{};
            test::run_blocking([&](int v) { result = v; })(coro());
            BOOST_TEST_EQ(result, 60);
        }
    }

    void
    testReadyWithError()
    {
        // ready(ec) creates failed void result
        {
            auto ec = make_error_code(std::errc::invalid_argument);
            auto im = ready(ec);
            BOOST_TEST(im.await_ready());
            auto r = im.await_resume();
            BOOST_TEST(r.ec);
        }

        // ready(ec, T1) creates failed single-value result
        {
            auto ec = make_error_code(std::errc::invalid_argument);
            auto im = ready(ec, std::size_t{0});
            BOOST_TEST(im.await_ready());
            auto r = im.await_resume();
            BOOST_TEST(r.ec);
            BOOST_TEST_EQ(r.t1, 0u);
        }

        // ready(ec, T1, T2) creates failed two-value result
        {
            auto ec = make_error_code(std::errc::invalid_argument);
            auto im = ready(ec, 0, 0.0);
            auto r = im.await_resume();
            BOOST_TEST(r.ec);
        }

        // ready(ec, T1, T2, T3) creates failed three-value result
        {
            auto ec = make_error_code(std::errc::invalid_argument);
            auto im = ready(ec, 0, 0, 0);
            auto r = im.await_resume();
            BOOST_TEST(r.ec);
        }

        // co_await with error
        {
            auto coro = []() -> task<std::size_t> {
                auto ec = make_error_code(std::errc::invalid_argument);
                auto [err, n] = co_await ready(ec, std::size_t{0});
                if(err)
                    co_return 999;
                co_return n;
            };
            std::size_t result{};
            test::run_blocking([&](std::size_t v) { result = v; })(coro());
            BOOST_TEST_EQ(result, 999u);
        }
    }

    void
    testWriteSinkPattern()
    {
        // Simulate WriteSink adapter pattern
        struct mock_sync_parser
        {
            std::size_t total_ = 0;
            bool complete_ = false;

            std::error_code
            write(std::size_t n)
            {
                total_ += n;
                return {};
            }

            void finish() { complete_ = true; }
            bool is_complete() const { return complete_; }
        };

        struct parser_write_sink
        {
            mock_sync_parser& pr_;

            immediate<io_result<std::size_t>>
            write(std::size_t n)
            {
                auto ec = pr_.write(n);
                if(ec)
                    return ready(ec, std::size_t{0});
                return ready(n);
            }

            immediate<io_result<>>
            write_eof()
            {
                pr_.finish();
                if(!pr_.is_complete())
                    return ready(make_error_code(
                        std::errc::invalid_argument));
                return ready();
            }
        };

        // Use the adapter
        {
            mock_sync_parser parser;
            parser_write_sink sink{parser};

            auto coro = [&]() -> task<std::size_t> {
                auto [ec1, n1] = co_await sink.write(10);
                if(ec1)
                    co_return 0;

                auto [ec2, n2] = co_await sink.write(20);
                if(ec2)
                    co_return 0;

                auto [ec3] = co_await sink.write_eof();
                if(ec3)
                    co_return 0;

                co_return n1 + n2;
            };

            std::size_t result{};
            test::run_blocking([&](std::size_t v) { result = v; })(coro());
            BOOST_TEST_EQ(result, 30u);
            BOOST_TEST_EQ(parser.total_, 30u);
            BOOST_TEST(parser.is_complete());
        }
    }

    void
    testIoAwaitableProtocol()
    {
        // Test await_suspend with executor (IoAwaitable protocol)
        {
            int dispatch_count = 0;
            test_executor ex(dispatch_count);

            immediate<int> im{42};
            BOOST_TEST(im.await_ready());

            // Even though we call await_suspend, it returns noop
            // because the result is already ready
            io_env env{ex, {}};
            auto result = im.await_suspend(
                std::noop_coroutine(),
                &env);
            BOOST_TEST(result == std::noop_coroutine());

            // Executor was not used since we're already ready
            BOOST_TEST_EQ(dispatch_count, 0);
        }
    }

    void
    testMoveSemantics()
    {
        // Move-only types work
        {
            struct move_only
            {
                int value;
                move_only(int v) : value(v) {}
                move_only(move_only&&) = default;
                move_only& operator=(move_only&&) = default;
                move_only(move_only const&) = delete;
                move_only& operator=(move_only const&) = delete;
            };

            immediate<move_only> im{move_only{42}};
            auto result = im.await_resume();
            BOOST_TEST_EQ(result.value, 42);
        }
    }

    void
    run()
    {
        testAwaitReady();
        testAwaitResume();
        testCoAwait();
        testReadyVoid();
        testReadySingleValue();
        testReadyTwoValues();
        testReadyThreeValues();
        testReadyWithError();
        testWriteSinkPattern();
        testIoAwaitableProtocol();
        testMoveSemantics();
    }
};

TEST_SUITE(
    immediate_test,
    "boost.capy.immediate");

} // capy
} // boost
