//
// Copyright (c) 2026 Steve Gerbino
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Self-checks for the early quitter: stops at each depth, completions
// racing a stop, a task between quitters, and teardown.

#include "quitter.hpp"

#include <boost/capy.hpp>

#include <atomic>
#include <cstdio>
#include <memory_resource>
#include <queue>
#include <stdexcept>
#include <thread>

namespace capy = boost::capy;
using early::quitter;
using capy::results;

static int failures = 0;

#define CHECK(e) \
    do { if(!(e)) { ++failures; std::printf("%s:%d: CHECK(%s) failed\n", \
        __FILE__, __LINE__, #e); } } while(0)

struct queue_context : capy::execution_context
{
    queue_context() : execution_context(this) {}
};

static queue_context g_ctx;

// Queues every post; pump() runs them in order.
struct queue_executor
{
    std::queue<capy::continuation>* q;
    bool* closed = nullptr;

    bool operator==(queue_executor const& o) const noexcept { return q == o.q; }
    capy::execution_context& context() const noexcept { return g_ctx; }
    void on_work_started() const noexcept {}
    void on_work_finished() const noexcept {}

    std::coroutine_handle<> dispatch(capy::continuation& c) const
    {
        post(c);
        return std::noop_coroutine();
    }

    // A closed executor gives up what is posted to it.
    void post(capy::continuation& c) const
    {
        if(closed && *closed)
            capy::abandon(c);
        else
            q->push(c);
    }
};

static_assert(capy::Executor<queue_executor>);
static_assert(capy::ContextRunnable<quitter<results<>>>);
static_assert(capy::ContextRunnable<quitter<std::error_code>>);

static void pump(std::queue<capy::continuation>& q)
{
    while(!q.empty())
    {
        auto c = q.front();
        q.pop();
        c.h.resume();
    }
}

struct counter
{
    int* n;
    explicit counter(int& c) : n(&c) {}
    counter(counter&& o) noexcept : n(std::exchange(o.n, nullptr)) {}
    ~counter() { if(n) ++*n; }
};

struct yield_awaitable
{
    capy::continuation c_;
    bool await_ready() const noexcept { return false; }
    void await_suspend(std::coroutine_handle<> h, capy::await_context const* env)
    {
        c_.h = h;
        c_.owner = env->owner;
        env->executor.post(c_);
    }
    void await_resume() const noexcept {}
};

// Completes at once with `r`; optionally requests a stop first.
template<class R>
struct ready_awaitable
{
    R r;
    std::stop_source* stop = nullptr;
    bool await_ready() noexcept
    {
        if(stop)
            stop->request_stop();
        return true;
    }
    void await_suspend(std::coroutine_handle<>, capy::await_context const*) noexcept {}
    R await_resume() { return r; }
};

struct throwing_ready
{
    std::stop_source* stop;
    bool await_ready() noexcept { stop->request_stop(); return true; }
    void await_suspend(std::coroutine_handle<>, capy::await_context const*) noexcept {}
    results<> await_resume() { throw std::runtime_error("early"); }
};

// What run_async's value handler got.
struct outcome
{
    int calls = 0;
    std::error_code ec;
    int value = 0;
};

template<class Q>
static void launch(std::queue<capy::continuation>& q, std::stop_token st,
    outcome& o, Q quitter_, bool* closed = nullptr)
{
    queue_executor ex{&q, closed};
    capy::run_async(ex, st, std::pmr::new_delete_resource(),
        [&o](auto r) {
            ++o.calls;
            if constexpr (std::is_same_v<decltype(r), std::error_code>)
                o.ec = r;
            else
            {
                o.ec = std::get<0>(r);
                if constexpr (std::tuple_size_v<decltype(r)> == 2)
                    o.value = std::get<1>(r);
            }
        },
        [&o](std::exception_ptr) { o.calls += 100; })(std::move(quitter_));
}

//------------------------------------------------

static quitter<results<int>> two_yields()
{
    co_await yield_awaitable{};
    co_await yield_awaitable{};
    co_return results<int>{std::error_code(), 42};
}

static void value_without_stop()
{
    std::queue<capy::continuation> q;
    std::stop_source ss;
    outcome o;
    launch(q, ss.get_token(), o, two_yields());
    pump(q);
    CHECK(o.calls == 1);
    CHECK(!o.ec);
    CHECK(o.value == 42);
}

//------------------------------------------------

struct reach
{
    bool leaf = false, mid = false, top = false;
};

static quitter<int> leaf(capy::async_waker& w, int& down, reach& r)
{
    counter c(down);
    std::ignore = co_await w.wait();
    r.leaf = true;
    co_return 1;
}

static quitter<> mid(capy::async_waker& w, int& down, reach& r)
{
    counter c(down);
    std::ignore = co_await leaf(w, down, r);
    r.mid = true;
}

static quitter<results<>> top(capy::async_waker& w, int& down, reach& r)
{
    counter c(down);
    co_await mid(w, down, r);
    r.top = true;
    co_return results<>{};
}

// The stop ends three quitters at once; none resumes, all unwind.
static void deep_stop_while_suspended()
{
    std::queue<capy::continuation> q;
    std::stop_source ss;
    capy::async_waker w;
    outcome o;
    int down = 0;
    reach r;
    launch(q, ss.get_token(), o, top(w, down, r));
    pump(q);
    CHECK(o.calls == 0);
    ss.request_stop();
    pump(q);
    CHECK(o.calls == 1);
    CHECK(o.ec == capy::cond::canceled);
    CHECK(down == 3);
    CHECK(!r.leaf && !r.mid && !r.top);
}

//------------------------------------------------

static quitter<std::error_code> starts_then_waits(
    capy::async_waker& w, bool& started, bool& reached)
{
    started = true;
    std::ignore = co_await w.wait();
    reached = true;
    co_return std::error_code{};
}

// With no hook in the launcher, the body runs up to its first co_await.
static void stop_before_start()
{
    std::queue<capy::continuation> q;
    std::stop_source ss;
    ss.request_stop();
    capy::async_waker w;
    outcome o;
    bool started = false, reached = false;
    launch(q, ss.get_token(), o, starts_then_waits(w, started, reached));
    pump(q);
    CHECK(o.calls == 1);
    CHECK(o.ec == capy::cond::canceled);
    CHECK(started);
    CHECK(!reached);
}

//------------------------------------------------

static quitter<results<>> ready_then_yield(
    std::stop_source& ss, results<int> r, int& got, bool& reached)
{
    auto [ec, v] = co_await ready_awaitable<results<int>>{r, &ss};
    got = ec ? -1 : v;
    co_await yield_awaitable{};
    reached = true;
    co_return results<>{};
}

// A result that completed is delivered; the stop lands at the next await.
static void ready_success_then_stop_delivers()
{
    std::queue<capy::continuation> q;
    std::stop_source ss;
    outcome o;
    int got = 0;
    bool reached = false;
    launch(q, ss.get_token(), o,
        ready_then_yield(ss, results<int>{std::error_code(), 7}, got, reached));
    pump(q);
    CHECK(got == 7);
    CHECK(!reached);
    CHECK(o.calls == 1);
    CHECK(o.ec == capy::cond::canceled);
}

// A canceled result with the stop pending ends the quitter there.
static void ready_canceled_then_stop_stops()
{
    std::queue<capy::continuation> q;
    std::stop_source ss;
    outcome o;
    int got = 0;
    bool reached = false;
    launch(q, ss.get_token(), o, ready_then_yield(ss,
        results<int>{capy::error::canceled, 0}, got, reached));
    pump(q);
    CHECK(got == 0);
    CHECK(!reached);
    CHECK(o.calls == 1);
    CHECK(o.ec == capy::cond::canceled);
}

//------------------------------------------------

// Records whether it was started at all.
struct probe_awaitable
{
    bool* touched;
    bool await_ready() noexcept { *touched = true; return true; }
    void await_suspend(std::coroutine_handle<>, capy::await_context const*) noexcept {}
    void await_resume() const noexcept {}
};

static quitter<results<>> stop_then_probe(std::stop_source& ss, bool& touched)
{
    co_await ready_awaitable<results<>>{results<>{}, &ss};
    co_await probe_awaitable{&touched};
    co_return results<>{};
}

// With a stop pending at a co_await, the operation is never started,
// so nothing (a lock, a buffer) is acquired only to be dropped.
static void stop_pending_leaves_operation_untouched()
{
    std::queue<capy::continuation> q;
    std::stop_source ss;
    outcome o;
    bool touched = false;
    launch(q, ss.get_token(), o, stop_then_probe(ss, touched));
    pump(q);
    CHECK(!touched);
    CHECK(o.calls == 1);
    CHECK(o.ec == capy::cond::canceled);
}

//------------------------------------------------

static quitter<results<>> early_throw(std::stop_source& ss, bool& caught,
    bool& reached)
{
    try
    {
        co_await throwing_ready{&ss};
    }
    catch(std::runtime_error const&)
    {
        caught = true;
    }
    co_await yield_awaitable{};
    reached = true;
    co_return results<>{};
}

// An exception from an early await_resume is not a stop: the body gets it.
static void early_exception_reaches_body()
{
    std::queue<capy::continuation> q;
    std::stop_source ss;
    outcome o;
    bool caught = false, reached = false;
    launch(q, ss.get_token(), o, early_throw(ss, caught, reached));
    pump(q);
    CHECK(caught);
    CHECK(!reached);
    CHECK(o.calls == 1);
    CHECK(o.ec == capy::cond::canceled);
}

//------------------------------------------------

static quitter<results<>> wake_then_wait(
    capy::async_waker& w, int& down, bool& woken, bool& reached)
{
    counter c(down);
    auto [ec] = co_await w.wait();
    woken = !ec;
    co_await w.wait();
    reached = true;
    co_return results<>{};
}

// The wake has queued the gate; the stop arrives before it runs.
static void stop_after_wake_before_gate()
{
    std::queue<capy::continuation> q;
    std::stop_source ss;
    capy::async_waker w;
    outcome o;
    int down = 0;
    bool woken = false, reached = false;
    launch(q, ss.get_token(), o, wake_then_wait(w, down, woken, reached));
    pump(q);
    w.wake();
    CHECK(!q.empty());
    ss.request_stop();
    pump(q);
    CHECK(woken);
    CHECK(!reached);
    CHECK(down == 1);
    CHECK(o.calls == 1);
    CHECK(o.ec == capy::cond::canceled);
}

//------------------------------------------------

static quitter<results<>> inner_quitter(capy::async_waker& w, int& down)
{
    counter c(down);
    co_await w.wait();
    co_return results<>{};
}

static capy::task<results<>> middle_task(
    capy::async_waker& w, int& down, std::error_code& seen)
{
    counter c(down);
    auto [ec] = co_await inner_quitter(w, down);
    seen = ec;
    co_return results<>{ec};
}

static quitter<results<>> outer_quitter(
    capy::async_waker& w, int& down, std::error_code& seen, bool& reached)
{
    counter c(down);
    co_await middle_task(w, down, seen);
    reached = true;
    co_return results<>{};
}

// A task between quitters receives canceled as a value and decides.
static void task_between_quitters()
{
    std::queue<capy::continuation> q;
    std::stop_source ss;
    capy::async_waker w;
    outcome o;
    int down = 0;
    std::error_code seen;
    bool reached = false;
    launch(q, ss.get_token(), o, outer_quitter(w, down, seen, reached));
    pump(q);
    ss.request_stop();
    pump(q);
    CHECK(seen == capy::cond::canceled);
    CHECK(!reached);
    CHECK(down == 3);
    CHECK(o.calls == 1);
    CHECK(o.ec == capy::cond::canceled);
}

//------------------------------------------------

static quitter<results<>> thrower()
{
    co_await yield_awaitable{};
    throw std::runtime_error("boom");
}

static quitter<results<>> catches_child(bool& caught)
{
    try
    {
        co_await thrower();
    }
    catch(std::runtime_error const&)
    {
        caught = true;
    }
    co_return results<>{};
}

static void exception_propagates()
{
    std::queue<capy::continuation> q;
    std::stop_source ss;
    outcome o;
    bool caught = false;
    launch(q, ss.get_token(), o, catches_child(caught));
    pump(q);
    CHECK(caught);
    CHECK(o.calls == 1);
    CHECK(!o.ec);
}

//------------------------------------------------

// The waker's stop posts the gate to a closed executor, which abandons
// it: the chain is torn down from its owner, and no handler runs.
static void teardown_through_closed_executor()
{
    std::queue<capy::continuation> q;
    std::stop_source ss;
    capy::async_waker w;
    outcome o;
    int down = 0;
    reach r;
    bool closed = false;
    launch(q, ss.get_token(), o, top(w, down, r), &closed);
    pump(q);
    closed = true;
    ss.request_stop();
    pump(q);
    CHECK(o.calls == 0);
    CHECK(down == 3);
    CHECK(!r.leaf && !r.mid && !r.top);
}

//------------------------------------------------

static quitter<results<>> waits_once(capy::async_waker& w, int& down)
{
    counter c(down);
    co_await w.wait();
    co_return results<>{};
}

static capy::task<results<>> join_two(
    capy::async_waker& a, capy::async_waker& b, int& down, bool& resumed)
{
    std::ignore = co_await capy::when_all(waits_once(a, down), waits_once(b, down));
    resumed = true;
    co_return results<>{};
}

static void when_all_branches_stop()
{
    std::queue<capy::continuation> q;
    std::stop_source ss;
    capy::async_waker a, b;
    int down = 0;
    bool resumed = false;
    bool value = false;
    queue_executor ex{&q};
    capy::run_async(ex, ss.get_token(), std::pmr::new_delete_resource(),
        [&](results<>) { value = true; })(join_two(a, b, down, resumed));
    pump(q);
    ss.request_stop();
    pump(q);
    CHECK(resumed);
    CHECK(value);
    CHECK(down == 2);
}

//------------------------------------------------

struct atomic_counter
{
    std::atomic<int>* n;
    explicit atomic_counter(std::atomic<int>& c) : n(&c) {}
    ~atomic_counter()
    {
        ++*n;
        n->notify_one();
    }
};

static quitter<results<>> race_body(
    capy::async_waker& w, std::atomic<int>& down, bool& reached)
{
    atomic_counter c(down);
    co_await w.wait();
    co_await w.wait();
    reached = true;
    co_return results<>{};
}

// A wake and a stop from two threads, against a pool thread running
// the quitter: exactly one completion, and the frame always unwinds.
static void stop_races_wake()
{
    capy::thread_pool pool(2);
    // Outlives every iteration: the frame notifies it after the
    // increment that wakes this thread.
    std::atomic<int> down{0};
    for(int i = 0; i < 2000; ++i)
    {
        capy::async_waker w;
        std::stop_source ss;
        std::atomic<int> calls{0};
        down = 0;
        bool reached = false;
        capy::run_async(pool.get_executor(), ss.get_token(),
            [&](results<>) { ++calls; },
            [&](std::exception_ptr) { calls += 100; })(
                race_body(w, down, reached));
        std::thread waker([&] { w.wake(); });
        std::thread stopper([&] { ss.request_stop(); });
        waker.join();
        stopper.join();
        // The frame is destroyed after the handler runs.
        down.wait(0);
        CHECK(calls == 1);
        CHECK(!reached);
    }
}

int main()
{
    value_without_stop();
    deep_stop_while_suspended();
    stop_before_start();
    ready_success_then_stop_delivers();
    ready_canceled_then_stop_stops();
    stop_pending_leaves_operation_untouched();
    early_exception_reaches_body();
    stop_after_wake_before_gate();
    task_between_quitters();
    exception_propagates();
    teardown_through_closed_executor();
    when_all_branches_stop();
    stop_races_wake();
    std::printf("%s (%d failures)\n", failures ? "FAILED" : "passed", failures);
    return failures ? 1 : 0;
}
