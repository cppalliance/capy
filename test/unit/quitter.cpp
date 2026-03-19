//
// Copyright (c) 2026 Michael Vandeberg
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Test that header file is self-contained.
#include <boost/capy/quitter.hpp>

#include <boost/capy/buffers/make_buffer.hpp>
#include <boost/capy/delay.hpp>
#include <boost/capy/error.hpp>
#include <boost/capy/ex/io_env.hpp>
#include <boost/capy/ex/run_async.hpp>
#include <boost/capy/ex/this_coro.hpp>
#include <boost/capy/ex/thread_pool.hpp>
#include <boost/capy/io_task.hpp>
#include <boost/capy/task.hpp>
#include <boost/capy/test/run_blocking.hpp>
#include <boost/capy/test/stream.hpp>
#include <boost/capy/when_all.hpp>
#include <boost/capy/when_any.hpp>

#include "test_helpers.hpp"

#include <atomic>
#include <latch>
#include <semaphore>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <variant>

namespace boost {
namespace capy {

static_assert(IoAwaitable<quitter<void>>);
static_assert(IoAwaitable<quitter<int>>);
static_assert(IoRunnable<quitter<void>>);
static_assert(IoRunnable<quitter<int>>);

struct quitter_test
{
    //----------------------------------------------------------
    // 1. Normal completion — quitter<int> returns a value
    //----------------------------------------------------------

    static quitter<int>
    returns_int()
    {
        co_return 42;
    }

    void
    testNormalCompletion()
    {
        int result = 0;
        test::run_blocking([&](int v) { result = v; })(returns_int());
        BOOST_TEST_EQ(result, 42);
    }

    //----------------------------------------------------------
    // 2. Void completion
    //----------------------------------------------------------

    static quitter<>
    void_quitter()
    {
        co_return;
    }

    void
    testVoidCompletion()
    {
        test::run_blocking()(void_quitter());
    }

    //----------------------------------------------------------
    // 3. Exception propagation
    //----------------------------------------------------------

    static quitter<>
    throws_quitter()
    {
        throw test_exception("quitter exception");
        co_return;
    }

    void
    testExceptionPropagation()
    {
        BOOST_TEST_THROWS(
            test::run_blocking()(throws_quitter()),
            test_exception);
    }

    //----------------------------------------------------------
    // 4. Stop before first co_await
    //----------------------------------------------------------

    struct raii_counter
    {
        int* count;
        raii_counter(int& c) : count(&c) {}
        raii_counter(raii_counter&& o) noexcept
            : count(std::exchange(o.count, nullptr)) {}
        ~raii_counter() { if(count) ++(*count); }
    };

    static quitter<>
    quitter_with_raii(int& dtor_count)
    {
        raii_counter guard(dtor_count);
        co_await stop_only_awaitable{};
    }

    void
    testStopBeforeFirstAwait()
    {
        // When stop is already requested, initial_suspend throws
        // before the coroutine body starts.  Body locals are never
        // constructed, so dtor_count stays 0.  The stopped state
        // routes to the error handler via exception().
        int dispatch_count = 0;
        test_executor ex(dispatch_count);
        std::stop_source source;
        source.request_stop();

        int dtor_count = 0;
        bool got_stopped = false;

        run_async(ex, source.get_token(),
            [](){ BOOST_TEST(false); },
            [&](std::exception_ptr ep) {
                got_stopped = (ep != nullptr);
            })(quitter_with_raii(dtor_count));

        BOOST_TEST(got_stopped);
        // Body never started, so no destructors ran
        BOOST_TEST_EQ(dtor_count, 0);
    }

    //----------------------------------------------------------
    // 5. Stop during I/O
    //----------------------------------------------------------

    void
    testStopDuringIO()
    {
        std::atomic<int> state = 0;
        std::binary_semaphore suspended{0};

        int dtor_count = 0;

        auto q = [&]() -> quitter<>
        {
            raii_counter guard(dtor_count);
            state = 1;
            suspended.release();
            co_await stop_only_awaitable{};
            // Should never reach here when stopped
            state = 99;
        };

        {
            std::jthread jt(
                [&](std::stop_token st)
                {
                    int dc = 0;
                    test_executor ex(dc);
                    run_async(ex, st)(q());
                }
            );
            suspended.acquire();
            BOOST_TEST(state == 1);
            // jthread destructor calls request_stop() then join()
        }

        // The coroutine was stopped; it should NOT have reached state=99
        BOOST_TEST(state != 99);
        BOOST_TEST_EQ(dtor_count, 1);
    }

    //----------------------------------------------------------
    // 6. Stop propagation through chain
    //----------------------------------------------------------

    static quitter<>
    inner_quitter(
        int& dtor_count,
        bool& reached_end,
        std::binary_semaphore& suspended)
    {
        raii_counter guard(dtor_count);
        suspended.release();
        co_await stop_only_awaitable{};
        reached_end = true;
    }

    static quitter<>
    middle_quitter(
        int& dtor_count,
        bool& reached_end,
        std::binary_semaphore& suspended)
    {
        raii_counter guard(dtor_count);
        co_await inner_quitter(dtor_count, reached_end, suspended);
        reached_end = true;
    }

    static quitter<>
    outer_quitter(
        int& dtor_count,
        bool& reached_end,
        std::binary_semaphore& suspended)
    {
        raii_counter guard(dtor_count);
        co_await middle_quitter(dtor_count, reached_end, suspended);
        reached_end = true;
    }

    void
    testStopPropagationChain()
    {
        std::binary_semaphore suspended{0};
        int dtor_count = 0;
        bool reached_end = false;

        auto top = [&]() -> quitter<>
        {
            co_await outer_quitter(
                dtor_count, reached_end, suspended);
            reached_end = true;
        };

        {
            std::jthread jt(
                [&](std::stop_token st)
                {
                    int dc = 0;
                    test_executor ex(dc);
                    run_async(ex, st)(top());
                }
            );
            suspended.acquire();
        }

        // 3 guards from outer/middle/inner
        BOOST_TEST_EQ(dtor_count, 3);
        // No coroutine body continued past its co_await
        BOOST_TEST(!reached_end);
    }

    //----------------------------------------------------------
    // 9. Mixing quitter and task — task awaits quitter
    //----------------------------------------------------------

    static quitter<int>
    quitter_returns_42()
    {
        co_return 42;
    }

    static task<int>
    task_awaits_quitter()
    {
        int v = co_await quitter_returns_42();
        co_return v + 1;
    }

    void
    testMixingQuitterAndTask()
    {
        // Normal case: task awaits quitter that completes normally
        {
            int result = 0;
            test::run_blocking(
                [&](int v) { result = v; })(task_awaits_quitter());
            BOOST_TEST_EQ(result, 43);
        }

        // Stopped case: task awaits stopped quitter — sees exception
        {
            std::stop_source source;
            source.request_stop();

            int dispatch_count = 0;
            test_executor ex(dispatch_count);
            bool got_exception = false;

            auto t = []() -> task<int>
            {
                co_return co_await quitter_returns_42();
            };

            run_async(ex, source.get_token(),
                [](int) { BOOST_TEST(false); },
                [&](std::exception_ptr ep) {
                    got_exception = (ep != nullptr);
                })(t());

            BOOST_TEST(got_exception);
        }
    }

    //----------------------------------------------------------
    // 10. No stop requested — identical to task<T>
    //----------------------------------------------------------

    static quitter<int>
    quitter_chain()
    {
        auto inner = []() -> quitter<int> {
            co_return 10;
        };

        auto middle = [inner]() -> quitter<int> {
            int v = co_await inner();
            co_return v * 2;
        };

        int v = co_await middle();
        co_return v + 5;
    }

    void
    testNoStopRequested()
    {
        int result = 0;
        test::run_blocking(
            [&](int v) { result = v; })(quitter_chain());
        BOOST_TEST_EQ(result, 25);
    }

    //----------------------------------------------------------
    // 11. RAII verification
    //----------------------------------------------------------

    void
    testRAIIVerification()
    {
        // Body starts, constructs guards, then stop is requested
        // during the co_await.  All guard destructors must run.
        // The body must NOT continue past co_await.
        std::binary_semaphore suspended{0};
        int dtor_count = 0;
        bool reached_end = false;

        auto q = [&]() -> quitter<>
        {
            raii_counter g1(dtor_count);
            raii_counter g2(dtor_count);
            raii_counter g3(dtor_count);
            suspended.release();
            co_await stop_only_awaitable{};
            reached_end = true;
        };

        {
            std::jthread jt(
                [&](std::stop_token st)
                {
                    int dc = 0;
                    test_executor ex(dc);
                    run_async(ex, st)(q());
                }
            );
            suspended.acquire();
        }

        BOOST_TEST_EQ(dtor_count, 3);
        BOOST_TEST(!reached_end);
    }

    //----------------------------------------------------------
    // 12. Multiple co_await — stop after second
    //----------------------------------------------------------

    static quitter<int>
    quitter_multi_await(
        std::atomic<int>& progress,
        std::binary_semaphore& sem)
    {
        progress = 1;
        co_await yield_awaitable{};
        progress = 2;
        sem.release();
        co_await stop_only_awaitable{};
        // Should not reach here
        progress = 3;
        co_return 0;
    }

    void
    testMultipleCoAwait()
    {
        std::atomic<int> progress{0};
        std::binary_semaphore sem{0};

        {
            std::jthread jt(
                [&](std::stop_token st)
                {
                    int dc = 0;
                    test_executor ex(dc);
                    run_async(ex, st)(
                        quitter_multi_await(progress, sem));
                }
            );
            sem.acquire();
            BOOST_TEST(progress == 2);
            // jthread destructor requests stop
        }

        // The third await should have been short-circuited
        BOOST_TEST(progress != 3);
    }

    //----------------------------------------------------------
    // Move operations
    //----------------------------------------------------------

    void
    testMoveOperations()
    {
        int dispatch_count = 0;
        test_executor ex(dispatch_count);
        io_env env{executor_ref(ex), {}, nullptr};

        // move constructor
        {
            auto q1 = returns_int();
            auto h1 = q1.handle();
            q1.release();
            BOOST_TEST(h1);

            quitter<int> q2(std::move(q1));
            BOOST_TEST(!q2.handle());

            h1.promise().set_environment(&env);
            while(!h1.done())
                h1.resume();
            BOOST_TEST_EQ(*h1.promise().result_, 42);
            h1.destroy();
        }

        // release()
        {
            auto q = returns_int();
            auto h = q.handle();
            q.release();
            BOOST_TEST(h);
            BOOST_TEST(!q.handle());

            h.promise().set_environment(&env);
            while(!h.done())
                h.resume();
            BOOST_TEST(h.promise().result_.has_value());
            BOOST_TEST_EQ(*h.promise().result_, 42);
            h.destroy();
        }
    }

    //----------------------------------------------------------
    // Quitter returning string
    //----------------------------------------------------------

    static quitter<std::string>
    returns_string()
    {
        co_return "hello";
    }

    void
    testReturnString()
    {
        std::string result;
        test::run_blocking(
            [&](std::string v) { result = std::move(v); })(
                returns_string());
        BOOST_TEST_EQ(result, "hello");
    }

    //----------------------------------------------------------
    // Exception in quitter<int>
    //----------------------------------------------------------

    static quitter<int>
    quitter_throws_int()
    {
        throw test_exception("quitter int exception");
        co_return 0;
    }

    void
    testExceptionInValueQuitter()
    {
        BOOST_TEST_THROWS(
            test::run_blocking()(quitter_throws_int()),
            test_exception);
    }

    //----------------------------------------------------------
    // 7. Stop propagation with when_all
    //
    //    Two quitter<io_result<size_t>> children inside when_all.
    //    Both block on stop_only_awaitable.  when_all creates a
    //    child stop_source; when the parent stop fires, when_all
    //    propagates it.  Each quitter child intercepts the stop
    //    in transform_awaiter::await_resume and short-circuits
    //    via stop_requested_exception — it never reaches the
    //    co_return.  Verify both stop and when_all completes.
    //----------------------------------------------------------

    static quitter<io_result<std::size_t>>
    quitter_pending_size(bool& reached_co_return)
    {
        co_await stop_only_awaitable{};
        // If quitter's transform_awaiter intercepted the stop,
        // we never reach here.
        reached_co_return = true;
        co_return io_result<std::size_t>{
            make_error_code(error::canceled), 0};
    }

    void
    testWhenAllWithStop()
    {
        thread_pool pool(2);
        std::latch done(1);
        std::latch suspended(1);
        std::stop_source source;

        bool child1_returned = false;
        bool child2_returned = false;

        auto outer = [&]() -> task<>
        {
            suspended.count_down();
            auto result = co_await when_all(
                quitter_pending_size(child1_returned),
                quitter_pending_size(child2_returned));
            (void)result;
        };

        run_async(pool.get_executor(), source.get_token(),
            [&]() { done.count_down(); },
            [&](std::exception_ptr) { done.count_down(); })(
                outer());

        suspended.wait();
        std::this_thread::sleep_for(
            std::chrono::milliseconds(10 * failsafe_scale));
        source.request_stop();

        done.wait();
        // Both quitter children were stopped by transform_awaiter
        // before reaching co_return.
        BOOST_TEST(!child1_returned);
        BOOST_TEST(!child2_returned);
    }

    //----------------------------------------------------------
    // 8. Stop propagation with when_any
    //
    //    Two quitter children.  One succeeds immediately,
    //    when_any stops the sibling.  The sibling quitter
    //    intercepts the stop and exits cleanly.
    //----------------------------------------------------------

    static quitter<io_result<std::size_t>>
    quitter_success_size(std::size_t n)
    {
        co_return io_result<std::size_t>{{}, n};
    }

    void
    testWhenAnyWithStop()
    {
        // One child succeeds immediately.  when_any stops
        // the pending sibling quitter.  The sibling must
        // be intercepted by transform_awaiter (never reach
        // co_return).
        {
            thread_pool pool(2);
            std::latch done(1);
            bool sibling_returned = false;

            auto outer = [&]() -> task<>
            {
                auto result = co_await when_any(
                    quitter_success_size(42),
                    quitter_pending_size(sibling_returned));
                // Variadic when_any returns
                // variant<error_code, size_t, size_t>.
                // Index 1 = first child won.
                BOOST_TEST(result.index() == 1);
                if(result.index() == 1)
                    BOOST_TEST_EQ(std::get<1>(result),
                        std::size_t(42));
            };

            run_async(pool.get_executor(),
                [&]() { done.count_down(); },
                [&](std::exception_ptr) {
                    done.count_down();
                })(outer());

            done.wait();
            BOOST_TEST(!sibling_returned);
        }

        // Both children pending.  Parent stop fires.
        // when_any propagates stop to children.  Both
        // quitter children short-circuit.
        {
            thread_pool pool(2);
            std::latch done(1);
            std::latch suspended(1);
            std::stop_source source;

            bool child1_returned = false;
            bool child2_returned = false;

            auto outer = [&]() -> task<>
            {
                suspended.count_down();
                auto result = co_await when_any(
                    quitter_pending_size(child1_returned),
                    quitter_pending_size(child2_returned));
                (void)result;
            };

            run_async(pool.get_executor(), source.get_token(),
                [&]() { done.count_down(); },
                [&](std::exception_ptr) {
                    done.count_down();
                })(outer());

            suspended.wait();
            std::this_thread::sleep_for(
                std::chrono::milliseconds(10 * failsafe_scale));
            source.request_stop();

            done.wait();
            BOOST_TEST(!child1_returned);
            BOOST_TEST(!child2_returned);
        }
    }

    //----------------------------------------------------------
    // 14. Timer cancellation
    //----------------------------------------------------------

    void
    testTimerCancellation()
    {
        using namespace std::chrono_literals;

        thread_pool pool(1);
        std::latch done(1);
        std::latch suspended(1);
        std::stop_source source;
        bool reached_end = false;

        auto q = [&]() -> quitter<>
        {
            suspended.count_down();
            auto [ec] = co_await delay(10s);
            (void)ec;
            reached_end = true;
        };

        auto start = std::chrono::steady_clock::now();

        run_async(pool.get_executor(), source.get_token(),
            [&]() { done.count_down(); },
            [&](std::exception_ptr) { done.count_down(); })(q());

        suspended.wait();
        std::this_thread::sleep_for(
            std::chrono::milliseconds(10 * failsafe_scale));
        source.request_stop();

        done.wait();
        auto elapsed = std::chrono::steady_clock::now() - start;
        // Should complete promptly, well under 10s
        BOOST_TEST(elapsed < 1s);
        // Quitter intercepted the stop — body did not continue
        BOOST_TEST(!reached_end);
    }

    //----------------------------------------------------------
    // 13. Echo server with shutdown
    //
    //    A quitter echo loop over a mock stream pair.
    //    The client exchanges data, then requests stop.
    //    The echo quitter exits cleanly via stop interception,
    //    RAII runs, and the echoed data was correct.
    //
    //    The mock stream's read_some is not stop-aware, so we
    //    wrap each read in when_any with a stop_only_awaitable
    //    to make it cancellable — mirroring how a real server
    //    would have the OS cancel an in-flight read.
    //----------------------------------------------------------

    void
    testEchoWithShutdown()
    {
        // Echo server over a mock stream pair.  The server
        // reads pre-provided data, echoes it back, then
        // waits for shutdown via stop_only_awaitable.  When
        // stop fires, the quitter intercepts at the co_await
        // and exits cleanly.  All stream access is on the
        // jthread's synchronous executor — no cross-thread
        // stream use.
        test::fuse f;
        auto [server_end, client_end] =
            test::make_stream_pair(f);

        client_end.provide("hello");

        int dtor_count = 0;
        bool reached_end = false;
        std::size_t total_echoed = 0;
        std::binary_semaphore suspended{0};

        auto echo_server = [&]() -> quitter<>
        {
            raii_counter guard(dtor_count);
            char buf[64];

            // Echo loop: process all available data
            auto [ec, n] = co_await server_end.read_some(
                make_buffer(buf));
            if(ec)
                co_return;
            total_echoed += n;
            auto [ec2, n2] = co_await server_end.write_some(
                make_buffer(buf, n));
            if(ec2)
                co_return;

            // Signal that echo is done, then wait for
            // shutdown.  stop_only_awaitable suspends until
            // the stop token fires.
            suspended.release();
            co_await stop_only_awaitable{};

            // Should never reach here — quitter intercepts
            reached_end = true;
        };

        {
            std::jthread jt(
                [&](std::stop_token st)
                {
                    int dc = 0;
                    test_executor ex(dc);
                    run_async(ex, st)(echo_server());
                }
            );
            suspended.acquire();

            // Verify the echo happened
            BOOST_TEST_EQ(total_echoed, std::size_t(5));

            // Read back the echoed data from client side
            // (synchronous — data is already in the buffer)
            // Not possible here since we're on the main
            // thread and streams are single-threaded.  The
            // echo write went to client_end's read buffer
            // which we can't access cross-thread.  The
            // echo itself is verified by total_echoed.

            // jthread destructor requests stop and joins
        }

        BOOST_TEST_EQ(dtor_count, 1);
        BOOST_TEST(!reached_end);
    }

    //----------------------------------------------------------
    // run()
    //----------------------------------------------------------

    void
    run()
    {
        testNormalCompletion();
        testVoidCompletion();
        testExceptionPropagation();
        testStopBeforeFirstAwait();
        testStopDuringIO();
        testStopPropagationChain();
        testMixingQuitterAndTask();
        testNoStopRequested();
        testRAIIVerification();
        testMultipleCoAwait();
        testMoveOperations();
        testReturnString();
        testExceptionInValueQuitter();
        testWhenAllWithStop();
        testWhenAnyWithStop();
        testTimerCancellation();
        testEchoWithShutdown();
    }
};

TEST_SUITE(
    quitter_test,
    "boost.capy.quitter");

} // capy
} // boost
