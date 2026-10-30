//
// Copyright (c) 2026 Michael Vandeberg
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#include <boost/capy/concept/execution_context.hpp>
#include <boost/capy/ex/abandon.hpp>
#include <boost/capy/ex/async_waker.hpp>
#include <boost/capy/ex/await_context.hpp>
#include <boost/capy/ex/execution_context.hpp>
#include <boost/capy/ex/run_async.hpp>
#include <boost/capy/ex/this_coro.hpp>
#include <boost/capy/quitter.hpp>
#include <boost/capy/results.hpp>
#include <boost/capy/task.hpp>

#include <chrono>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <stop_token>


/* Benchmarks of quitter against task.

   Four families, each run for task and for quitter:

   loop   One coroutine doing N `co_await yield_awaitable{}`. Every await
          is a real executor round trip, so this is the worst case for a
          per-suspension overhead.
   entry  A task awaiting K short children, each doing one yield. This
          measures what it costs to start and finish a child.
   ready  One coroutine doing N `co_await`s that complete without
          suspending and return a results<std::size_t>, as an
          uncontended async_mutex lock does. A quitter takes such a
          result early, to check it for canceled while a stop is
          pending, then moves it to the body.
   stop   A chain of depth D whose leaf waits on an async_waker that is
          never woken. Only the time from request_stop() until run()
          returns is measured; building and parking the chain is not.

   queue_context is a single-threaded FIFO: post() links the continuation
   through its `reserved` slot and run() drains the queue. dispatch()
   returns the handle, so a completion on the right executor resumes
   inline, as in a real I/O context.

   The first line says whether BOOST_CAPY_SYMMETRIC_TRANSFER_WORKAROUND
   is on, since it changes how every co_await suspends. Each result line
   prints `<name> <ns> ns/op`, where one op is one whole loop or ready
   run, one whole entry run, or one stop. */

using namespace boost::capy;

//-----------------------------------------------
//
// Queue Context
//
//-----------------------------------------------

class queue_context : public execution_context
{
    continuation* head_ = nullptr;
    continuation* tail_ = nullptr;

public:
    class executor_type;

    queue_context()
        : execution_context(this)
    {
    }

    ~queue_context()
    {
        // Anything still queued will never run.
        while(auto* c = pop())
            boost::capy::abandon(*c);
        shutdown();
        destroy();
    }

    executor_type
    get_executor() noexcept;

    void
    enqueue(continuation& c) noexcept
    {
        c.reserved = nullptr;
        if(tail_)
            tail_->reserved = &c;
        else
            head_ = &c;
        tail_ = &c;
    }

    std::size_t
    run_one()
    {
        auto* c = pop();
        if(!c)
            return 0;
        c->h.resume();
        return 1;
    }

    void
    run()
    {
        while(run_one())
        {
        }
    }

private:
    continuation*
    pop() noexcept
    {
        auto* c = head_;
        if(!c)
            return nullptr;
        head_ = static_cast<continuation*>(c->reserved);
        if(!head_)
            tail_ = nullptr;
        return c;
    }
};

class queue_context::executor_type
{
    queue_context* ctx_;

public:
    explicit executor_type(queue_context& ctx) noexcept
        : ctx_(&ctx)
    {
    }

    queue_context& context() const noexcept
    {
        return *ctx_;
    }

    bool operator==(executor_type const& other) const noexcept
    {
        return ctx_ == other.ctx_;
    }

    void on_work_started() const noexcept {}
    void on_work_finished() const noexcept {}

    std::coroutine_handle<> dispatch(continuation& c) const
    {
        return c.h;
    }

    void post(continuation& c) const
    {
        ctx_->enqueue(c);
    }
};

inline queue_context::executor_type
queue_context::get_executor() noexcept
{
    return executor_type(*this);
}

static_assert(Executor<queue_context::executor_type>);
static_assert(ExecutionContext<queue_context>);

//-----------------------------------------------
//
// Yield Awaitable
//
// Always suspends and posts the awaiting
// coroutine to its executor: exactly one real
// executor round trip.
//
//-----------------------------------------------

struct yield_awaitable
{
    continuation c_;

    bool await_ready() const noexcept
    {
        return false;
    }

    std::coroutine_handle<> await_suspend(
        std::coroutine_handle<> h,
        await_context const* env)
    {
        c_.h = h;
        c_.owner = env->owner;
        env->executor.post(c_);
        return std::noop_coroutine();
    }

    void await_resume() const noexcept
    {
    }
};

static_assert(ContextAwaitable<yield_awaitable>);

//-----------------------------------------------
//
// Ready Awaitable
//
// Completes without suspending, with a value in
// a results<>.
//
//-----------------------------------------------

struct ready_awaitable
{
    std::size_t n_;

    bool await_ready() const noexcept
    {
        return true;
    }

    std::coroutine_handle<> await_suspend(
        std::coroutine_handle<> h,
        await_context const*) noexcept
    {
        return h;
    }

    results<std::size_t> await_resume() const noexcept
    {
        return {std::error_code(), n_};
    }
};

static_assert(ContextAwaitable<ready_awaitable>);

//-----------------------------------------------
//
// Harness (as in bench.cpp)
//
//-----------------------------------------------

static void
print_result(char const* name, double per_op)
{
    std::cout << std::left << std::setw(40) << name
              << std::right << std::setw(10) << std::fixed << std::setprecision(1)
              << per_op << " ns/op\n";
}

template<class F>
void run_benchmark(
    char const* name,
    std::size_t iterations,
    F&& f)
{
    // Warmup
    for(std::size_t i = 0; i < 1000; ++i)
        f();

    auto start = std::chrono::high_resolution_clock::now();

    for(std::size_t i = 0; i < iterations; ++i)
        f();

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
    print_result(name, duration.count() / static_cast<double>(iterations));
}

// Like run_benchmark, but `f` returns the part of its run to count, so
// that setup is excluded.
template<class F>
void run_timed_benchmark(
    char const* name,
    std::size_t iterations,
    F&& f)
{
    for(std::size_t i = 0; i < 1000; ++i)
        f();

    std::chrono::nanoseconds total{0};
    for(std::size_t i = 0; i < iterations; ++i)
        total += f();
    print_result(name, total.count() / static_cast<double>(iterations));
}

//-----------------------------------------------
//
// Coroutines under test
//
// Each body is a template on its own return
// type, so task and every quitter run the same
// code.
//
//-----------------------------------------------

// Set when a loop body runs with a token that can be stopped, as a
// real quitter always does; bench_loop refuses to report otherwise.
bool loop_saw_stop_token = false;

template<class Coro>
Coro loop_body(std::size_t n)
{
    auto st = co_await this_coro::stop_token;
    loop_saw_stop_token = st.stop_possible();
    for(std::size_t i = 0; i < n; ++i)
        co_await yield_awaitable{};
    co_return results<>{};
}

// Read after each run, so the loop is not optimized away.
std::size_t ready_sum = 0;

template<class Coro>
Coro ready_body(std::size_t n)
{
    std::size_t sum = 0;
    for(std::size_t i = 0; i < n; ++i)
    {
        auto r = co_await ready_awaitable{i};
        sum += std::get<1>(r);
    }
    ready_sum = sum;
    co_return results<>{};
}

template<class Coro>
Coro entry_child()
{
    co_await yield_awaitable{};
    co_return results<>{};
}

template<class Coro>
task<> entry_parent(std::size_t k)
{
    for(std::size_t i = 0; i < k; ++i)
        (void)co_await entry_child<Coro>();
}

// The leaf waits on a waker nobody wakes; only a stop ends it. A task
// frame checks the error code and returns it, as cooperative code does.
template<class Coro>
Coro stop_chain(async_waker& w, int depth)
{
    if(depth == 1)
    {
        auto [ec] = co_await w.wait();
        co_return results<>{ec};
    }
    auto [ec] = co_await stop_chain<Coro>(w, depth - 1);
    if(ec)
        co_return results<>{ec};
    co_return results<>{};
}

template<class Coro>
task<> drive(Coro c)
{
    (void)co_await std::move(c);
}

//-----------------------------------------------
//
// Families
//
//-----------------------------------------------

constexpr std::size_t loop_n = 1000;
constexpr std::size_t entry_k = 1000;
constexpr int stop_depths[] = {1, 8, 64};

template<class Coro>
void bench_loop(queue_context& ctx, char const* variant)
{
    char name[64];
    std::snprintf(name, sizeof(name), "loop %s N=%zu", variant, loop_n);
    auto ex = ctx.get_executor();
    std::stop_source ss;
    loop_saw_stop_token = false;
    run_benchmark(name, 20000, [&]{
        run_async(ex, ss.get_token())(drive(loop_body<Coro>(loop_n)));
        ctx.run();
    });
    if(!loop_saw_stop_token)
    {
        std::cout << "FAIL " << name << ": ran without a stop token\n";
        std::exit(1);
    }
}

template<class Coro>
void bench_ready(queue_context& ctx, char const* variant)
{
    char name[64];
    std::snprintf(name, sizeof(name), "ready %s N=%zu", variant, loop_n);
    auto ex = ctx.get_executor();
    std::stop_source ss;
    run_benchmark(name, 20000, [&]{
        run_async(ex, ss.get_token())(drive(ready_body<Coro>(loop_n)));
        ctx.run();
    });
    if(ready_sum != loop_n * (loop_n - 1) / 2)
    {
        std::cout << "FAIL " << name << ": wrong sum\n";
        std::exit(1);
    }
}

template<class Coro>
void bench_entry(queue_context& ctx, char const* variant)
{
    char name[64];
    std::snprintf(name, sizeof(name), "entry %s K=%zu", variant, entry_k);
    auto ex = ctx.get_executor();
    run_benchmark(name, 5000, [&]{
        run_async(ex)(entry_parent<Coro>(entry_k));
        ctx.run();
    });
}

template<class Coro>
void bench_stop(queue_context& ctx, char const* variant)
{
    auto ex = ctx.get_executor();
    for(int d : stop_depths)
    {
        char name[64];
        std::snprintf(name, sizeof(name), "stop %s D=%d", variant, d);
        run_timed_benchmark(name, 10000, [&]{
            std::stop_source ss;
            async_waker w;
            run_async(ex, ss.get_token())(drive(stop_chain<Coro>(w, d)));
            ctx.run();
            auto start = std::chrono::steady_clock::now();
            ss.request_stop();
            ctx.run();
            return std::chrono::steady_clock::now() - start;
        });
    }
}

int main()
{
    queue_context ctx;

    std::cout << "BOOST_CAPY_SYMMETRIC_TRANSFER_WORKAROUND="
              << BOOST_CAPY_SYMMETRIC_TRANSFER_WORKAROUND << "\n";

    for(int rep = 0; rep < 5; ++rep)
    {
        std::cout << "=== repetition " << rep + 1 << " ===\n";

        bench_loop<task<results<>>>(ctx, "task");
        bench_loop<quitter<results<>>>(ctx, "quitter");

        bench_ready<task<results<>>>(ctx, "task");
        bench_ready<quitter<results<>>>(ctx, "quitter");

        bench_entry<task<results<>>>(ctx, "task");
        bench_entry<quitter<results<>>>(ctx, "quitter");

        bench_stop<task<results<>>>(ctx, "task-ec");
        bench_stop<quitter<results<>>>(ctx, "quitter");
    }
    return 0;
}
