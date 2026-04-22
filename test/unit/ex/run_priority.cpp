//
// Copyright (c) 2026 Steve Gerbino
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Tests that fail against the current run()'s use of dispatch at
// cross-executor boundaries; pass once run() posts on both trips.

#include <boost/capy/concept/executor.hpp>
#include <boost/capy/ex/run.hpp>
#include <boost/capy/ex/run_async.hpp>
#include <boost/capy/ex/strand.hpp>
#include <boost/capy/ex/thread_pool.hpp>
#include <boost/capy/task.hpp>

#include "priority_executor.hpp"
#include "test/unit/test_helpers.hpp"
#include "test_suite.hpp"

#include <coroutine>
#include <cstddef>
#include <queue>
#include <string>
#include <utility>
#include <vector>

namespace boost {
namespace capy {

static_assert(Executor<test::priority_executor<queuing_executor>>,
    "priority_executor must satisfy Executor concept");

namespace {

// Bare coroutine that appends a message and ends. Posted directly.
struct log_coro
{
    struct promise_type
    {
        std::vector<std::string>* log;
        std::string msg;

        log_coro get_return_object() noexcept
        {
            return log_coro{
                std::coroutine_handle<promise_type>::from_promise(*this)};
        }

        std::suspend_always initial_suspend() noexcept { return {}; }
        std::suspend_never final_suspend() noexcept { return {}; }
        void return_void() noexcept {}
        void unhandled_exception() { std::terminate(); }
    };

    std::coroutine_handle<promise_type> h_;

    ~log_coro() { if(h_) h_.destroy(); }

    log_coro(log_coro&& o) noexcept : h_(o.h_) { o.h_ = nullptr; }

    std::coroutine_handle<void> handle() const noexcept { return h_; }

    void release() noexcept { h_ = nullptr; }

private:
    explicit log_coro(std::coroutine_handle<promise_type> h) : h_(h) {}
};

inline log_coro
make_log_coro(std::vector<std::string>& log, std::string msg)
{
    return [](std::vector<std::string>* log, std::string msg) -> log_coro {
        log->push_back(std::move(msg));
        co_return;
    }(&log, std::move(msg));
}

inline void
pump(std::queue<std::coroutine_handle<>>& q)
{
    while(!q.empty())
    {
        auto h = q.front();
        q.pop();
        h.resume();
    }
}

} // namespace

struct run_priority_test
{
    // run(pe)(inner) from a handler on pe must enqueue inner
    // behind other work already in pe's queue, not cut in line.
    void
    testForwardCrossing()
    {
        std::queue<std::coroutine_handle<>> q;
        queuing_executor qe(q);
        test::priority_executor_state state;
        test::priority_executor pe(state, qe);

        std::vector<std::string> log;

        auto inner_task_fn = [&]() -> task<void> {
            log.push_back("inner");
            co_return;
        };

        auto outer_task_fn = [&]() -> task<void> {
            log.push_back("outer_start");
            co_await capy::run(pe)(inner_task_fn());
            log.push_back("outer_end");
        };

        bool outer_done = false;
        run_async(pe, [&]() { outer_done = true; })(outer_task_fn());

        auto sibling_coro = make_log_coro(log, "sibling");
        continuation sibling_cont{sibling_coro.handle()};
        pe.post(sibling_cont);
        sibling_coro.release();

        pump(q);

        BOOST_TEST(outer_done);

        BOOST_TEST_EQ(log.size(), std::size_t(4));
        if(log.size() == 4)
        {
            BOOST_TEST_EQ(log[0], std::string("outer_start"));
            BOOST_TEST_EQ(log[1], std::string("sibling"));
            BOOST_TEST_EQ(log[2], std::string("inner"));
            BOOST_TEST_EQ(log[3], std::string("outer_end"));
        }
    }

    // The return trip must post the caller back to its executor,
    // giving pe a tick to drain higher-priority work before the
    // caller resumes. inline_ex is chosen as the target so the
    // forward trip is trivial and only the return trip is observed.
    void
    testReturnTripParentWrongFrame()
    {
        std::queue<std::coroutine_handle<>> q;
        queuing_executor qe(q);
        test::priority_executor_state state;
        test::priority_executor pe(state, qe);

        std::vector<std::string> log;

        auto inner_task_fn = [&]() -> task<void> {
            log.push_back("inner");
            co_return;
        };

        auto outer_task_fn = [&]() -> task<void> {
            log.push_back("outer_start");

            auto pending_high_coro = make_log_coro(log, "pending_high");
            continuation pending_high_cont{pending_high_coro.handle()};
            pe.post_high(pending_high_cont);
            pending_high_coro.release();

            int dummy = 0;
            test_executor inline_ex(1, dummy);
            co_await capy::run(inline_ex)(inner_task_fn());

            log.push_back("outer_end");
        };

        bool outer_done = false;
        run_async(pe, [&]() { outer_done = true; })(outer_task_fn());

        pump(q);

        BOOST_TEST(outer_done);

        BOOST_TEST_EQ(log.size(), std::size_t(4));
        if(log.size() == 4)
        {
            BOOST_TEST_EQ(log[0], std::string("outer_start"));
            BOOST_TEST_EQ(log[1], std::string("inner"));
            BOOST_TEST_EQ(log[2], std::string("pending_high"));
            BOOST_TEST_EQ(log[3], std::string("outer_end"));
        }
    }

    // run(inner)(work) from inside a strand must actually release
    // the strand while work runs, not nest work in the strand's frame.
    void
    testExitStrandOverPriority()
    {
        std::queue<std::coroutine_handle<>> q;
        queuing_executor qe(q);
        test::priority_executor_state state;
        test::priority_executor pe(state, qe);

        strand<test::priority_executor<queuing_executor>> s(pe);

        bool s_running_inside_work = false;
        bool work_ran = false;

        auto work_task_fn = [&]() -> task<void> {
            s_running_inside_work = s.running_in_this_thread();
            work_ran = true;
            co_return;
        };

        auto outer_task_fn = [&]() -> task<void> {
            co_await capy::run(pe)(work_task_fn());
        };

        bool outer_done = false;
        run_async(s, [&]() { outer_done = true; })(outer_task_fn());

        pump(q);

        BOOST_TEST(outer_done);
        BOOST_TEST(work_ran);
        BOOST_TEST(!s_running_inside_work);
    }

    void
    run()
    {
        testForwardCrossing();
        testReturnTripParentWrongFrame();
        testExitStrandOverPriority();
    }
};

TEST_SUITE(
    run_priority_test,
    "boost.capy.run.priority");

} // namespace capy
} // namespace boost
