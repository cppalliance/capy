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
#include <boost/capy/cond.hpp>
#include <boost/capy/error.hpp>
#include <boost/capy/ex/abandon.hpp>
#include <boost/capy/ex/async_mutex.hpp>
#include <boost/capy/ex/async_waker.hpp>
#include <boost/capy/ex/await_context.hpp>
#include <boost/capy/ex/run.hpp>
#include <boost/capy/ex/run_async.hpp>
#include <boost/capy/ex/strand.hpp>
#include <boost/capy/ex/this_coro.hpp>
#include <boost/capy/ex/thread_pool.hpp>
#include <boost/capy/results.hpp>
#include <boost/capy/task.hpp>
#include <boost/capy/test/run_blocking.hpp>
#include <boost/capy/test/stream.hpp>
#include <boost/capy/when_all.hpp>
#include <boost/capy/when_any.hpp>

#include "death_test.hpp"
#include "test_helpers.hpp"

#include <cstdlib>

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <latch>
#include <memory_resource>
#include <mutex>
#include <queue>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <type_traits>
#include <variant>
#include <vector>

// GCC gives false positive -Wmaybe-uninitialized on structured bindings
// via the tuple protocol inside coroutine frames.
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif

namespace boost {
namespace capy {

static_assert(ContextAwaitable<quitter<void>>);
static_assert(ContextAwaitable<quitter<int>>);
static_assert(ContextRunnable<quitter<void>>);
static_assert(ContextRunnable<quitter<int>>);

struct quitter_test
{
    // True when `ep` holds what a stopped quitter<> or plain-T quitter
    // reports: a std::system_error that compares equal to
    // cond::canceled.
    static bool
    is_stop(std::exception_ptr ep)
    {
        try
        {
            std::rethrow_exception(ep);
        }
        catch(std::system_error const& e)
        {
            return e.code() == cond::canceled;
        }
        catch(...)
        {
            return false;
        }
    }

    // Assigning over a quitter that is not awaited destroys the frame
    // it held.
    void testMoveAssignDestroysHeldFrame()
    {
        int a_down = 0, b_down = 0;
        auto body = [](destroy_count_t) -> quitter<void> { co_return; };
        {
            auto t = body(destroy_count_t(a_down));
            t = body(destroy_count_t(b_down));
            BOOST_TEST_EQ(a_down, 1);
            BOOST_TEST_EQ(b_down, 0);
        }
        BOOST_TEST_EQ(b_down, 1);
    }

#if !defined(_WIN32)
    void testDestroyLentTaskTerminates()
    {
        BOOST_TEST(terminates([] {
            parked slot;
            int n = 0;
            test_executor ex(n);
            await_context env{executor_ref(ex), {}, nullptr};
            auto t = [](parked* s) -> quitter<void> {
                co_await park_awaitable{s};
            }(&slot);
            lend(t, env);
            // destructor runs with the frame lent and suspended
        }));
    }

    void testMoveAssignOverLentTaskTerminates()
    {
        BOOST_TEST(terminates([] {
            parked slot;
            int n = 0;
            test_executor ex(n);
            await_context env{executor_ref(ex), {}, nullptr};
            auto body = [](parked* s) -> quitter<void> {
                co_await park_awaitable{s};
            };
            auto t = body(&slot);
            lend(t, env);
            t = body(&slot);
            std::_Exit(0); // must not be reached
        }));
    }

    void testDestroyUnstartedOrFinishedTaskIsFine()
    {
        BOOST_TEST(exits_cleanly([] {
            auto body = []() -> quitter<void> { co_return; };
            { auto t = body(); }               // never started
            int n = 0;
            test_executor ex(n);
            await_context env{executor_ref(ex), {}, nullptr};
            auto t = body();
            lend(t, env);                     // runs to final_suspend
            return t.handle().done();
        }));                                   // finished frame destroyed
    }
#endif

    // A quitter in the middle of a chain comes down with it when the
    // leaf below it is torn down.
    void testTeardownThroughQuitter()
    {
        alloc_counts st;
        int leaf_down = 0, mid_down = 0, root_down = 0;
        parked slot;
        {
            auto leaf = [](parked* s, destroy_count_t)
                -> task<void>
            {
                co_await park_awaitable{s};
            };
            auto mid = [&](destroy_count_t) -> quitter<void>
            {
                co_await leaf(&slot, destroy_count_t(leaf_down));
            };
            auto root = [&](destroy_count_t) -> task<void>
            {
                co_await mid(destroy_count_t(mid_down));
            };
            int n = 0;
            test_executor ex(n);
            run_async(ex, counting_allocator<std::byte>(st))(
                root(destroy_count_t(root_down)));
            BOOST_TEST(slot);
            capy::abandon(slot);
        }
        BOOST_TEST_EQ(leaf_down, 1);
        BOOST_TEST_EQ(mid_down, 1);
        BOOST_TEST_EQ(root_down, 1);
        BOOST_TEST(st.balanced());
    }

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
        // When stop is already requested, run_async never starts the
        // body. Body locals are never constructed, so dtor_count stays
        // 0. A stopped quitter<> reaches the error handler.
        int dispatch_count = 0;
        test_executor ex(dispatch_count);
        std::stop_source source;
        source.request_stop();

        int dtor_count = 0;
        bool got_stopped = false;

        run_async(ex, source.get_token(),
            []() { BOOST_TEST(false); },
            [&](std::exception_ptr ep) { got_stopped = is_stop(ep); })(
                quitter_with_raii(dtor_count));

        BOOST_TEST(got_stopped);
        // Body never started, so no destructors ran
        BOOST_TEST_EQ(dtor_count, 0);
    }

    //----------------------------------------------------------
    // 5. Stop during I/O
    //----------------------------------------------------------

    // stop_only_awaitable as the tests below need it.
    //
    // A stop that stop_only_awaitable sees in await_suspend makes it
    // return the awaiting coroutine: a synchronous completion, whose
    // result quitter inspects. So await_resume reports that stop as
    // canceled, as capy's own awaitables do, and quitter is abandoned
    // there, just as for a stop that arrives while suspended.
    //
    // `registered`, when given, is counted down once the awaitable has
    // registered for the stop, so a stop the test requests after
    // waiting on it always finds the frame suspended.
    struct parked_until_stop : stop_only_awaitable
    {
        std::latch* registered;
        bool canceled = false;

        explicit parked_until_stop(
            std::latch* r = nullptr) noexcept
            : registered(r)
        {
        }

        std::coroutine_handle<>
        await_suspend(
            std::coroutine_handle<> h, await_context const* env)
        {
            // Copied out: once the latch lets the stop in, this
            // frame may be resumed and destroyed before
            // await_suspend returns.
            std::latch* r = registered;
            auto next = stop_only_awaitable::await_suspend(h, env);
            if(next == h)
                canceled = true;
            if(r)
                r->count_down();
            return next;
        }

        results<> await_resume() noexcept
        {
            if(canceled)
                return {make_error_code(error::canceled)};
            return {{}};
        }
    };

    void
    testStopDuringIO()
    {
        std::atomic<int> state = 0;
        std::latch suspended(1);

        int dtor_count = 0;

        auto q = [&]() -> quitter<>
        {
            raii_counter guard(dtor_count);
            state = 1;
            co_await parked_until_stop{&suspended};
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
            suspended.wait();
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
        std::latch& suspended)
    {
        raii_counter guard(dtor_count);
        co_await parked_until_stop{&suspended};
        reached_end = true;
    }

    static quitter<>
    middle_quitter(
        int& dtor_count,
        bool& reached_end,
        std::latch& suspended)
    {
        raii_counter guard(dtor_count);
        co_await inner_quitter(dtor_count, reached_end, suspended);
        reached_end = true;
    }

    static quitter<>
    outer_quitter(
        int& dtor_count,
        bool& reached_end,
        std::latch& suspended)
    {
        raii_counter guard(dtor_count);
        co_await middle_quitter(dtor_count, reached_end, suspended);
        reached_end = true;
    }

    void
    testStopPropagationChain()
    {
        std::latch suspended(1);
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
            suspended.wait();
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
        std::latch suspended(1);
        int dtor_count = 0;
        bool reached_end = false;

        auto q = [&]() -> quitter<>
        {
            raii_counter g1(dtor_count);
            raii_counter g2(dtor_count);
            raii_counter g3(dtor_count);
            co_await parked_until_stop{&suspended};
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
            suspended.wait();
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
        std::latch& sem)
    {
        progress = 1;
        co_await yield_awaitable{};
        progress = 2;
        co_await parked_until_stop{&sem};
        // Should not reach here
        progress = 3;
        co_return 0;
    }

    void
    testMultipleCoAwait()
    {
        std::atomic<int> progress{0};
        std::latch sem(1);

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
            sem.wait();
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
        await_context env{executor_ref(ex), {}, nullptr};

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

    // A ContextAwaitable whose await_suspend returns bool, to drive the
    // non-handle branch of the quitter transform_awaiter.
    struct bool_resume_awaitable
    {
        bool await_ready() const noexcept { return false; }
        bool await_suspend(std::coroutine_handle<>, await_context const*) noexcept
        {
            return false;  // do not suspend
        }
        int await_resume() noexcept { return 7; }
    };

    static quitter<int>
    co_awaits_bool_suspend()
    {
        co_return co_await bool_resume_awaitable{};
    }

    void
    testBoolSuspendAwaitable()
    {
        int result = 0;
        test::run_blocking([&](int v) { result = v; })(
            co_awaits_bool_suspend());
        BOOST_TEST_EQ(result, 7);
    }

    // A ContextAwaitable whose bool await_suspend really suspends,
    // parking the awaiting coroutine for the test to resume.
    struct bool_park_awaitable
    {
        std::coroutine_handle<>& parked;
        bool& resumed;

        bool await_ready() const noexcept { return false; }
        bool await_suspend(
            std::coroutine_handle<> h, await_context const*) noexcept
        {
            parked = h;
            return true;
        }
        void await_resume() noexcept { resumed = true; }
    };

    static quitter<>
    co_awaits_bool_park(
        std::coroutine_handle<>& parked, bool& resumed)
    {
        co_await bool_park_awaitable{parked, resumed};
    }

    // A stop that arrives while the wrapped awaitable is suspended
    // takes effect at the resumption, as for a handle await_suspend.
    void
    testBoolSuspendThenStop()
    {
        int dispatch_count = 0;
        test_executor ex(dispatch_count);

        std::stop_source source;
        std::coroutine_handle<> parked;
        bool resumed = false;
        bool completed = false;
        int stopped = 0;

        run_async(ex, source.get_token(),
            [&]() { completed = true; },
            [&](std::exception_ptr ep) { stopped += is_stop(ep); })(
                co_awaits_bool_park(parked, resumed));

        BOOST_TEST(parked);
        source.request_stop();
        parked.resume();

        // A stopped quitter<> reaches the error handler.
        BOOST_TEST(!resumed);
        BOOST_TEST(!completed);
        BOOST_TEST_EQ(stopped, 1);
    }

    static quitter<>
    plain_void_quitter()
    {
        co_return;
    }

    static quitter<int>
    co_awaits_void_quitter()
    {
        co_await plain_void_quitter();
        co_return 5;
    }

    void
    testCoAwaitVoidQuitter()
    {
        // Co-await a normally-completing void quitter: exercises the
        // void path of await_resume.
        int result = 0;
        test::run_blocking([&](int v) { result = v; })(
            co_awaits_void_quitter());
        BOOST_TEST_EQ(result, 5);
    }

    static quitter<int>
    co_awaits_throwing_quitter()
    {
        co_await quitter_throws_int();
        co_return 0;
    }

    void
    testCoAwaitThrowingQuitter()
    {
        // Co-await a throwing quitter: exercises the rethrow path of
        // await_resume.
        BOOST_TEST_THROWS(
            test::run_blocking()(co_awaits_throwing_quitter()),
            test_exception);
    }

    //----------------------------------------------------------
    // 7. Stop propagation with when_all
    //
    //    Two quitter<results<size_t>> children inside when_all.
    //    Both block on parked_until_stop.  when_all creates a
    //    child stop_source; when the parent stop fires, when_all
    //    propagates it.  Each quitter child is abandoned by its
    //    gate, so it never reaches the co_return, and delivers
    //    canceled to its branch.  Verify both stop and when_all
    //    completes.
    //----------------------------------------------------------

    static quitter<results<std::size_t>>
    quitter_pending_size(
        bool& reached_co_return,
        std::latch* registered = nullptr)
    {
        co_await parked_until_stop{registered};
        // A stopped quitter is abandoned, so we never reach here.
        reached_co_return = true;
        co_return results<std::size_t>{
            make_error_code(error::canceled), 0};
    }

    void
    testWhenAllWithStop()
    {
        thread_pool pool(2);
        std::latch done(1);
        std::latch registered(2);
        std::stop_source source;

        bool child1_returned = false;
        bool child2_returned = false;

        auto outer = [&]() -> task<>
        {
            auto result = co_await when_all(
                quitter_pending_size(child1_returned, &registered),
                quitter_pending_size(child2_returned, &registered));
            (void)result;
        };

        run_async(pool.get_executor(), source.get_token(),
            [&]() { done.count_down(); },
            [&](std::exception_ptr) { done.count_down(); })(
                outer());

        registered.wait();
        source.request_stop();

        done.wait();
        // Both quitter children were abandoned before reaching
        // co_return.
        BOOST_TEST(!child1_returned);
        BOOST_TEST(!child2_returned);
    }

    //----------------------------------------------------------
    // 8. Stop propagation with when_any
    //
    //    Two quitter children.  One succeeds once the other is
    //    waiting, and when_any stops the sibling.  The sibling
    //    quitter is abandoned and delivers canceled.
    //----------------------------------------------------------

    // Succeeds once the sibling has registered for the stop, so the
    // stop always finds the sibling suspended.
    static quitter<results<std::size_t>>
    quitter_success_after_registered(
        std::size_t n, std::latch& registered)
    {
        registered.wait();
        co_return results<std::size_t>{std::error_code(), n};
    }

    static quitter<results<std::size_t>>
    quitter_pending_size_registered(
        bool& reached_co_return, std::latch& registered)
    {
        co_await parked_until_stop{&registered};
        // A stopped quitter is abandoned, so we never reach here.
        reached_co_return = true;
        co_return results<std::size_t>{
            make_error_code(error::canceled), 0};
    }

    void
    testWhenAnyWithStop()
    {
        // One child succeeds once its sibling waits.  when_any
        // stops the pending sibling quitter, which is abandoned
        // and never reaches co_return.
        {
            thread_pool pool(2);
            std::latch done(1);
            std::latch registered(1);
            bool sibling_returned = false;

            // The sibling comes first, so the winner never blocks the
            // thread that would start it.
            auto outer = [&]() -> task<>
            {
                auto result = co_await when_any(
                    quitter_pending_size_registered(
                        sibling_returned, registered),
                    quitter_success_after_registered(42, registered));
                // Variadic when_any returns
                // variant<error_code, size_t, size_t>.
                // Index 2 = second child won.
                BOOST_TEST(result.index() == 2);
                if(result.index() == 2)
                    BOOST_TEST_EQ(std::get<2>(result),
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
            std::latch registered(2);
            std::stop_source source;

            bool child1_returned = false;
            bool child2_returned = false;

            auto outer = [&]() -> task<>
            {
                auto result = co_await when_any(
                    quitter_pending_size(child1_returned, &registered),
                    quitter_pending_size(child2_returned, &registered));
                (void)result;
            };

            run_async(pool.get_executor(), source.get_token(),
                [&]() { done.count_down(); },
                [&](std::exception_ptr) {
                    done.count_down();
                })(outer());

            registered.wait();
            source.request_stop();

            done.wait();
            BOOST_TEST(!child1_returned);
            BOOST_TEST(!child2_returned);
        }
    }

    //----------------------------------------------------------
    // 14. Waker cancellation
    //----------------------------------------------------------

    void
    testWakerCancellation()
    {
        thread_pool pool(1);
        std::latch done(1);
        std::latch suspended(1);
        std::stop_source source;
        bool reached_end = false;

        // Never woken: an external wait that only cancellation
        // can complete.
        async_waker waker;

        auto q = [&]() -> quitter<>
        {
            suspended.count_down();
            auto [ec] = co_await waker.wait();
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
        // Should complete promptly
        BOOST_TEST(elapsed < std::chrono::seconds(1));
        // The quitter was abandoned: the body did not continue
        BOOST_TEST(!reached_end);
    }

    //----------------------------------------------------------
    // 15. Catch around a co_await whose await_ready does an atomic
    //     read-modify-write (MSVC 14.51 ARM64 release regression)
    //
    //    quitter's transform_awaiter gets the same safeguard as
    //    task's: where the symmetric-transfer workaround is on, the
    //    wrapped await_ready is asked from inside await_suspend. See
    //    testCatchAfterRmwAwaitReady in test/unit/task.cpp. Both
    //    tests below pass on every other build with or without it.
    //----------------------------------------------------------

    // An opaque call before the try; the defect needs one there.
    static BOOST_CAPY_NOINLINE void
    call_before_try(bool& marker)
    {
        marker = true;
    }

    // Parks; await_ready performs a compare-exchange that never
    // succeeds here, as async_waker's does. await_resume throws when
    // throw_on_resume is set.
    struct rmw_ready_awaitable
    {
        std::coroutine_handle<>& slot;
        std::atomic<int>& state;
        bool throw_on_resume;

        bool await_ready() noexcept
        {
            int expected = 1;
            return state.compare_exchange_strong(
                expected, 0,
                std::memory_order_acq_rel,
                std::memory_order_acquire);
        }

        std::coroutine_handle<>
        await_suspend(
            std::coroutine_handle<> h, await_context const*) noexcept
        {
            slot = h;
            return std::noop_coroutine();
        }

        void await_resume() const
        {
            if(throw_on_resume)
                throw test_exception("rmw ready");
        }
    };

    static quitter<>
    rmw_ready_quitter(
        std::coroutine_handle<>& slot,
        std::atomic<int>& state,
        bool throw_on_resume,
        bool& marker,
        bool& caught)
    {
        call_before_try(marker);
        try
        {
            co_await rmw_ready_awaitable{slot, state, throw_on_resume};
        }
        catch(...)
        {
            caught = true;
        }
    }

    // The awaitable's own await_resume throws.
    void
    testCatchAfterRmwAwaitReady()
    {
        int dispatch_count = 0;
        test_executor ex(dispatch_count);

        std::coroutine_handle<> parked;
        std::atomic<int> state{0};
        bool marker = false;
        bool caught = false;
        bool escaped = false;
        bool completed = false;

        run_async(ex,
            [&]() { completed = true; },
            [&](std::exception_ptr) { escaped = true; })(
                rmw_ready_quitter(parked, state, true, marker, caught));

        BOOST_TEST(static_cast<bool>(parked));
        BOOST_TEST(marker);
        if(parked)
            parked.resume();

        BOOST_TEST(caught);
        BOOST_TEST(!escaped);
        BOOST_TEST(completed);
    }

    // A stop request while parked: the frame is abandoned, never
    // resumed, so the catch(...) in the body cannot take the stop. A
    // stopped quitter<> reaches the error handler.
    void
    testCatchStopAfterRmwAwaitReady()
    {
        int dispatch_count = 0;
        test_executor ex(dispatch_count);

        std::stop_source source;
        std::coroutine_handle<> parked;
        std::atomic<int> state{0};
        bool marker = false;
        bool caught = false;
        bool stopped = false;
        bool completed = false;

        run_async(ex, source.get_token(),
            [&]() { completed = true; },
            [&](std::exception_ptr ep) { stopped = is_stop(ep); })(
                rmw_ready_quitter(parked, state, false, marker, caught));

        BOOST_TEST(static_cast<bool>(parked));
        BOOST_TEST(marker);
        source.request_stop();
        if(parked)
            parked.resume();

        BOOST_TEST(!caught);
        BOOST_TEST(stopped);
        BOOST_TEST(!completed);
    }

    //----------------------------------------------------------
    // 13. Echo server with shutdown
    //
    //    A quitter echo loop over a mock stream pair.
    //    The client exchanges data, then requests stop.
    //    The echo quitter is abandoned on the stop, RAII runs,
    //    and the echoed data was correct.
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
        // stop fires, the quitter is abandoned at that co_await
        // and RAII runs.  All stream access is on the
        // jthread's synchronous executor — no cross-thread
        // stream use.
        test::fuse f;
        auto [server_end, client_end] =
            test::make_stream_pair(f);

        client_end.provide("hello");

        int dtor_count = 0;
        bool reached_end = false;
        std::size_t total_echoed = 0;
        std::latch suspended(1);

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

            // Wait for shutdown, signalling once suspended
            // until the stop token fires.
            co_await parked_until_stop{&suspended};

            // Never reached: the stop abandons the quitter
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
            suspended.wait();

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
    // 16. A stop never discards what the inner await_ready acquired
    //
    //    async_mutex's await_ready takes the lock when the mutex is
    //    free. A stop pending at the co_await must leave the inner
    //    awaitable untouched, and once the inner awaitable reports
    //    ready, its await_resume must run so that what it acquired is
    //    handed over. Only a result canceled by the stop, which
    //    acquired nothing, abandons the quitter there.
    //----------------------------------------------------------

    static quitter<>
    lock_after_stop(async_mutex& mtx, std::stop_source& source)
    {
        source.request_stop();
        // Not a structured binding: GCC 15 at -O0 destroys the never
        // constructed binding when the frame is destroyed at this
        // co_await (GCC PR c++/124584).
        auto r = co_await mtx.scoped_lock();
        (void)r;
    }

    // The stop is already pending when the body reaches the co_await.
    void
    testStopPendingLeavesMutexUntouched()
    {
        int dispatch_count = 0;
        test_executor ex(dispatch_count);

        async_mutex mtx;
        std::stop_source source;
        bool completed = false;
        int stopped = 0;

        run_async(ex, source.get_token(),
            [&]() { completed = true; },
            [&](std::exception_ptr ep) { stopped += is_stop(ep); })(
                lock_after_stop(mtx, source));

        // A stopped quitter<> reaches the error handler.
        BOOST_TEST(!mtx.is_locked());
        BOOST_TEST(!completed);
        BOOST_TEST_EQ(stopped, 1);
    }

    // Reports ready, and requests the stop from inside await_ready, so
    // the stop arrives after the inner awaitable has completed.
    struct stop_while_ready_awaitable
    {
        std::stop_source& source;
        bool& resumed;

        bool await_ready() noexcept
        {
            source.request_stop();
            return true;
        }

        std::coroutine_handle<>
        await_suspend(
            std::coroutine_handle<> h, await_context const*) noexcept
        {
            return h; // LCOV_EXCL_LINE never called: await_ready is true
        }

        void await_resume() noexcept
        {
            resumed = true;
        }
    };

    static quitter<>
    ready_then_stop(std::stop_source& source, bool& resumed)
    {
        co_await stop_while_ready_awaitable{source, resumed};
    }

    void
    testReadyThenStopRunsAwaitResume()
    {
        int dispatch_count = 0;
        test_executor ex(dispatch_count);

        std::stop_source source;
        bool resumed = false;
        bool completed = false;
        int stopped = 0;

        run_async(ex, source.get_token(),
            [&]() { completed = true; },
            [&](std::exception_ptr) { ++stopped; })(
                ready_then_stop(source, resumed));

        BOOST_TEST(resumed);
        // Nothing suspends after the co_await, so the body finishes.
        BOOST_TEST(completed);
        BOOST_TEST_EQ(stopped, 0);
    }

    // Not ready, and await_suspend returns the awaiting coroutine
    // itself, after requesting the stop: a synchronous completion
    // through the returned handle, as async_waker's await_suspend does
    // when it consumes a wakeup that was latched after its await_ready.
    struct stop_then_return_self_awaitable
    {
        std::stop_source& source;
        bool& resumed;

        bool await_ready() const noexcept
        {
            return false;
        }

        std::coroutine_handle<>
        await_suspend(
            std::coroutine_handle<> h, await_context const*) noexcept
        {
            source.request_stop();
            return h;
        }

        void await_resume() noexcept
        {
            resumed = true;
        }
    };

    static quitter<>
    return_self_then_stop(std::stop_source& source, bool& resumed)
    {
        co_await stop_then_return_self_awaitable{source, resumed};
    }

    void
    testReturnSelfThenStopRunsAwaitResume()
    {
        int dispatch_count = 0;
        test_executor ex(dispatch_count);

        std::stop_source source;
        bool resumed = false;
        bool completed = false;
        int stopped = 0;

        run_async(ex, source.get_token(),
            [&]() { completed = true; },
            [&](std::exception_ptr) { ++stopped; })(
                return_self_then_stop(source, resumed));

        BOOST_TEST(resumed);
        // Nothing suspends after the co_await, so the body finishes.
        BOOST_TEST(completed);
        BOOST_TEST_EQ(stopped, 0);
    }

    // How a sync_results_awaitable completes without suspending.
    enum class completes { ready, by_false, by_handle };

    // Completes synchronously, after requesting the stop when given a
    // source, with a results holding `ec`: like async_mutex or
    // async_event when they see the stop first. It reports ready, or
    // its await_suspend returns false or the handle it was given.
    // The payload counts its destruction, as a lock_guard releases.
    template<completes How = completes::ready>
    struct sync_results_awaitable
    {
        std::stop_source* source;
        std::error_code ec;
        bool& resumed;
        int& destroyed;

        bool await_ready() noexcept
        {
            if constexpr (How != completes::ready)
            {
                return false;
            }
            else
            {
                if(source)
                    source->request_stop();
                return true;
            }
        }

        auto
        await_suspend(
            std::coroutine_handle<> h, await_context const*) noexcept
        {
            if(source)
                source->request_stop();
            if constexpr (How == completes::by_false)
                return false;
            else
                return h;
        }

        results<raii_counter> await_resume() noexcept
        {
            resumed = true;
            return {ec, raii_counter(destroyed)};
        }
    };

    struct ready_results_outcome
    {
        bool resumed = false;
        bool continued = false;
        std::error_code seen;
        int stopped = 0;
        int destroyed = 0;
    };

    template<completes How>
    static quitter<>
    ready_results_body(
        std::stop_source* source,
        std::error_code ec,
        ready_results_outcome& out)
    {
        // Not a structured binding: see lock_after_stop.
        auto r = co_await sync_results_awaitable<How>{
            source, ec, out.resumed, out.destroyed};
        out.seen = std::get<0>(r);
        out.continued = true;
    }

    template<completes How = completes::ready>
    static ready_results_outcome
    run_ready_results(bool stop, std::error_code ec)
    {
        int dispatch_count = 0;
        test_executor ex(dispatch_count);

        std::stop_source source;
        ready_results_outcome out;

        run_async(ex, source.get_token(),
            []() {},
            [&](std::exception_ptr ep) { out.stopped += is_stop(ep); })(
                ready_results_body<How>(
                    stop ? &source : nullptr, ec, out));
        return out;
    }

    // A canceled result under our stop acquired nothing, so the
    // quitter is abandoned at that co_await. A stopped quitter<>
    // reaches the error handler.
    void
    testReadyCanceledThenStopStops()
    {
        auto out = run_ready_results(
            true, make_error_code(error::canceled));
        BOOST_TEST(out.resumed);
        BOOST_TEST(!out.continued);
        BOOST_TEST_EQ(out.stopped, 1);
        // The dropped result is destroyed, not leaked.
        BOOST_TEST_EQ(out.destroyed, 1);
    }

    // The same when await_suspend completes synchronously, by
    // returning false or the handle it was given.
    void
    testSuspendCanceledThenStopStops()
    {
        auto by_false = run_ready_results<completes::by_false>(
            true, make_error_code(error::canceled));
        BOOST_TEST(by_false.resumed);
        BOOST_TEST(!by_false.continued);
        BOOST_TEST_EQ(by_false.destroyed, 1);

        auto by_handle = run_ready_results<completes::by_handle>(
            true, make_error_code(error::canceled));
        BOOST_TEST(by_handle.resumed);
        BOOST_TEST(!by_handle.continued);
        BOOST_TEST_EQ(by_handle.destroyed, 1);

        auto success = run_ready_results<completes::by_handle>(
            true, {});
        BOOST_TEST(success.continued);
        BOOST_TEST(!success.seen);
        BOOST_TEST_EQ(success.destroyed, 1);
    }

    // A successful result is handed over even with a stop pending.
    void
    testReadySuccessThenStopDelivers()
    {
        auto out = run_ready_results(true, {});
        BOOST_TEST(out.continued);
        BOOST_TEST(!out.seen);
        BOOST_TEST_EQ(out.stopped, 0);
        BOOST_TEST_EQ(out.destroyed, 1);
    }

    // A canceled result with no stop of ours is the body's to handle.
    void
    testReadyCanceledWithoutStopDelivers()
    {
        auto out = run_ready_results(
            false, make_error_code(error::canceled));
        BOOST_TEST(out.continued);
        BOOST_TEST(out.seen == cond::canceled);
        BOOST_TEST_EQ(out.stopped, 0);
        BOOST_TEST_EQ(out.destroyed, 1);
    }

    //----------------------------------------------------------
    // 17. Stop by abandonment
    //
    //    A stop ends a quitter's frames without resuming any of
    //    them. The first frame above that is not a quitter gets the
    //    stopped value: canceled for results and error_code, and a
    //    std::system_error for any other T, void included.
    //----------------------------------------------------------

    // What the parent of a quitter saw when it resumed.
    struct seen
    {
        bool resumed = false;
        bool threw = false;
        std::error_code ec;
    };

    // Awaits `q` and records what it delivered. The quitter object is
    // moved into the co_await, so its frame is gone once that
    // statement ends.
    template<class Q>
    static task<>
    await_into(Q q, seen& s)
    {
        try
        {
            auto r = co_await std::move(q);
            s.ec = std::get<0>(r);
        }
        catch(...)
        {
            s.threw = true;
        }
        s.resumed = true;
    }

    // Runs `t` on a queue until nothing is runnable, requests the
    // stop, and runs it again. Frames come from new_delete_resource,
    // so ASan sees any frame used after it was freed.
    static void
    run_then_stop(std::stop_source& ss, task<> t)
    {
        std::queue<continuation> q;
        queuing_executor ex(q);
        run_async(ex, ss.get_token(),
            std::pmr::new_delete_resource())(std::move(t));
        pump(q);
        ss.request_stop();
        pump(q);
    }

    static quitter<results<std::size_t>>
    three_yields()
    {
        co_await yield_awaitable{};
        co_await yield_awaitable{};
        co_await yield_awaitable{};
        co_return results<std::size_t>{std::error_code(), 42};
    }

    void
    testUnstoppedValue()
    {
        std::stop_source ss;
        std::error_code ec = make_error_code(error::canceled);
        std::size_t n = 0;
        auto parent = [&]() -> task<>
        {
            auto [e, v] = co_await three_yields();
            ec = e;
            n = v;
        };
        run_then_stop(ss, parent());
        BOOST_TEST(!ec);
        BOOST_TEST_EQ(n, 42u);
    }

    static quitter<results<>>
    guarded_wait(async_waker& w, int& down, bool& reached)
    {
        raii_counter g(down);
        auto [ec] = co_await w.wait();
        (void)ec;
        reached = true;
        co_return results<>{};
    }

    void
    testStopDeliversCanceled()
    {
        async_waker w;
        std::stop_source ss;
        seen s;
        int down = 0;
        bool reached = false;
        run_then_stop(ss, await_into(guarded_wait(w, down, reached), s));
        BOOST_TEST(s.resumed);
        BOOST_TEST(!s.threw);
        BOOST_TEST(s.ec == cond::canceled);
        BOOST_TEST(!reached);
    }

    void
    testFramesNotResumed()
    {
        async_waker w;
        std::stop_source ss;
        int down = 0;
        bool reached = false;
        int down_at_resume = -1;
        auto parent = [&]() -> task<>
        {
            auto [ec] = co_await guarded_wait(w, down, reached);
            (void)ec;
            down_at_resume = down;
        };
        run_then_stop(ss, parent());
        BOOST_TEST(!reached);
        BOOST_TEST_EQ(down_at_resume, 1);
    }

    static quitter<results<>>
    catch_all_wait(async_waker& w, bool& hit)
    {
        try
        {
            auto [ec] = co_await w.wait();
            (void)ec;
        }
        catch(...)
        {
            hit = true;
        }
        co_return results<>{};
    }

    void
    testCatchAllNeverRuns()
    {
        async_waker w;
        std::stop_source ss;
        seen s;
        bool hit = false;
        run_then_stop(ss, await_into(catch_all_wait(w, hit), s));
        BOOST_TEST(!hit);
        BOOST_TEST(s.ec == cond::canceled);
    }

    struct coop_state
    {
        bool task_finished = false;
        std::error_code task_ec;
        bool quitter_alive_then = false;
        int quitter_down = 0;
        bool quitter_reached = false;
    };

    static task<results<>>
    coop_task(async_waker& w, coop_state& st)
    {
        auto [ec] = co_await w.wait();
        st.task_finished = true;
        st.task_ec = ec;
        st.quitter_alive_then = st.quitter_down == 0;
        co_return results<>{ec};
    }

    static quitter<results<>>
    coop_quitter(async_waker& w, coop_state& st)
    {
        raii_counter g(st.quitter_down);
        auto [ec] = co_await coop_task(w, st);
        (void)ec;
        st.quitter_reached = true;
        co_return results<>{};
    }

    // A task below a quitter stops through its own code; the quitter
    // is destroyed only once the task has returned into it.
    void
    testTaskBelowCooperates()
    {
        async_waker w;
        std::stop_source ss;
        seen s;
        coop_state st;
        run_then_stop(ss, await_into(coop_quitter(w, st), s));
        BOOST_TEST(st.task_finished);
        BOOST_TEST(st.task_ec == cond::canceled);
        BOOST_TEST(st.quitter_alive_then);
        BOOST_TEST_EQ(st.quitter_down, 1);
        BOOST_TEST(!st.quitter_reached);
        BOOST_TEST(s.ec == cond::canceled);
    }

    static quitter<results<>>
    join_guarded_wait(async_waker& w, int& down, bool& reached)
    {
        raii_counter g(down);
        auto [ec] = co_await guarded_wait(w, down, reached);
        (void)ec;
        reached = true;
        co_return results<>{};
    }

    // A quitter awaited by a quitter joins its segment: one stop
    // destroys both frames, and the outer one never sees a value.
    void
    testQuitterJoinsQuitterSegment()
    {
        async_waker w;
        std::stop_source ss;
        seen s;
        int down = 0;
        bool reached = false;
        run_then_stop(ss, await_into(join_guarded_wait(w, down, reached), s));
        BOOST_TEST_EQ(down, 2);
        BOOST_TEST(!reached);
        BOOST_TEST(s.ec == cond::canceled);
    }

    static task<results<>>
    task_between(async_waker& w, coop_state& st)
    {
        int down = 0;
        bool reached = false;
        auto [ec] = co_await guarded_wait(w, down, reached);
        st.task_finished = true;
        st.task_ec = ec;
        st.quitter_alive_then = st.quitter_down == 0;
        co_return results<>{ec};
    }

    static quitter<results<>>
    quitter_over_task(async_waker& w, coop_state& st)
    {
        raii_counter g(st.quitter_down);
        auto [ec] = co_await task_between(w, st);
        (void)ec;
        st.quitter_reached = true;
        co_return results<>{};
    }

    // Q1 -> task -> Q2: the task gets canceled from Q2 as a value and
    // returns through its own code; only then is Q1 destroyed.
    void
    testTaskBetweenQuittersCooperates()
    {
        async_waker w;
        std::stop_source ss;
        seen s;
        coop_state st;
        run_then_stop(ss, await_into(quitter_over_task(w, st), s));
        BOOST_TEST(st.task_finished);
        BOOST_TEST(st.task_ec == cond::canceled);
        BOOST_TEST(st.quitter_alive_then);
        BOOST_TEST_EQ(st.quitter_down, 1);
        BOOST_TEST(!st.quitter_reached);
        BOOST_TEST(s.ec == cond::canceled);
    }

    static quitter<results<>>
    lock_after_stop_results(
        async_mutex& mtx, std::stop_source& ss, bool& reached)
    {
        ss.request_stop();
        // Not a structured binding: see lock_after_stop.
        auto r = co_await mtx.scoped_lock();
        (void)r;
        reached = true;
        co_return results<>{};
    }

    void
    testStopPendingAtAwaitAbandons()
    {
        async_mutex mtx;
        std::stop_source ss;
        seen s;
        bool reached = false;
        run_then_stop(ss,
            await_into(lock_after_stop_results(mtx, ss, reached), s));
        BOOST_TEST(!mtx.is_locked());
        BOOST_TEST(!reached);
        BOOST_TEST(s.ec == cond::canceled);
    }

    static quitter<results<>>
    flag_and_return(bool& started)
    {
        started = true;
        co_return results<>{};
    }

    static quitter<results<>>
    join_after_stop(std::stop_source& ss, bool& started, bool& reached)
    {
        ss.request_stop();
        auto r = co_await flag_and_return(started);
        (void)r;
        reached = true;
        co_return results<>{};
    }

    // The same for a quitter awaited by a quitter: a pending stop
    // abandons the segment before the joined quitter's body starts.
    void
    testStopPendingAtJoinAbandons()
    {
        std::stop_source ss;
        seen s;
        bool started = false;
        bool reached = false;
        run_then_stop(ss,
            await_into(join_after_stop(ss, started, reached), s));
        BOOST_TEST(!started);
        BOOST_TEST(!reached);
        BOOST_TEST(s.ec == cond::canceled);
    }

    // Completes inside await_suspend, after requesting a stop, by
    // returning the handle it was given.
    struct inline_after_stop_awaitable
    {
        std::stop_source& ss;
        bool& resumed;

        bool await_ready() const noexcept { return false; }

        std::coroutine_handle<>
        await_suspend(
            std::coroutine_handle<> h, await_context const*) noexcept
        {
            ss.request_stop();
            return h;
        }

        void await_resume() noexcept { resumed = true; }
    };

    // The same, by returning false.
    struct false_after_stop_awaitable
    {
        std::stop_source& ss;
        bool& resumed;

        bool await_ready() const noexcept { return false; }

        bool
        await_suspend(
            std::coroutine_handle<>, await_context const*) noexcept
        {
            ss.request_stop();
            return false;
        }

        void await_resume() noexcept { resumed = true; }
    };

    // Not a template: GCC 11 applies await_transform twice to a
    // non-dependent co_await in a coroutine template.
    static quitter<results<>>
    complete_then_yield(
        bool by_false, std::stop_source& ss, bool& resumed, bool& reached)
    {
        if(by_false)
            co_await false_after_stop_awaitable{ss, resumed};
        else
            co_await inline_after_stop_awaitable{ss, resumed};
        co_await yield_awaitable{};
        reached = true;
        co_return results<>{};
    }

    // A completion that does not go through the gate is handed over,
    // and the stop takes effect at the next suspension.
    void
    testInlineCompletionContinues()
    {
        std::stop_source ss;
        seen s;
        bool resumed = false;
        bool reached = false;
        run_then_stop(ss, await_into(
            complete_then_yield(false, ss, resumed, reached), s));
        BOOST_TEST(resumed);
        BOOST_TEST(!reached);
        BOOST_TEST(s.ec == cond::canceled);
    }

    void
    testBoolFalseCompletionContinues()
    {
        std::stop_source ss;
        seen s;
        bool resumed = false;
        bool reached = false;
        run_then_stop(ss, await_into(
            complete_then_yield(true, ss, resumed, reached), s));
        BOOST_TEST(resumed);
        BOOST_TEST(!reached);
        BOOST_TEST(s.ec == cond::canceled);
    }

    static quitter<results<>>
    first_statement(bool& ran)
    {
        ran = true;
        co_await yield_awaitable{};
        co_return results<>{};
    }

    // A stop already requested when the quitter is awaited: its body
    // never starts.
    void
    testStopBeforeFirstResume()
    {
        std::stop_source ss;
        ss.request_stop();
        seen s;
        bool ran = false;
        run_then_stop(ss, await_into(first_statement(ran), s));
        BOOST_TEST(!ran);
        BOOST_TEST(s.resumed);
        BOOST_TEST(s.ec == cond::canceled);
    }

    static task<results<>>
    task_wait(async_waker& w)
    {
        auto [ec] = co_await w.wait();
        co_return results<>{ec};
    }

    // The quitter branch is abandoned under its own owner, so the join
    // sees canceled from it instead of a lost branch.
    void
    testWhenAllBranchStopNotLost()
    {
        async_waker w1;
        async_waker w2;
        std::stop_source ss;
        seen s;
        int down = 0;
        bool reached = false;
        auto parent = [&]() -> task<>
        {
            auto r = co_await when_all(
                guarded_wait(w1, down, reached), task_wait(w2));
            s.ec = std::get<0>(r);
            s.resumed = true;
        };
        run_then_stop(ss, parent());
        BOOST_TEST(s.resumed);
        BOOST_TEST(s.ec == cond::canceled);
        BOOST_TEST_EQ(down, 1);
        BOOST_TEST(!reached);
    }

    // The stop's unwind is posted to an executor that has closed: it
    // ends as teardown, with no handler.
    void
    testOwnerDestroyedByClosedExecutorIsTeardown()
    {
        std::queue<continuation> q;
        bool closed = false;
        closing_executor ex{&q, &closed};
        async_waker w;
        std::stop_source ss;
        seen s;
        int down = 0;
        bool reached = false;
        bool value = false;
        bool error = false;
        run_async(ex, ss.get_token(), std::pmr::new_delete_resource(),
            [&]() { value = true; },
            [&](std::exception_ptr) { error = true; })(
                await_into(guarded_wait(w, down, reached), s));
        ss.request_stop();
        closed = true;
        pump(q);
        BOOST_TEST(!value);
        BOOST_TEST(!error);
        BOOST_TEST(!s.resumed);
        BOOST_TEST_EQ(down, 1);
        BOOST_TEST(!reached);
    }

    static quitter<results<>>
    counted_wait(async_waker& w, destroy_count_t)
    {
        auto [ec] = co_await w.wait();
        co_return results<>{ec};
    }

    void
    testNoDoubleDestroyOnStop()
    {
        async_waker w;
        std::stop_source ss;
        seen s;
        int down = 0;
        run_then_stop(ss,
            await_into(counted_wait(w, destroy_count_t(down)), s));
        BOOST_TEST_EQ(down, 1);
        BOOST_TEST(s.ec == cond::canceled);
    }

    // async_waker::wait(), setting `armed` once the waiter has
    // registered, so a wake or stop that follows finds it suspended.
    struct armed_wait
    {
        async_waker::wait_awaiter a;
        std::atomic<bool>* armed;

        bool await_ready() noexcept { return a.await_ready(); }

        std::coroutine_handle<>
        await_suspend(
            std::coroutine_handle<> h, await_context const* env) noexcept
        {
            auto next = a.await_suspend(h, env);
            armed->store(true, std::memory_order_release);
            return next;
        }

        results<> await_resume() noexcept { return a.await_resume(); }
    };

    static quitter<results<>>
    counted_reaching_wait(
        async_waker& w,
        std::atomic<bool>& armed,
        destroy_count_t,
        bool& reached)
    {
        auto [ec] = co_await armed_wait{w.wait(), &armed};
        (void)ec;
        reached = true;
        co_return results<>{};
    }

    // A wake on another thread races the stop: either outcome is
    // delivered exactly once, and nothing leaks. Both threads start
    // once the waiter has registered, and one of them is held back by
    // a varying delay, so both outcomes occur.
    void
    testStopRacesCompletion()
    {
        std::atomic<int> total_handlers{0};
        int woken = 0;
        int canceled = 0;
        {
        thread_pool pool(2);
        for(int i = 0; i < 10000; ++i)
        {
            async_waker w;
            std::stop_source ss;
            std::atomic<bool> armed{false};
            std::atomic<bool> go{false};
            // A mutex, not a latch: TSan does not see the latch order
            // the handler's writes before a wait that returns at once.
            std::mutex m;
            std::condition_variable cv;
            bool done = false;
            seen s;
            // Counts the frame's destruction even when a stop that
            // lands first keeps the body from starting.
            int down = 0;
            bool reached = false;
            int handlers = 0;
            // The parent finished before its handler ran.
            bool finished_first = false;
            auto handled = [&]
            {
                finished_first = s.resumed;
                ++handlers;
                ++total_handlers;
                std::lock_guard<std::mutex> lock(m);
                done = true;
                cv.notify_one();
            };
            run_async(pool.get_executor(), ss.get_token(),
                std::pmr::new_delete_resource(),
                [&]() { handled(); },
                [&](std::exception_ptr) { handled(); })(
                    await_into(counted_reaching_wait(
                        w, armed, destroy_count_t(down), reached), s));
            // Without the delay the stop nearly always wins, since the
            // woken path also needs the gate to run before the stop.
            auto const delay = std::chrono::microseconds(i % 64);
            bool const hold_stop = i % 2 == 0;
            auto start = [&](bool held)
            {
                while(!go.load(std::memory_order_acquire))
                    std::this_thread::yield();
                if(!held)
                    return;
                auto const until = std::chrono::steady_clock::now() + delay;
                while(std::chrono::steady_clock::now() < until)
                    ;
            };
            std::thread waker([&]
            {
                start(!hold_stop);
                w.wake();
            });
            std::thread stopper([&]
            {
                start(hold_stop);
                ss.request_stop();
            });
            while(!armed.load(std::memory_order_acquire))
                std::this_thread::yield();
            go.store(true, std::memory_order_release);
            waker.join();
            stopper.join();
            {
                std::unique_lock<std::mutex> lock(m);
                cv.wait(lock, [&] { return done; });
            }
            BOOST_TEST_EQ(handlers, 1);
            BOOST_TEST(finished_first);
            BOOST_TEST_EQ(down, 1);
            BOOST_TEST(!s.threw);
            if(s.ec == cond::canceled)
            {
                ++canceled;
                BOOST_TEST(!reached);
            }
            else
            {
                ++woken;
                BOOST_TEST(!s.ec);
                BOOST_TEST(reached);
            }
        }
        }
        BOOST_TEST_EQ(total_handlers.load(), 10000);
        BOOST_TEST_EQ(woken + canceled, 10000);
        BOOST_TEST(woken > 0);
        BOOST_TEST(canceled > 0);
    }

    // The race above, made deterministic: the wake has queued the gate,
    // and the stop arrives before the gate runs. The gate abandons the
    // segment, so the body is not resumed.
    void
    testStopAfterWakeBeforeGate()
    {
        std::queue<continuation> q;
        queuing_executor ex(q);
        async_waker w;
        std::stop_source ss;
        seen s;
        int down = 0;
        bool reached = false;
        run_async(ex, ss.get_token(), std::pmr::new_delete_resource())(
            await_into(guarded_wait(w, down, reached), s));
        pump(q);
        w.wake();
        BOOST_TEST(!q.empty());
        ss.request_stop();
        pump(q);
        BOOST_TEST(s.resumed);
        BOOST_TEST(s.ec == cond::canceled);
        BOOST_TEST(!reached);
        BOOST_TEST_EQ(down, 1);
    }

    static quitter<results<>>
    guarded_lock(async_mutex& mtx, bool& reached)
    {
        // Not a structured binding: see lock_after_stop.
        auto r = co_await mtx.scoped_lock();
        (void)r;
        reached = true;
        co_return results<>{};
    }

    // The same for a lock: unlock() hands it to the waiter and queues
    // the gate, and the stop arrives before the gate runs. The lock the
    // abandoned waiter was handed is released, not leaked.
    void
    testStopAfterGrantBeforeGate()
    {
        std::queue<continuation> q;
        queuing_executor ex(q);
        async_mutex mtx;
        std::stop_source ss;
        seen s;
        bool reached = false;
        auto holder = [&]() -> task<>
        {
            auto r = co_await mtx.lock();
            (void)r;
        };
        run_async(ex)(holder());
        pump(q);
        BOOST_TEST(mtx.is_locked());
        run_async(ex, ss.get_token(), std::pmr::new_delete_resource())(
            await_into(guarded_lock(mtx, reached), s));
        pump(q);
        mtx.unlock();
        BOOST_TEST(!q.empty());
        ss.request_stop();
        pump(q);
        BOOST_TEST(s.resumed);
        BOOST_TEST(s.ec == cond::canceled);
        BOOST_TEST(!reached);
        BOOST_TEST(!mtx.is_locked());
    }

    static quitter<results<>>
    deep_chain(int depth, int& down, async_waker& w)
    {
        raii_counter g(down);
        if(depth == 1)
        {
            auto [ec] = co_await w.wait();
            (void)ec;
            co_return results<>{};
        }
        auto [ec] = co_await deep_chain(depth - 1, down, w);
        (void)ec;
        co_return results<>{};
    }

    void
    testDeepQuitterChainUnwinds()
    {
        async_waker w;
        std::stop_source ss;
        seen s;
        int down = 0;
        // The unwind recurses once per frame. A depth of 1000 crashed
        // the MSVC 19.51 x64 Debug build, whose main thread has 1 MB.
        constexpr int depth = 100;
        run_then_stop(ss, await_into(deep_chain(depth, down, w), s));
        BOOST_TEST_EQ(down, depth);
        BOOST_TEST(s.ec == cond::canceled);
    }

    // A quitter's segment frame does not come from the chain's frame
    // allocator, which may be gone by the time the segment is freed.
    void
    testSegmentNotOnChainAllocator()
    {
        struct counting_resource : std::pmr::memory_resource
        {
            std::size_t allocations = 0;

            void* do_allocate(std::size_t n, std::size_t a) override
            {
                ++allocations;
                return std::pmr::new_delete_resource()->allocate(n, a);
            }

            void do_deallocate(
                void* p, std::size_t n, std::size_t a) override
            {
                std::pmr::new_delete_resource()->deallocate(p, n, a);
            }

            bool do_is_equal(
                std::pmr::memory_resource const& o) const noexcept override
            {
                return this == &o;
            }
        };

        auto count = [](auto make)
        {
            counting_resource mr;
            std::queue<continuation> q;
            queuing_executor ex(q);
            seen s;
            run_async(ex, &mr)(await_into(make(), s));
            pump(q);
            BOOST_TEST(s.resumed);
            return mr.allocations;
        };
        auto via_task = count([]() -> task<results<>>
        {
            co_await yield_awaitable{};
            co_return results<>{};
        });
        auto via_quitter = count([]() -> quitter<results<>>
        {
            co_await yield_awaitable{};
            co_return results<>{};
        });
        BOOST_TEST_EQ(via_quitter, via_task);
    }

    //----------------------------------------------------------
    // 18. What a stopped quitter delivers, by T
    //----------------------------------------------------------

    static quitter<results<int, std::string>>
    stopped_payload(async_waker& w)
    {
        auto [ec] = co_await w.wait();
        (void)ec;
        co_return results<int, std::string>{std::error_code(), 7, "x"};
    }

    void
    testStoppedResultsDelivered()
    {
        async_waker w;
        std::stop_source ss;
        std::error_code ec;
        int i = -1;
        std::string str = "unset";
        auto parent = [&]() -> task<>
        {
            auto [e, v, t] = co_await stopped_payload(w);
            ec = e;
            i = v;
            str = t;
        };
        run_then_stop(ss, parent());
        BOOST_TEST(ec == cond::canceled);
        BOOST_TEST_EQ(i, 0);
        BOOST_TEST(str.empty());
    }

    // Returns success even after the waker's canceled, so only the
    // stopped value can make the parent see canceled.
    static quitter<std::error_code>
    stopped_ec(async_waker& w, bool& reached)
    {
        auto [ec] = co_await w.wait();
        (void)ec;
        reached = true;
        co_return std::error_code();
    }

    void
    testStoppedErrorCodeDelivered()
    {
        async_waker w;
        std::stop_source ss;
        std::error_code ec;
        bool reached = false;
        auto parent = [&]() -> task<>
        {
            ec = co_await stopped_ec(w, reached);
        };
        run_then_stop(ss, parent());
        BOOST_TEST(ec == cond::canceled);
        BOOST_TEST(!reached);
    }

    static quitter<>
    stopped_void(async_waker& w, bool& reached)
    {
        auto [ec] = co_await w.wait();
        (void)ec;
        reached = true;
    }

    // A void return would read as success, so a stopped quitter<>
    // throws as a plain T does.
    void
    testStoppedVoidThrows()
    {
        async_waker w;
        std::stop_source ss;
        bool reached = false;
        bool after = false;
        bool caught = false;
        auto parent = [&]() -> task<>
        {
            try
            {
                co_await stopped_void(w, reached);
                after = true;
            }
            catch(std::system_error const& e)
            {
                caught = e.code() == cond::canceled;
            }
        };
        run_then_stop(ss, parent());
        BOOST_TEST(caught);
        BOOST_TEST(!after);
        BOOST_TEST(!reached);
    }

    static quitter<int>
    stopped_int(async_waker& w)
    {
        auto [ec] = co_await w.wait();
        (void)ec;
        co_return 1;
    }

    void
    testStoppedPlainThrows()
    {
        async_waker w;
        std::stop_source ss;
        bool caught = false;
        auto parent = [&]() -> task<>
        {
            try
            {
                (void)co_await stopped_int(w);
            }
            catch(std::system_error const& e)
            {
                caught = e.code() == cond::canceled;
            }
        };
        run_then_stop(ss, parent());
        BOOST_TEST(caught);
    }

    //----------------------------------------------------------
    // 19. Launchers that drive a quitter directly
    //----------------------------------------------------------

    // Starts `q` with run_async and the given handlers, then stops it.
    template<class Q, class... H>
    static void
    run_async_then_stop(std::stop_source& ss, Q q, H... h)
    {
        std::queue<continuation> queue;
        queuing_executor ex(queue);
        run_async(ex, ss.get_token(), h...)(std::move(q));
        pump(queue);
        ss.request_stop();
        pump(queue);
    }

    void
    testRunAsyncStoppedResults()
    {
        async_waker w;
        std::stop_source ss;
        int down = 0;
        bool reached = false;
        std::error_code ec;
        int values = 0;
        int errors = 0;
        run_async_then_stop(ss, guarded_wait(w, down, reached),
            [&](results<> r) { ++values; ec = std::get<0>(r); },
            [&](std::exception_ptr) { ++errors; });
        BOOST_TEST_EQ(values, 1);
        BOOST_TEST_EQ(errors, 0);
        BOOST_TEST(ec == cond::canceled);
        BOOST_TEST_EQ(down, 1);
        BOOST_TEST(!reached);
    }

    void
    testRunAsyncStoppedResultsPayload()
    {
        async_waker w;
        std::stop_source ss;
        std::error_code ec;
        int i = -1;
        std::string str = "unset";
        int errors = 0;
        run_async_then_stop(ss, stopped_payload(w),
            [&](results<int, std::string> r)
            {
                ec = std::get<0>(r);
                i = std::get<1>(r);
                str = std::get<2>(r);
            },
            [&](std::exception_ptr) { ++errors; });
        BOOST_TEST(ec == cond::canceled);
        BOOST_TEST_EQ(i, 0);
        BOOST_TEST(str.empty());
        BOOST_TEST_EQ(errors, 0);
    }

    void
    testRunAsyncStoppedVoidCallsErrorHandler()
    {
        async_waker w;
        std::stop_source ss;
        bool reached = false;
        int values = 0;
        int stops = 0;
        run_async_then_stop(ss, stopped_void(w, reached),
            [&]() { ++values; },
            [&](std::exception_ptr ep) { stops += is_stop(ep); });
        BOOST_TEST_EQ(values, 0);
        BOOST_TEST_EQ(stops, 1);
        BOOST_TEST(!reached);
    }

    void
    testRunAsyncStoppedPlainCallsErrorHandler()
    {
        async_waker w;
        std::stop_source ss;
        int values = 0;
        bool canceled = false;
        run_async_then_stop(ss, stopped_int(w),
            [&](int) { ++values; },
            [&](std::exception_ptr ep)
            {
                try
                {
                    std::rethrow_exception(ep);
                }
                catch(std::system_error const& e)
                {
                    canceled = e.code() == cond::canceled;
                }
            });
        BOOST_TEST_EQ(values, 0);
        BOOST_TEST(canceled);
    }

    static task<>
    throws_code(std::error_code ec)
    {
        throw std::system_error(ec);
        co_return;
    }

    // With no error handler, a stopped plain-T quitter calls no
    // handler and does not terminate. A value handler that does not
    // take an exception_ptr is not called either; one that does
    // receives the std::system_error.
    void
    testDefaultHandlerDiscardsStop()
    {
        async_waker w;
        std::stop_source ss;
        run_async_then_stop(ss, stopped_int(w));

        async_waker w2;
        std::stop_source ss2;
        int values = 0;
        run_async_then_stop(ss2, stopped_int(w2),
            [&](int) { ++values; });
        BOOST_TEST_EQ(values, 0);

        async_waker w3;
        std::stop_source ss3;
        int errors = 0;
        run_async_then_stop(ss3, stopped_int(w3),
            [&](auto&& v)
            {
                if constexpr(std::is_same_v<
                    std::decay_t<decltype(v)>, std::exception_ptr>)
                    ++errors;
            });
        BOOST_TEST_EQ(errors, 1);
    }

    // A quitter's stop that escapes a task is dropped by the default
    // handler, as on develop: a task that awaits quitter<> workers
    // and does not catch the stop must not terminate. The task is
    // unwound by the stop.
    void
    testDefaultHandlerDiscardsStopEscapingTask()
    {
        // The task awaits the quitter.
        {
            async_waker w;
            std::stop_source ss;
            bool reached = false;
            bool after = false;
            auto parent = [&]() -> task<>
            {
                co_await stopped_void(w, reached);
                after = true;
            };
            run_then_stop(ss, parent());
            BOOST_TEST(!reached);
            BOOST_TEST(!after);
        }

        // The task awaits run(ex)(q).
        {
            std::queue<continuation> queue;
            queuing_executor ex(queue);
            async_waker w;
            std::stop_source ss;
            bool reached = false;
            bool after = false;
            auto parent = [&]() -> task<>
            {
                co_await capy::run(ex)(stopped_void(w, reached));
                after = true;
            };
            run_async(ex, ss.get_token())(parent());
            pump(queue);
            ss.request_stop();
            pump(queue);
            BOOST_TEST(!reached);
            BOOST_TEST(!after);
        }
    }

#if !defined(_WIN32)
    // Only a quitter's stop is discarded: canceled thrown by a task is
    // an unhandled exception like any other, whatever its category.
    void
    testDefaultHandlerTerminatesOnOtherErrors()
    {
        BOOST_TEST(terminates([] {
            int n = 0;
            test_executor ex(n);
            run_async(ex)(throws_code(
                std::make_error_code(std::errc::io_error)));
        }));
        BOOST_TEST(terminates([] {
            int n = 0;
            test_executor ex(n);
            run_async(ex)(throws_code(make_error_code(error::canceled)));
        }));
        BOOST_TEST(terminates([] {
            int n = 0;
            test_executor ex(n);
            run_async(ex)(throws_code(
                std::make_error_code(std::errc::operation_canceled)));
        }));
    }
#endif

    void
    testUserCanceledReachesErrorHandler()
    {
        int n = 0;
        test_executor ex(n);
        bool canceled = false;
        run_async(ex,
            [] {},
            [&](std::exception_ptr ep)
            {
                try
                {
                    std::rethrow_exception(ep);
                }
                catch(std::system_error const& e)
                {
                    canceled = e.code() == cond::canceled;
                }
            })(throws_code(make_error_code(error::canceled)));
        BOOST_TEST(canceled);
    }

    // Q1 -> run(st2)(Q2): stopping st2 ends Q2 only. Q1 gets canceled
    // as a value and keeps running.
    void
    testRunNoExecutorStopScoped()
    {
        async_waker w;
        std::stop_source outer;
        std::stop_source inner;
        int down = 0;
        bool reached = false;
        int q1_down = 0;
        std::error_code inner_ec;
        bool q1_finished = false;
        seen s;
        auto q1 = [&]() -> quitter<results<>>
        {
            raii_counter g(q1_down);
            auto [ec] = co_await capy::run(inner.get_token())(
                guarded_wait(w, down, reached));
            inner_ec = ec;
            q1_finished = true;
            co_return results<>{};
        };
        std::queue<continuation> queue;
        queuing_executor ex(queue);
        run_async(ex, outer.get_token())(await_into(q1(), s));
        pump(queue);
        inner.request_stop();
        pump(queue);
        BOOST_TEST(inner_ec == cond::canceled);
        BOOST_TEST(q1_finished);
        BOOST_TEST_EQ(down, 1);
        BOOST_TEST(!reached);
        BOOST_TEST_EQ(q1_down, 1);
        BOOST_TEST(s.resumed);
        BOOST_TEST(!s.ec);
    }

    static quitter<results<>>
    guarded_park(parked& slot, int& down)
    {
        raii_counter g(down);
        co_await park_awaitable{&slot};
        co_return results<>{};
    }

    // A teardown with no stop requested unwinds silently, through a
    // segment the quitter opened and through one it was launched in.
    void
    testTeardownStaysSilent()
    {
        int values = 0;
        int errors = 0;
        auto value = [&](auto&&...) { ++values; };
        auto error = [&](std::exception_ptr) { ++errors; };

        // Opened by the quitter's await_suspend.
        {
            parked slot;
            int down = 0;
            seen s;
            int n = 0;
            test_executor ex(n);
            run_async(ex, value, error)(
                await_into(guarded_park(slot, down), s));
            BOOST_TEST(slot);
            capy::abandon(slot);
            BOOST_TEST_EQ(down, 1);
            BOOST_TEST(!s.resumed);
        }

        // Launched directly.
        {
            parked slot;
            int down = 0;
            int n = 0;
            test_executor ex(n);
            run_async(ex, value, error)(guarded_park(slot, down));
            BOOST_TEST(slot);
            capy::abandon(slot);
            BOOST_TEST_EQ(down, 1);
        }

        BOOST_TEST_EQ(values, 0);
        BOOST_TEST_EQ(errors, 0);
    }

    // run(ex)(q) for a plain T or void throws the stop to its awaiting
    // parent.
    template<class Q>
    static void
    run_stopped_throws_to_parent(Q (*make)(async_waker&, bool&))
    {
        std::queue<continuation> queue;
        queuing_executor ex(queue);
        async_waker w;
        std::stop_source ss;
        bool reached = false;
        bool caught = false;
        bool after = false;
        auto parent = [&]() -> task<>
        {
            try
            {
                (void)co_await capy::run(ex)(make(w, reached));
            }
            catch(std::system_error const& e)
            {
                caught = e.code() == cond::canceled;
            }
            after = true;
        };
        run_async(ex, ss.get_token())(parent());
        pump(queue);
        ss.request_stop();
        pump(queue);
        BOOST_TEST(caught);
        BOOST_TEST(after);
        BOOST_TEST(!reached);
    }

    static quitter<int>
    stopped_int_reaching(async_waker& w, bool& reached)
    {
        auto [ec] = co_await w.wait();
        (void)ec;
        reached = true;
        co_return 1;
    }

    void
    testRunStoppedPlainThrowsToParent()
    {
        run_stopped_throws_to_parent(&stopped_int_reaching);
        run_stopped_throws_to_parent(&stopped_void);
    }

    // run(ex)(q) with a stop already requested: the body never starts.
    void
    testRunStopBeforeStart()
    {
        std::queue<continuation> queue;
        queuing_executor ex(queue);
        std::stop_source ss;
        ss.request_stop();
        bool ran = false;
        seen s;
        auto parent = [&]() -> task<>
        {
            auto [ec] = co_await capy::run(ex)(first_statement(ran));
            s.ec = ec;
            s.resumed = true;
        };
        run_async(ex, ss.get_token())(parent());
        pump(queue);
        BOOST_TEST(!ran);
        BOOST_TEST(s.resumed);
        BOOST_TEST(s.ec == cond::canceled);
    }

    // run(st)(q) with st already stopped: the body never starts, and
    // the awaiter gets canceled at once, from a task and from a quitter.
    void
    testRunTokenStopBeforeStart()
    {
        std::queue<continuation> queue;
        queuing_executor ex(queue);
        std::stop_source ss;
        ss.request_stop();
        bool ran = false;
        seen s;
        auto from_task = [&]() -> task<>
        {
            auto [ec] = co_await capy::run(ss.get_token())(
                first_statement(ran));
            s.ec = ec;
            s.resumed = true;
        };
        run_async(ex)(from_task());
        pump(queue);
        BOOST_TEST(!ran);
        BOOST_TEST(s.resumed);
        BOOST_TEST(s.ec == cond::canceled);

        // The awaiting quitter's own token is not stopped, so it
        // receives canceled as a value and keeps running.
        seen inner;
        auto from_quitter = [&]() -> quitter<results<>>
        {
            auto [ec] = co_await capy::run(ss.get_token())(
                first_statement(ran));
            inner.ec = ec;
            inner.resumed = true;
            co_return results<>{};
        };
        s = {};
        run_async(ex)(await_into(from_quitter(), s));
        pump(queue);
        BOOST_TEST(!ran);
        BOOST_TEST(inner.resumed);
        BOOST_TEST(inner.ec == cond::canceled);
        BOOST_TEST(s.resumed);
        BOOST_TEST(!s.ec);
    }

    // run_async with a stop requested after the launch has posted the
    // quitter, before it runs: the body never starts.
    void
    testRunAsyncStopAfterLaunch()
    {
        std::queue<continuation> queue;
        queuing_executor ex(queue);
        std::stop_source ss;
        bool ran = false;
        int values = 0;
        int errors = 0;
        std::error_code ec;
        run_async(ex, ss.get_token(),
            [&](results<> r) { ++values; ec = std::get<0>(r); },
            [&](std::exception_ptr) { ++errors; })(first_statement(ran));
        ss.request_stop();
        pump(queue);
        BOOST_TEST(!ran);
        BOOST_TEST_EQ(values, 1);
        BOOST_TEST_EQ(errors, 0);
        BOOST_TEST(ec == cond::canceled);
    }

    // The same for run(ex)(q): the stop arrives after the quitter is
    // posted to the other executor, before it runs.
    void
    testRunStopAfterPost()
    {
        std::queue<continuation> outer_queue;
        std::queue<continuation> inner_queue;
        queuing_executor outer_ex(outer_queue);
        queuing_executor inner_ex(inner_queue);
        std::stop_source ss;
        bool ran = false;
        seen s;
        auto parent = [&]() -> task<>
        {
            auto [ec] = co_await capy::run(inner_ex)(first_statement(ran));
            s.ec = ec;
            s.resumed = true;
        };
        run_async(outer_ex, ss.get_token())(parent());
        pump(outer_queue);
        BOOST_TEST(!inner_queue.empty());
        ss.request_stop();
        while(!outer_queue.empty() || !inner_queue.empty())
        {
            pump(inner_queue);
            pump(outer_queue);
        }
        BOOST_TEST(!ran);
        BOOST_TEST(s.resumed);
        BOOST_TEST(s.ec == cond::canceled);
    }

    static quitter<>
    shutdown_worker(
        async_waker& w,
        std::atomic<int>& cleaned,
        std::latch& started)
    {
        struct cleanup
        {
            std::atomic<int>& n;
            ~cleanup() { ++n; }
        } guard{cleaned};
        started.count_down();
        for(;;)
            (void)co_await w.wait();
    }

    // The shape of example/quitter-shutdown: workers with no
    // cancellation code, a stop, and a latch counted by the handlers.
    void
    testQuitterShutdownExample()
    {
        constexpr int workers = 4;
        thread_pool pool(workers);
        std::latch done(workers);
        std::latch started(workers);
        std::atomic<int> cleaned{0};
        std::stop_source ss;
        std::array<async_waker, workers> wakers;
        std::vector<strand<thread_pool::executor_type>> strands;
        for(int i = 0; i < workers; ++i)
            strands.emplace_back(pool.get_executor());
        for(int i = 0; i < workers; ++i)
            run_async(strands[i], ss.get_token(),
                [&]() { done.count_down(); },
                [&](std::exception_ptr) { done.count_down(); })(
                    shutdown_worker(wakers[i], cleaned, started));
        started.wait();
        ss.request_stop();
        done.wait();
        BOOST_TEST_EQ(cleaned.load(), workers);
    }

    //----------------------------------------------------------
    // 20. Stops across executors and threads
    //----------------------------------------------------------

    // Blocks until a handler reports, without std::latch, whose
    // ordering TSan does not see.
    struct completion
    {
        std::mutex m;
        std::condition_variable cv;
        bool done = false;

        void signal()
        {
            std::lock_guard<std::mutex> lock(m);
            done = true;
            cv.notify_one();
        }

        void wait()
        {
            std::unique_lock<std::mutex> lock(m);
            cv.wait(lock, [&] { return done; });
        }
    };

    struct hop_state
    {
        bool task_finished = false;
        bool task_on_inner = false;
        int quitter_down = 0;
        bool quitter_reached = false;
        bool parent_on_outer = false;
        seen s;
    };

    // Waits for the stop, then returns canceled through its own code.
    static task<results<>>
    task_until_stop(
        std::latch& registered,
        thread_pool::executor_type inner,
        hop_state& st)
    {
        co_await parked_until_stop{&registered};
        st.task_finished = true;
        st.task_on_inner =
            (co_await this_coro::executor) == executor_ref(inner);
        co_return results<>{make_error_code(error::canceled)};
    }

    static quitter<results<>>
    quitter_over_run(
        thread_pool& inner, std::latch& registered, hop_state& st)
    {
        raii_counter g(st.quitter_down);
        auto [ec] = co_await capy::run(inner.get_executor())(
            task_until_stop(registered, inner.get_executor(), st));
        (void)ec;
        st.quitter_reached = true;
        co_return results<>{};
    }

    // Records what it got, and whether it resumed on `outer`. The
    // executor is held by value: an executor_ref does not own it. Not a
    // template, as complete_then_yield is not.
    static task<>
    await_on(
        quitter<results<>> q,
        thread_pool::executor_type outer,
        hop_state& st)
    {
        auto r = co_await std::move(q);
        st.s.ec = std::get<0>(r);
        st.s.resumed = true;
        st.parent_on_outer =
            (co_await this_coro::executor) == executor_ref(outer);
    }

    // quitter -> run(other executor) -> task: the task stops through
    // its own code on the other executor, and the quitter's parent gets
    // canceled back on its own.
    void
    testStopAcrossRunToTask()
    {
        thread_pool outer(1);
        thread_pool inner(1);
        std::latch registered(1);
        std::stop_source ss;
        hop_state st;
        completion c;
        run_async(outer.get_executor(), ss.get_token(),
            [&]() { c.signal(); },
            [&](std::exception_ptr) { c.signal(); })(
                await_on(quitter_over_run(inner, registered, st),
                    outer.get_executor(), st));
        registered.wait();
        ss.request_stop();
        c.wait();
        BOOST_TEST(st.task_finished);
        BOOST_TEST(st.task_on_inner);
        BOOST_TEST(st.s.resumed);
        BOOST_TEST(st.s.ec == cond::canceled);
        BOOST_TEST(st.parent_on_outer);
        BOOST_TEST(!st.quitter_reached);
        BOOST_TEST_EQ(st.quitter_down, 1);
    }

    static quitter<results<>>
    inner_quitter_until_stop(
        std::latch& registered, int& down, bool& reached)
    {
        raii_counter g(down);
        co_await parked_until_stop{&registered};
        reached = true;
        co_return results<>{};
    }

    static quitter<results<>>
    quitter_over_run_quitter(
        thread_pool& inner, std::latch& registered, hop_state& st,
        int& inner_down, bool& inner_reached)
    {
        raii_counter g(st.quitter_down);
        auto [ec] = co_await capy::run(inner.get_executor())(
            inner_quitter_until_stop(
                registered, inner_down, inner_reached));
        (void)ec;
        st.quitter_reached = true;
        co_return results<>{};
    }

    // quitter -> run(other executor) -> quitter: two segments unwind,
    // and the outer parent gets canceled.
    void
    testStopAcrossRunToQuitter()
    {
        thread_pool outer(1);
        thread_pool inner(1);
        std::latch registered(1);
        std::stop_source ss;
        hop_state st;
        int inner_down = 0;
        bool inner_reached = false;
        completion c;
        run_async(outer.get_executor(), ss.get_token(),
            [&]() { c.signal(); },
            [&](std::exception_ptr) { c.signal(); })(
                await_on(quitter_over_run_quitter(
                    inner, registered, st, inner_down, inner_reached),
                    outer.get_executor(), st));
        registered.wait();
        ss.request_stop();
        c.wait();
        BOOST_TEST(st.s.resumed);
        BOOST_TEST(st.s.ec == cond::canceled);
        BOOST_TEST(st.parent_on_outer);
        BOOST_TEST(!st.quitter_reached);
        BOOST_TEST_EQ(st.quitter_down, 1);
        BOOST_TEST(!inner_reached);
        BOOST_TEST_EQ(inner_down, 1);
    }

    // Q1 -> run(ex, st2)(Q2): stopping st2 gives Q1 canceled, and Q1
    // keeps running, because its own token was not stopped.
    void
    testStopScopedToToken()
    {
        std::queue<continuation> queue;
        queuing_executor ex(queue);
        async_waker w;
        std::stop_source outer;
        std::stop_source inner;
        int down = 0;
        bool reached = false;
        int q1_down = 0;
        bool q1_finished = false;
        std::error_code inner_ec;
        seen s;
        auto q1 = [&]() -> quitter<results<>>
        {
            raii_counter g(q1_down);
            auto [ec] = co_await capy::run(ex, inner.get_token())(
                guarded_wait(w, down, reached));
            inner_ec = ec;
            q1_finished = true;
            co_return results<>{};
        };
        run_async(ex, outer.get_token())(await_into(q1(), s));
        pump(queue);
        inner.request_stop();
        pump(queue);
        BOOST_TEST(inner_ec == cond::canceled);
        BOOST_TEST(q1_finished);
        BOOST_TEST_EQ(down, 1);
        BOOST_TEST(!reached);
        BOOST_TEST_EQ(q1_down, 1);
        BOOST_TEST(s.resumed);
        BOOST_TEST(!s.ec);
    }

    // Records the thread that destroys the frame holding it.
    struct destroyed_on
    {
        std::thread::id* where;

        explicit destroyed_on(std::thread::id& w) noexcept
            : where(&w)
        {
        }

        destroyed_on(destroyed_on&& o) noexcept
            : where(std::exchange(o.where, nullptr))
        {
        }

        ~destroyed_on()
        {
            if(where)
                *where = std::this_thread::get_id();
        }
    };

    static quitter<results<>>
    busy_then_yield(
        std::atomic<bool>& started,
        std::atomic<bool>& stop_requested,
        bool& ran_after_stop,
        bool& reached,
        std::thread::id& destroyed)
    {
        destroyed_on mark(destroyed);
        started = true;
        // Busy, without suspending, until the stop has been requested.
        while(!stop_requested.load())
            std::this_thread::yield();
        ran_after_stop = true;
        co_await yield_awaitable{};
        reached = true;
        co_return results<>{};
    }

    // A stop requested from another thread while the body runs does
    // nothing until the body next suspends, and no frame is touched on
    // the requesting thread.
    void
    testStopWhileRunning()
    {
        thread_pool pool(1);
        std::stop_source ss;
        std::atomic<bool> started{false};
        std::atomic<bool> stop_requested{false};
        bool ran_after_stop = false;
        bool reached = false;
        std::thread::id destroyed;
        seen s;
        completion c;
        run_async(pool.get_executor(), ss.get_token(),
            [&]() { c.signal(); },
            [&](std::exception_ptr) { c.signal(); })(
                await_into(busy_then_yield(started, stop_requested,
                    ran_after_stop, reached, destroyed), s));
        while(!started.load())
            std::this_thread::yield();
        ss.request_stop();
        stop_requested = true;
        c.wait();
        BOOST_TEST(ran_after_stop);
        BOOST_TEST(!reached);
        BOOST_TEST(s.ec == cond::canceled);
        BOOST_TEST(destroyed != std::thread::id());
        BOOST_TEST(destroyed != std::this_thread::get_id());
    }

    //----------------------------------------------------------
    // 21. Misuse that must not corrupt memory
    //----------------------------------------------------------

    // Drives a quitter through its handle in the caller's own segment,
    // as a user-written wrapper might: the quitter's gate then abandons
    // the segment of a task.
    template<class Q>
    struct drive_in_callers_segment
    {
        Q q;

        bool await_ready() const noexcept { return false; }

        std::coroutine_handle<>
        await_suspend(std::coroutine_handle<> cont, await_context const* env)
        {
            auto h = q.handle();
            h.promise().set_continuation(cont);
            h.promise().set_environment(env);
            return h;
        }

        decltype(auto) await_resume() { return q.await_resume(); }
    };

    static task<>
    task_driving_quitter(async_waker& w, int& down, bool& reached)
    {
        // A named awaitable: GCC 11 crashes destroying this frame,
        // suspended here, when the awaitable is an aggregate temporary.
        drive_in_callers_segment<quitter<results<>>> d{
            guarded_wait(w, down, reached)};
        auto r = co_await std::move(d);
        (void)r;
    }

    struct misuse_outcome
    {
        int down = 0;
        bool reached = false;
        bool parent_resumed = false;
        int handlers = 0;
    };

    // Stops a quitter that a task drives in its own segment, started
    // directly by run_async, or by run_async through run(ex).
    static void
    stop_in_task_segment(misuse_outcome& out, bool through_run)
    {
        std::queue<continuation> queue;
        queuing_executor ex(queue);
        async_waker w;
        std::stop_source ss;
        auto parent = [&]() -> task<>
        {
            co_await capy::run(ex)(
                task_driving_quitter(w, out.down, out.reached));
            out.parent_resumed = true;
        };
        auto on_value = [&]() { ++out.handlers; };
        auto on_error = [&](std::exception_ptr) { ++out.handlers; };
        if(through_run)
            run_async(ex, ss.get_token(), std::pmr::new_delete_resource(),
                on_value, on_error)(parent());
        else
            run_async(ex, ss.get_token(), std::pmr::new_delete_resource(),
                on_value, on_error)(
                    task_driving_quitter(w, out.down, out.reached));
        pump(queue);
        ss.request_stop();
        pump(queue);
    }

    // The stop reaches the owner of a task's segment. A debug build
    // asserts in the launcher. Otherwise the task is torn down: no
    // handler reads its destroyed promise.
    void
    testStopInTaskSegmentIsTeardownUnderRunAsync()
    {
#ifdef NDEBUG
        misuse_outcome out;
        stop_in_task_segment(out, false);
        BOOST_TEST_EQ(out.handlers, 0);
        BOOST_TEST_EQ(out.down, 1);
        BOOST_TEST(!out.reached);
#elif !defined(_WIN32)
        BOOST_TEST(aborts([] {
            misuse_outcome out;
            stop_in_task_segment(out, false);
        }));
#endif
    }

    void
    testStopInTaskSegmentIsTeardownUnderRun()
    {
#ifdef NDEBUG
        misuse_outcome out;
        stop_in_task_segment(out, true);
        BOOST_TEST(!out.parent_resumed);
        BOOST_TEST_EQ(out.handlers, 0);
        BOOST_TEST_EQ(out.down, 1);
        BOOST_TEST(!out.reached);
#elif !defined(_WIN32)
        BOOST_TEST(aborts([] {
            misuse_outcome out;
            stop_in_task_segment(out, true);
        }));
#endif
    }

#if !defined(_WIN32) && !defined(NDEBUG)
    static quitter<>
    yields_once()
    {
        co_await yield_awaitable{};
    }

    // A quitter run with no chain_owner cannot be stopped; a debug
    // build says so when a stop arrives instead of ignoring it.
    void
    testOwnerlessStopAsserts()
    {
        BOOST_TEST(aborts([] {
            int n = 0;
            test_executor ex(n);
            std::stop_source ss;
            await_context env{executor_ref(ex), ss.get_token(), nullptr};
            auto q = yields_once();
            auto h = q.handle();
            q.release();
            h.promise().set_environment(&env);
            ss.request_stop();
            h.resume();
        }));
    }
#endif

    //----------------------------------------------------------
    // run()
    //----------------------------------------------------------

    void
    run()
    {
        testMoveAssignDestroysHeldFrame();
#if !defined(_WIN32)
        testDestroyLentTaskTerminates();
        testMoveAssignOverLentTaskTerminates();
        testDestroyUnstartedOrFinishedTaskIsFine();
#endif
        testTeardownThroughQuitter();
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
        testBoolSuspendAwaitable();
        testBoolSuspendThenStop();
        testCoAwaitVoidQuitter();
        testCoAwaitThrowingQuitter();
        testWhenAllWithStop();
        testWhenAnyWithStop();
        testWakerCancellation();
        testCatchAfterRmwAwaitReady();
        testCatchStopAfterRmwAwaitReady();
        testStopPendingLeavesMutexUntouched();
        testReadyThenStopRunsAwaitResume();
        testReturnSelfThenStopRunsAwaitResume();
        testReadyCanceledThenStopStops();
        testSuspendCanceledThenStopStops();
        testReadySuccessThenStopDelivers();
        testReadyCanceledWithoutStopDelivers();
        testEchoWithShutdown();
        testUnstoppedValue();
        testStopDeliversCanceled();
        testFramesNotResumed();
        testCatchAllNeverRuns();
        testTaskBelowCooperates();
        testQuitterJoinsQuitterSegment();
        testTaskBetweenQuittersCooperates();
        testStopPendingAtAwaitAbandons();
        testStopPendingAtJoinAbandons();
        testInlineCompletionContinues();
        testBoolFalseCompletionContinues();
        testStopBeforeFirstResume();
        testWhenAllBranchStopNotLost();
        testOwnerDestroyedByClosedExecutorIsTeardown();
        testNoDoubleDestroyOnStop();
        testStopRacesCompletion();
        testStopAfterWakeBeforeGate();
        testStopAfterGrantBeforeGate();
        testDeepQuitterChainUnwinds();
        testSegmentNotOnChainAllocator();
        testStoppedResultsDelivered();
        testStoppedErrorCodeDelivered();
        testStoppedVoidThrows();
        testStoppedPlainThrows();
        testRunAsyncStoppedResults();
        testRunAsyncStoppedResultsPayload();
        testRunAsyncStoppedVoidCallsErrorHandler();
        testRunAsyncStoppedPlainCallsErrorHandler();
        testDefaultHandlerDiscardsStop();
        testDefaultHandlerDiscardsStopEscapingTask();
#if !defined(_WIN32)
        testDefaultHandlerTerminatesOnOtherErrors();
#endif
        testUserCanceledReachesErrorHandler();
        testRunNoExecutorStopScoped();
        testTeardownStaysSilent();
        testRunStoppedPlainThrowsToParent();
        testRunStopBeforeStart();
        testRunTokenStopBeforeStart();
        testRunAsyncStopAfterLaunch();
        testRunStopAfterPost();
        testQuitterShutdownExample();
        testStopAcrossRunToTask();
        testStopAcrossRunToQuitter();
        testStopScopedToToken();
        testStopWhileRunning();
        testStopInTaskSegmentIsTeardownUnderRunAsync();
        testStopInTaskSegmentIsTeardownUnderRun();
#if !defined(_WIN32) && !defined(NDEBUG)
        testOwnerlessStopAsserts();
#endif
    }
};

TEST_SUITE(
    quitter_test,
    "boost.capy.quitter");

} // capy
} // boost
