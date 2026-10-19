//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Test that header file is self-contained.
#include <boost/capy/test/run_blocking.hpp>

#include <boost/capy/error.hpp>
#include <boost/capy/task.hpp>
#include <boost/capy/ex/strand.hpp>
#include <boost/capy/ex/this_coro.hpp>

#include "test_helpers.hpp"
#include "test_suite.hpp"

#include <optional>
#include <stdexcept>
#include <string>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

namespace boost {
namespace capy {
namespace test {

static_assert(Executor<blocking_executor>);
static_assert(ExecutionContext<blocking_context>);

//----------------------------------------------------------
// Test Exception Type
//----------------------------------------------------------

struct test_exception : std::runtime_error
{
    explicit test_exception(char const* msg)
        : std::runtime_error(msg)
    {
    }
};

namespace {

// Posts a continuation from its destructor, as a frame-local primitive
// does when it is torn down.
struct posts_on_destroy
{
    blocking_executor ex;
    continuation* c;

    posts_on_destroy(blocking_executor e, continuation* cc) noexcept
        : ex(e)
        , c(cc)
    {
    }

    posts_on_destroy(posts_on_destroy&& o) noexcept
        : ex(o.ex)
        , c(std::exchange(o.c, nullptr))
    {
    }

    ~posts_on_destroy()
    {
        if(c)
            ex.post(*c);
    }
};

task<void>
posting_probe(destroy_count_t, posts_on_destroy)
{
    co_return;
}

task<void>
plain_probe(destroy_count_t)
{
    co_return;
}

// Posts from its destructor, after the context's services shut down.
struct dtor_poster : execution_context::service
{
    blocking_executor ex_;
    continuation* c_;

    dtor_poster(execution_context&, blocking_executor ex, continuation& c)
        : ex_(ex)
        , c_(&c)
    {
    }

    ~dtor_poster()
    {
        ex_.post(*c_);
    }

    void shutdown() override {}
};

// Posts from a thread of its own while the context's services shut down,
// and joins that thread before its shutdown() returns.
struct shutdown_poster : execution_context::service
{
    blocking_executor ex_;
    std::vector<continuation>* cs_;

    shutdown_poster(
        execution_context&,
        blocking_executor ex,
        std::vector<continuation>& cs)
        : ex_(ex)
        , cs_(&cs)
    {
    }

    void shutdown() override
    {
        std::thread t([this]
        {
            for(auto& c : *cs_)
                ex_.post(c);
        });
        t.join();
    }
};

// Dispatches a coroutine while its context shuts down.
struct shutdown_dispatcher : execution_context::service
{
    blocking_executor ex_;
    continuation* c_;

    shutdown_dispatcher(
        execution_context&,
        blocking_executor ex,
        continuation& c)
        : ex_(ex)
        , c_(&c)
    {
    }

    void shutdown() override
    {
        safe_resume(ex_.dispatch(*c_));
    }
};

// Posts when the context's services shut down, as a service giving up
// its work would.
struct late_poster : execution_context::service
{
    blocking_executor ex_;
    continuation* c_;

    late_poster(execution_context&, blocking_executor ex, continuation& c)
        : ex_(ex)
        , c_(&c)
    {
    }

    void shutdown() override
    {
        ex_.post(*c_);
    }
};

// A second late_poster, so one can be created while the first exists.
struct later_poster : late_poster
{
    using late_poster::late_poster;
};

// Records, when the context destroys it, how many probes were already
// destroyed. Made after the services the test creates, it is destroyed
// before them.
struct teardown_witness : execution_context::service
{
    int const* down_;
    int* seen_;

    teardown_witness(execution_context&, int const& down, int& seen)
        : down_(&down)
        , seen_(&seen)
    {
    }

    ~teardown_witness()
    {
        *seen_ = *down_;
    }

    void shutdown() override {}
};

// Creates a later_poster on the context when destroyed.
struct poster_maker
{
    blocking_context* ctx;
    continuation* c;

    poster_maker(blocking_context& x, continuation& cont) noexcept
        : ctx(&x)
        , c(&cont)
    {
    }

    poster_maker(poster_maker&& o) noexcept
        : ctx(std::exchange(o.ctx, nullptr))
        , c(o.c)
    {
    }

    ~poster_maker()
    {
        if(ctx)
            ctx->make_service<later_poster>(ctx->get_executor(), *c);
    }
};

} // namespace

//----------------------------------------------------------
// run_blocking Tests
//----------------------------------------------------------

struct run_blocking_test
{
    //----------------------------------------------------------
    // Basic Functionality
    //----------------------------------------------------------

    static task<int>
    returns_int()
    {
        co_return 42;
    }

    static task<void>
    returns_void()
    {
        co_return;
    }

    static task<std::string>
    returns_string()
    {
        co_return "hello";
    }

    void
    testVoidTaskNoHandler()
    {
        // Should complete without blocking forever
        run_blocking()(returns_void());
        BOOST_TEST(true);
    }

    void
    testIntTaskNoHandler()
    {
        // Result is discarded
        run_blocking()(returns_int());
        BOOST_TEST(true);
    }

    void
    testIntTaskWithHandler()
    {
        int result = 0;
        run_blocking([&](int v) { result = v; })(returns_int());
        BOOST_TEST_EQ(result, 42);
    }

    void
    testVoidTaskWithHandler()
    {
        bool called = false;
        run_blocking([&]() { called = true; })(returns_void());
        BOOST_TEST(called);
    }

    void
    testStringTaskWithHandler()
    {
        std::string result;
        run_blocking([&](std::string s) { result = std::move(s); })(
            returns_string());
        BOOST_TEST_EQ(result, "hello");
    }

    void
    testDualHandlers()
    {
        int result = 0;
        bool error_called = false;

        run_blocking(
            [&](int v) { result = v; },
            [&](std::exception_ptr) { error_called = true; }
        )(returns_int());

        BOOST_TEST_EQ(result, 42);
        BOOST_TEST(!error_called);
    }

    //----------------------------------------------------------
    // Exception Handling
    //----------------------------------------------------------

    static task<int>
    throws_exception()
    {
        throw test_exception("test error");
        co_return 0;
    }

    static task<void>
    void_throws_exception()
    {
        throw test_exception("void task error");
        co_return;
    }

    void
    testExceptionHandler()
    {
        bool success_called = false;
        bool error_called = false;

        run_blocking(
            [&](int) { success_called = true; },
            [&](std::exception_ptr ep) {
                error_called = true;
                BOOST_TEST(ep != nullptr);
            }
        )(throws_exception());

        BOOST_TEST(!success_called);
        BOOST_TEST(error_called);
    }

    void
    testVoidExceptionHandler()
    {
        bool success_called = false;
        bool error_called = false;

        run_blocking(
            [&]() { success_called = true; },
            [&](std::exception_ptr ep) {
                error_called = true;
                BOOST_TEST(ep != nullptr);
            }
        )(void_throws_exception());

        BOOST_TEST(!success_called);
        BOOST_TEST(error_called);
    }

    void
    testExceptionRethrown()
    {
        bool caught = false;
        try
        {
            run_blocking()(throws_exception());
        }
        catch(test_exception const& e)
        {
            caught = true;
            BOOST_TEST(std::string(e.what()) == "test error");
        }
        BOOST_TEST(caught);
    }

    void
    testOverloadedHandlerException()
    {
        bool got_value = false;
        bool got_exception = false;

        struct handler_t
        {
            bool* got_value_;
            bool* got_exception_;
            void operator()(int) { *got_value_ = true; }
            void operator()(std::exception_ptr) { *got_exception_ = true; }
        };

        run_blocking(handler_t{&got_value, &got_exception})(
            throws_exception());

        BOOST_TEST(!got_value);
        BOOST_TEST(got_exception);
    }

    //----------------------------------------------------------
    // blocking_executor Tests
    //----------------------------------------------------------

    void
    testBlockingExecutorConcept()
    {
        static_assert(Executor<blocking_executor>);
        static_assert(ExecutionContext<blocking_context>);
        BOOST_TEST(true);
    }

    //----------------------------------------------------------
    // Stop Token Tests
    //----------------------------------------------------------

    static task<bool>
    check_stop_requested()
    {
        auto token = co_await this_coro::stop_token;
        co_return token.stop_requested();
    }

    void
    testStopTokenNotRequested()
    {
        bool result = true;

        std::stop_source source;
        run_blocking(source.get_token(), [&](bool v) { result = v; })(
            check_stop_requested());

        BOOST_TEST(!result);
    }

    void
    testStopTokenRequested()
    {
        bool result = false;

        std::stop_source source;
        source.request_stop();

        run_blocking(source.get_token(), [&](bool v) { result = v; })(
            check_stop_requested());

        BOOST_TEST(result);
    }

    //----------------------------------------------------------
    // Allocator Propagation
    //----------------------------------------------------------

    static task<bool>
    check_allocator_propagated()
    {
        auto* alloc = co_await this_coro::frame_allocator;
        co_return alloc != nullptr;
    }

    void
    testAllocatorPropagation()
    {
        bool result = false;
        run_blocking([&](bool v) { result = v; })(
            check_allocator_propagated());
        BOOST_TEST(result);
    }

    //----------------------------------------------------------
    // Nested Task Tests
    //----------------------------------------------------------

    static task<int>
    nested_task()
    {
        int v = co_await returns_int();
        co_return v + 1;
    }

    void
    testNestedTask()
    {
        int result = 0;
        run_blocking([&](int v) { result = v; })(nested_task());
        BOOST_TEST_EQ(result, 43);
    }

    void
    testDestroyedContextDestroysQueued()
    {
        int down = 0;
        auto probe = [](destroy_count_t) -> task<void> { co_return; };
        auto t = probe(destroy_count_t(down));
        auto h = t.handle();
        t.release();
        {
            blocking_context ctx;
            continuation c{h};
            ctx.get_executor().post(c);
        }
        BOOST_TEST_EQ(down, 1);
    }

    // Work a service's own thread posts while the service shuts down is
    // destroyed with the context, none of it lost.
    void
    testPostFromServiceThreadAtShutdown()
    {
        constexpr int n = 16;
        int down = 0;
        auto probe = [](destroy_count_t) -> task<void> { co_return; };
        std::vector<continuation> cs;
        cs.reserve(n);
        for(int i = 0; i < n; ++i)
        {
            auto t = probe(destroy_count_t(down));
            cs.push_back(continuation{t.handle()});
            t.release();
        }
        {
            blocking_context ctx;
            ctx.make_service<shutdown_poster>(ctx.get_executor(), cs);
        }
        BOOST_TEST_EQ(down, n);
    }

    // Dispatch runs inline only from inside the context's own run(), so
    // a context being shut down runs nothing dispatched to it.
    void
    testDispatchDuringShutdown()
    {
        int down = 0;
        bool ran = false;
        auto probe = [](destroy_count_t, bool& r) -> task<void>
        {
            r = true;
            co_return;
        };
        auto t = probe(destroy_count_t(down), ran);
        continuation c{t.handle()};
        t.release();
        {
            blocking_context ctx;
            ctx.make_service<shutdown_dispatcher>(ctx.get_executor(), c);
        }
        BOOST_TEST(!ran);
        BOOST_TEST_EQ(down, 1);
    }

    // An abandoned chain delivers no result: the loop ends with
    // error::canceled instead.
    void
    testAbandonedChainThrows()
    {
        int down = 0;
        bool called = false;
        auto probe = [](destroy_count_t) -> task<void>
        {
            co_await self_destroy_awaitable{};
        };
        std::error_code ec;
        try
        {
            run_blocking([&]{ called = true; })(probe(destroy_count_t(down)));
        }
        catch(std::system_error const& e)
        {
            ec = e.code();
        }
        BOOST_TEST(ec == error::canceled);
        BOOST_TEST(!called);
        BOOST_TEST_EQ(down, 1);
    }

    // A service should not post from its destructor, but work one posts
    // anyway is still destroyed with the context.
    void
    testPostFromServiceDestructor()
    {
        int down = 0;
        auto t = plain_probe(destroy_count_t(down));
        continuation c{t.handle()};
        t.release();
        {
            blocking_context ctx;
            ctx.make_service<dtor_poster>(ctx.get_executor(), c);
        }
        BOOST_TEST_EQ(down, 1);
    }

    // A frame destroyed while the context shuts down may create a service.
    // That service is shut down as it is created, and the drain destroys
    // the work it gives up before the services made before the teardown
    // are destroyed.
    void
    testServiceCreatedDuringDrain()
    {
        int down = 0;
        int seen = -1;
        auto maker = [](poster_maker) -> task<void> { co_return; };
        auto t = plain_probe(destroy_count_t(down));
        continuation probe_c{t.handle()};
        t.release();
        continuation maker_c;
        {
            blocking_context ctx;
            auto m = maker(poster_maker(ctx, probe_c));
            maker_c.h = m.handle();
            m.release();
            ctx.make_service<late_poster>(ctx.get_executor(), maker_c);
            ctx.make_service<teardown_witness>(down, seen);
        }
        BOOST_TEST_EQ(seen, 1);
        BOOST_TEST_EQ(down, 1);
    }

    // A frame its services destroy at shutdown can post to the context;
    // that work is destroyed, not written into a freed queue.
    void
    testPostDuringServiceShutdown()
    {
        int down = 0;
        int posted_down = 0;
        // Outlives the context, so the queued work is given up by the
        // context's shutdown rather than by the strand's destructor.
        std::optional<strand<blocking_executor>> s;
        continuation c1;
        continuation c2;
        {
            blocking_context ctx;
            auto ex = ctx.get_executor();
            s.emplace(ex);
            auto t2 = plain_probe(destroy_count_t(posted_down));
            c2.h = t2.handle();
            t2.release();
            auto t1 = posting_probe(
                destroy_count_t(down), posts_on_destroy(ex, &c2));
            c1.h = t1.handle();
            t1.release();
            s->post(c1);
        }
        s.reset();
        BOOST_TEST_EQ(down, 1);
        BOOST_TEST_EQ(posted_down, 1);
    }

    //----------------------------------------------------------

    void
    run()
    {
        // Basic Functionality
        testVoidTaskNoHandler();
        testIntTaskNoHandler();
        testIntTaskWithHandler();
        testVoidTaskWithHandler();
        testStringTaskWithHandler();
        testDualHandlers();

        // Exception Handling
        testExceptionHandler();
        testVoidExceptionHandler();
        testExceptionRethrown();
        testOverloadedHandlerException();

        // blocking_executor
        testBlockingExecutorConcept();

        // Stop Token
        testStopTokenNotRequested();
        testStopTokenRequested();

        // Allocator Propagation
        testAllocatorPropagation();

        // Nested Tasks
        testNestedTask();

        // Teardown
        testDestroyedContextDestroysQueued();
        testDispatchDuringShutdown();
        testAbandonedChainThrows();
        testPostFromServiceThreadAtShutdown();
        testPostDuringServiceShutdown();
        testPostFromServiceDestructor();
        testServiceCreatedDuringDrain();
    }
};

TEST_SUITE(
    run_blocking_test,
    "boost.capy.ex.run_blocking");

} // namespace test
} // namespace capy
} // namespace boost
