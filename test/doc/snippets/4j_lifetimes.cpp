//
// Copyright (c) 2026 Steve Gerbino
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Compiled fragments shown in pages/4.coroutines/4j.lifetimes.adoc.
// The fragments that demonstrate dangling references are compiled but
// never executed: running them would be undefined behavior.

#include "../doc_warnings.hpp"

#include <boost/capy/cond.hpp>
#include <boost/capy/ex/any_executor.hpp>
#include <boost/capy/ex/async_event.hpp>
#include <boost/capy/ex/run_async.hpp>
#include <boost/capy/ex/thread_pool.hpp>
#include <boost/capy/task.hpp>
#include <boost/capy/test/run_blocking.hpp>

#include <cstddef>
#include <exception>
#include <latch>
#include <stop_token>
#include <string>
#include <utility>

#include "test_suite.hpp"

namespace capy = boost::capy;

namespace {

capy::any_executor executor;

// tag::ref_param_awaited[]
capy::task<std::size_t> count_bytes(std::string const& text)
{
    co_return text.size();
}

capy::task<std::size_t> awaited_at_once()
{
    // The temporary lives until the end of the full expression,
    // and the full expression includes the whole co_await.
    co_return co_await count_bytes(std::string("hello"));
}
// end::ref_param_awaited[]

// tag::ref_param_deferred[]
capy::task<std::size_t> awaited_later()
{
    auto t = count_bytes(std::string("hello")); // <1>
    co_return co_await std::move(t);            // <2>
}
// end::ref_param_deferred[]

// tag::value_param[]
capy::task<std::size_t> count_owned_bytes(std::string text)
{
    co_return text.size();
}

capy::task<std::size_t> awaited_later_by_value()
{
    auto t = count_owned_bytes(std::string("hello"));
    co_return co_await std::move(t);
}
// end::value_param[]

void start_dangling()
{
    // tag::run_async_temporary[]
    capy::run_async(executor)(count_bytes(std::string("hello"))); // <1>
    // end::run_async_temporary[]
}

void start_owned()
{
    // tag::run_async_owned[]
    capy::run_async(executor)(count_owned_bytes(std::string("hello")));
    // end::run_async_owned[]
}

// tag::unwind[]
struct slot_guard
{
    int& in_use;

    explicit slot_guard(int& n) noexcept
        : in_use(n)
    {
        ++in_use;
    }

    ~slot_guard()
    {
        --in_use; // <1>
    }
};

capy::task<> wait_for_work(capy::async_event& ready, int& in_use)
{
    slot_guard guard(in_use);
    auto [ec] = co_await ready.wait();
    if(ec)
        co_return;
    // <2>
}
// end::unwind[]

bool stopped_through_own_code = false;

capy::task<> wait_until_stopped(capy::async_event& never_set)
{
    auto [ec] = co_await never_set.wait();
    stopped_through_own_code = (ec == capy::cond::canceled);
}

capy::task<> noop()
{
    co_return;
}

struct lifetimes_test
{
    void
    testAwaitedAtOnce()
    {
        std::size_t n = 0;
        capy::test::run_blocking([&](std::size_t v) { n = v; })(
            awaited_at_once());
        BOOST_TEST(n == 5);
    }

    void
    testValueParam()
    {
        std::size_t n = 0;
        capy::test::run_blocking([&](std::size_t v) { n = v; })(
            awaited_later_by_value());
        BOOST_TEST(n == 5);
    }

    void
    testGuardOnCompletion()
    {
        int in_use = 0;
        capy::async_event ready;
        ready.set();
        capy::test::run_blocking()(wait_for_work(ready, in_use));
        BOOST_TEST(in_use == 0);
    }

    // The pool is stopped while the coroutine waits, so destroying the
    // event posts it to a queue that never runs, and destroying the
    // pool abandons it.
    void
    testGuardOnUnwind()
    {
        int in_use = 0;
        bool completed = false;
        {
            capy::thread_pool pool(1);
            capy::async_event ready;
            capy::run_async(pool.get_executor(),
                [&] { completed = true; },
                [&](std::exception_ptr) { completed = true; })(
                    wait_for_work(ready, in_use));

            // The one worker runs in order, so this runs only once the
            // first coroutine is suspended in wait().
            std::latch suspended(1);
            capy::run_async(pool.get_executor(),
                [&] { suspended.count_down(); })(noop());
            suspended.wait();
            BOOST_TEST(in_use == 1);
            pool.stop();
            pool.join();
        }
        BOOST_TEST(in_use == 0);
        BOOST_TEST(! completed);
    }

    void
    testShutdownOrder()
    {
        stopped_through_own_code = false;
        capy::async_event never_set;
        // tag::shutdown_order[]
        capy::thread_pool pool(1);
        std::stop_source stop;
        capy::run_async(pool.get_executor(), stop.get_token())(
            wait_until_stopped(never_set));

        // Later, to shut down:
        stop.request_stop(); // <1>
        pool.join();         // <2>
        // <3>
        // end::shutdown_order[]
        BOOST_TEST(stopped_through_own_code);
    }

    void
    run()
    {
        testAwaitedAtOnce();
        testValueParam();
        testGuardOnCompletion();
        testGuardOnUnwind();
        testShutdownOrder();
    }
};

} // namespace

TEST_SUITE(lifetimes_test, "boost.capy.doc.4j_lifetimes");
