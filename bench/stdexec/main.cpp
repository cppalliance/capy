//
// Copyright (c) 2026 Steve Gerbino
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

//
// I/O Read Stream Benchmark (stdexec edition)
//
// Compares three execution models across four stream
// abstraction levels. 20M read_some calls per cell,
// single thread.
//
// Table 1: sender pipeline   (connect/start)
// Table 2: capy::task        (capy::thread_pool)
// Table 3: exec::task        (exec::static_thread_pool)
//
// Each table has four rows:
//   Native      - concrete stream, full visibility
//   Abstract    - virtual dispatch, implementation hidden
//   Type erased - value-type erasure (exec::any_sender)
//   Synchronous - no scheduler trip
//

#include "allocation_tracker.hpp"
#include "awaitable_sender.hpp"
#include "ioaw_io_read_stream.hpp"
#include "ioaw_read_stream.hpp"
#include "ioaw_sync_read_stream.hpp"
#include <exec/repeat_until.hpp>
#include "sender_awaitable.hpp"
#include "sender_io_env.hpp"
#include "sndr_any_read_stream.hpp"
#include "sndr_io_read_stream.hpp"
#include "sndr_read_stream.hpp"
#include "sndr_sync_read_stream.hpp"

#include <boost/capy.hpp>
#include <boost/capy/io/any_read_stream.hpp>

#include <exec/function.hpp>
#include <exec/static_thread_pool.hpp>
#include <exec/task.hpp>
#include <stdexec/execution.hpp>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <latch>
#include <memory>

namespace capy = boost::capy;

static counting_memory_resource g_counting_resource{
    capy::get_recycling_memory_resource()};

auto get_counting_resource() -> std::pmr::memory_resource*
{
    return &g_counting_resource;
}

struct cell_result
{
    long long ns = 0;
    int64_t allocs = 0;
};

static constexpr int OPS_PER_CELL = 20'000'000;
static constexpr int OUTER_LOOPS  = 2'000;
static constexpr int INNER_LOOPS  = 10'000;

static constexpr int NUM_RUNS    = 5;
static constexpr int NUM_TABLES  = 3;
static constexpr int NUM_STREAMS = 4;
static constexpr int NUM_COLUMNS = 2;

static constexpr int SENDER_RECEIVER = 0;
static constexpr int CAPY_TASK       = 1;
static constexpr int EXEC_TASK       = 2;

static constexpr int NATIVE_STREAM      = 0;
static constexpr int ABSTRACT_STREAM    = 1;
static constexpr int TYPE_ERASED_STREAM = 2;
static constexpr int SYNC_STREAM        = 3;

static constexpr int NATIVE_EXEC_MODEL  = 0;
static constexpr int BRIDGED_EXEC_MODEL = 1;

// -----------------------------------------------------------
// Table 2: capy::task - Column A (awaitable, native)
// -----------------------------------------------------------

template <class Stream>
capy::task<> capy_session(Stream& stream)
{
    char buf[64];
    for (int i = 0; i < INNER_LOOPS; ++i)
        (void)co_await stream.read_some(
            capy::mutable_buffer(buf, sizeof(buf)));
}

template <class Stream>
capy::task<> capy_accept(Stream& stream, cell_result& out)
{
    auto before = g_alloc_count.load(std::memory_order_relaxed);
    auto start = std::chrono::steady_clock::now();

    for (int i = 0; i < OUTER_LOOPS; ++i)
        co_await capy_session(stream);

    auto elapsed = std::chrono::steady_clock::now() - start;
    auto after = g_alloc_count.load(std::memory_order_relaxed);
    out = {std::chrono::duration_cast<
        std::chrono::nanoseconds>(elapsed).count(),
        after - before};
}

// -----------------------------------------------------------
// Table 2: capy::task - Column B (sender via await_sender)
// -----------------------------------------------------------

template <class Stream>
capy::task<> capy_session_sndr(Stream& stream)
{
    char buf[64];
    for (int i = 0; i < INNER_LOOPS; ++i)
        (void)co_await capy::await_sender(
            stream.read_some(
                capy::mutable_buffer(buf, sizeof(buf))));
}

template <class Stream>
capy::task<> capy_accept_sndr(Stream& stream, cell_result& out)
{
    auto before = g_alloc_count.load(std::memory_order_relaxed);
    auto start = std::chrono::steady_clock::now();

    for (int i = 0; i < OUTER_LOOPS; ++i)
        co_await capy_session_sndr(stream);

    auto elapsed = std::chrono::steady_clock::now() - start;
    auto after = g_alloc_count.load(std::memory_order_relaxed);
    out = {std::chrono::duration_cast<
        std::chrono::nanoseconds>(elapsed).count(),
        after - before};
}

// -----------------------------------------------------------
// Table 3: exec::task - Column A (sender, native)
// -----------------------------------------------------------

template <class Stream>
auto exec_session(Stream& stream) -> exec::task<void>
{
    char buf[64];
    for (int i = 0; i < INNER_LOOPS; ++i)
        (void)co_await stream.read_some(
            capy::mutable_buffer(buf, sizeof(buf)));
}

template <class Stream>
auto exec_accept(Stream& stream, cell_result& out)
    -> exec::task<void>
{
    auto before = g_alloc_count.load(std::memory_order_relaxed);
    auto start = std::chrono::steady_clock::now();

    for (int i = 0; i < OUTER_LOOPS; ++i)
        co_await exec_session(stream);

    auto elapsed = std::chrono::steady_clock::now() - start;
    auto after = g_alloc_count.load(std::memory_order_relaxed);
    out = {std::chrono::duration_cast<
        std::chrono::nanoseconds>(elapsed).count(),
        after - before};
}

// -----------------------------------------------------------
// Table 3: exec::task - Column B (awaitable via as_sender)
//
// exec::task's promise env carries only get_start_scheduler
// (type-erased __any_scheduler), which does not propagate
// custom queries like get_io_executor. We supply the
// executor explicitly and inject it into each per-call env
// via write_env so the awaitable_sender bridge can find it.
// -----------------------------------------------------------

template <class Stream>
auto exec_session_ioaw(
    Stream& stream,
    sender_as_capy_executor ex) -> exec::task<void>
{
    char buf[64];
    for (int i = 0; i < INNER_LOOPS; ++i)
        (void)co_await stdexec::write_env(
            capy::as_sender(
                stream.read_some(
                    capy::mutable_buffer(buf, sizeof(buf)))),
            stdexec::prop{capy::get_io_executor, ex});
}

template <class Stream>
auto exec_accept_ioaw(
    Stream& stream,
    sender_as_capy_executor ex,
    cell_result& out) -> exec::task<void>
{
    auto before = g_alloc_count.load(std::memory_order_relaxed);
    auto start = std::chrono::steady_clock::now();

    for (int i = 0; i < OUTER_LOOPS; ++i)
        co_await exec_session_ioaw(stream, ex);

    auto elapsed = std::chrono::steady_clock::now() - start;
    auto after = g_alloc_count.load(std::memory_order_relaxed);
    out = {std::chrono::duration_cast<
        std::chrono::nanoseconds>(elapsed).count(),
        after - before};
}

int main()
{
    cell_result grid[NUM_RUNS + 1][NUM_TABLES][NUM_STREAMS][NUM_COLUMNS]{};

    // run 0 is a warmup pass (results discarded);
    // measured runs are 1..NUM_RUNS
    for (int run = 0; run <= NUM_RUNS; ++run)
    {

    // -----------------------------------------------------------
    // Table 1: sender/receiver pipeline (repeat_until)
    //
    // All Table 1 cells use exec::static_thread_pool(2) instead of (1).
    // exec::repeat_until synchronously emplaces iteration N+1 inside
    // iteration N's set_value cascade. With a single-worker pool the
    // worker is stuck in that cascade and can't dispatch the post the
    // cascade just queued, deadlocking. A second worker drains the
    // queue while the first is in the cascade. Tables 2 and 3 stay at
    // pool(1) because co_await suspension releases the worker between
    // iterations and avoids the issue.
    // -----------------------------------------------------------

    // Col A: Sender (native)

    // Native - sndr_read_stream
    {
        exec::static_thread_pool pool(2);
        static_pool_context ctx;
        sndr_read_stream stream{&pool};
        pool_scheduler sched{&pool, &ctx};
        int count = OPS_PER_CELL;
        char buf[64];
        auto before = g_alloc_count.load(std::memory_order_relaxed);
        auto start = std::chrono::steady_clock::now();
        stdexec::sync_wait(stdexec::starts_on(sched,
            exec::repeat_until(
                stdexec::let_value(stdexec::just(), [&]() {
                    return stream.read_some(
                        capy::mutable_buffer(buf, sizeof(buf)));
                })
                | stdexec::then([&count](std::size_t) { return --count == 0; }))));
        pool.request_stop();
        auto elapsed = std::chrono::steady_clock::now() - start;
        auto after = g_alloc_count.load(std::memory_order_relaxed);
        grid[run][SENDER_RECEIVER][NATIVE_STREAM][NATIVE_EXEC_MODEL] = {
            std::chrono::duration_cast<
                std::chrono::nanoseconds>(elapsed).count(),
            after - before};
    }

    // Abstract - sndr_io_read_stream
    {
        exec::static_thread_pool pool(2);
        static_pool_context ctx;
        sndr_io_read_stream_impl stream{&pool};
        pool_scheduler sched{&pool, &ctx};
        int count = OPS_PER_CELL;
        char buf[64];
        auto* mr = get_counting_resource();
        std::pmr::polymorphic_allocator<std::byte> alloc(mr);
        auto before = g_alloc_count.load(std::memory_order_relaxed);
        auto start = std::chrono::steady_clock::now();
        stdexec::sync_wait(
            stdexec::write_env(
                stdexec::starts_on(sched,
                    exec::repeat_until(
                        stdexec::let_value(stdexec::just(), [&]() {
                            return static_cast<sndr_io_read_stream&>(
                                stream).read_some(
                                    capy::mutable_buffer(buf, sizeof(buf)));
                        })
                        | stdexec::then([&count](std::size_t) { return --count == 0; }))),
                stdexec::prop{exec::get_frame_allocator, alloc}));
        pool.request_stop();
        auto elapsed = std::chrono::steady_clock::now() - start;
        auto after = g_alloc_count.load(std::memory_order_relaxed);
        grid[run][SENDER_RECEIVER][ABSTRACT_STREAM][NATIVE_EXEC_MODEL] = {
            std::chrono::duration_cast<
                std::chrono::nanoseconds>(elapsed).count(),
            after - before};
    }

    // Type erased - sndr_any_read_stream
    {
        exec::static_thread_pool pool(2);
        static_pool_context ctx;
        sndr_any_read_stream stream(sndr_read_stream{&pool});
        pool_scheduler sched{&pool, &ctx};
        int count = OPS_PER_CELL;
        char buf[64];
        auto* mr = get_counting_resource();
        std::pmr::polymorphic_allocator<std::byte> alloc(mr);
        auto before = g_alloc_count.load(std::memory_order_relaxed);
        auto start = std::chrono::steady_clock::now();
        stdexec::sync_wait(
            stdexec::write_env(
                stdexec::starts_on(sched,
                    exec::repeat_until(
                        stdexec::let_value(stdexec::just(), [&]() {
                            return stream.read_some(
                                capy::mutable_buffer(buf, sizeof(buf)));
                        })
                        | stdexec::then([&count](std::size_t) { return --count == 0; }))),
                stdexec::prop{exec::get_frame_allocator, alloc}));
        pool.request_stop();
        auto elapsed = std::chrono::steady_clock::now() - start;
        auto after = g_alloc_count.load(std::memory_order_relaxed);
        grid[run][SENDER_RECEIVER][TYPE_ERASED_STREAM][NATIVE_EXEC_MODEL] = {
            std::chrono::duration_cast<
                std::chrono::nanoseconds>(elapsed).count(),
            after - before};
    }

    // Synchronous - sndr_sync_read_stream (Col A)
    {
        exec::static_thread_pool pool(2);
        static_pool_context ctx;
        sndr_sync_read_stream stream;
        pool_scheduler sched{&pool, &ctx};
        int count = OPS_PER_CELL;
        char buf[64];
        auto before = g_alloc_count.load(std::memory_order_relaxed);
        auto start = std::chrono::steady_clock::now();
        stdexec::sync_wait(stdexec::starts_on(sched,
            exec::repeat_until(
                stdexec::let_value(stdexec::just(), [&]() {
                    return stream.read_some(
                        capy::mutable_buffer(buf, sizeof(buf)));
                })
                | stdexec::then([&count](std::size_t) { return --count == 0; }))));
        pool.request_stop();
        auto elapsed = std::chrono::steady_clock::now() - start;
        auto after = g_alloc_count.load(std::memory_order_relaxed);
        grid[run][SENDER_RECEIVER][SYNC_STREAM][NATIVE_EXEC_MODEL] = {
            std::chrono::duration_cast<
                std::chrono::nanoseconds>(elapsed).count(),
            after - before};
    }

    // Col B: Awaitable (via as_sender bridge)

    // Native - ioaw_read_stream
    {
        exec::static_thread_pool pool(2);
        static_pool_context ctx;
        ioaw_read_stream stream;
        pool_scheduler sched{&pool, &ctx};
        sender_as_capy_executor adapter{&pool, &ctx};
        int count = OPS_PER_CELL;
        char buf[64];
        auto before = g_alloc_count.load(std::memory_order_relaxed);
        auto start = std::chrono::steady_clock::now();
        stdexec::sync_wait(stdexec::starts_on(sched,
            exec::repeat_until(
                stdexec::let_value(stdexec::just(), [&]() {
                    return stdexec::write_env(
                        capy::as_sender(stream.read_some(
                            capy::mutable_buffer(buf, sizeof(buf)))),
                        stdexec::prop{capy::get_io_executor, adapter});
                })
                | stdexec::then([&count](std::size_t) { return --count == 0; }))));
        pool.request_stop();
        auto elapsed = std::chrono::steady_clock::now() - start;
        auto after = g_alloc_count.load(std::memory_order_relaxed);
        grid[run][SENDER_RECEIVER][NATIVE_STREAM][BRIDGED_EXEC_MODEL] = {
            std::chrono::duration_cast<
                std::chrono::nanoseconds>(elapsed).count(),
            after - before};
    }

    // Abstract - ioaw_io_read_stream
    {
        exec::static_thread_pool pool(2);
        static_pool_context ctx;
        ioaw_io_read_stream_impl stream;
        pool_scheduler sched{&pool, &ctx};
        sender_as_capy_executor adapter{&pool, &ctx};
        int count = OPS_PER_CELL;
        char buf[64];
        auto before = g_alloc_count.load(std::memory_order_relaxed);
        auto start = std::chrono::steady_clock::now();
        stdexec::sync_wait(stdexec::starts_on(sched,
            exec::repeat_until(
                stdexec::let_value(stdexec::just(), [&]() {
                    return stdexec::write_env(
                        capy::as_sender(
                            static_cast<ioaw_io_read_stream&>(
                                stream).read_some(
                                    capy::mutable_buffer(
                                        buf, sizeof(buf)))),
                        stdexec::prop{capy::get_io_executor, adapter});
                })
                | stdexec::then([&count](std::size_t) { return --count == 0; }))));
        pool.request_stop();
        auto elapsed = std::chrono::steady_clock::now() - start;
        auto after = g_alloc_count.load(std::memory_order_relaxed);
        grid[run][SENDER_RECEIVER][ABSTRACT_STREAM][BRIDGED_EXEC_MODEL] = {
            std::chrono::duration_cast<
                std::chrono::nanoseconds>(elapsed).count(),
            after - before};
    }

    // Type erased - capy::any_read_stream
    {
        exec::static_thread_pool pool(2);
        static_pool_context ctx;
        ioaw_read_stream concrete;
        capy::any_read_stream stream(&concrete);
        pool_scheduler sched{&pool, &ctx};
        sender_as_capy_executor adapter{&pool, &ctx};
        int count = OPS_PER_CELL;
        char buf[64];
        auto before = g_alloc_count.load(std::memory_order_relaxed);
        auto start = std::chrono::steady_clock::now();
        stdexec::sync_wait(stdexec::starts_on(sched,
            exec::repeat_until(
                stdexec::let_value(stdexec::just(), [&]() {
                    return stdexec::write_env(
                        capy::as_sender(stream.read_some(
                            capy::mutable_buffer(buf, sizeof(buf)))),
                        stdexec::prop{capy::get_io_executor, adapter});
                })
                | stdexec::then([&count](std::size_t) { return --count == 0; }))));
        pool.request_stop();
        auto elapsed = std::chrono::steady_clock::now() - start;
        auto after = g_alloc_count.load(std::memory_order_relaxed);
        grid[run][SENDER_RECEIVER][TYPE_ERASED_STREAM][BRIDGED_EXEC_MODEL] = {
            std::chrono::duration_cast<
                std::chrono::nanoseconds>(elapsed).count(),
            after - before};
    }

    // Synchronous - ioaw_sync_read_stream (Col B)
    {
        exec::static_thread_pool pool(2);
        static_pool_context ctx;
        ioaw_sync_read_stream stream;
        pool_scheduler sched{&pool, &ctx};
        sender_as_capy_executor adapter{&pool, &ctx};
        int count = OPS_PER_CELL;
        char buf[64];
        auto before = g_alloc_count.load(std::memory_order_relaxed);
        auto start = std::chrono::steady_clock::now();
        stdexec::sync_wait(stdexec::starts_on(sched,
            exec::repeat_until(
                stdexec::let_value(stdexec::just(), [&]() {
                    return stdexec::write_env(
                        capy::as_sender(stream.read_some(
                            capy::mutable_buffer(buf, sizeof(buf)))),
                        stdexec::prop{capy::get_io_executor, adapter});
                })
                | stdexec::then([&count](std::size_t) { return --count == 0; }))));
        pool.request_stop();
        auto elapsed = std::chrono::steady_clock::now() - start;
        auto after = g_alloc_count.load(std::memory_order_relaxed);
        grid[run][SENDER_RECEIVER][SYNC_STREAM][BRIDGED_EXEC_MODEL] = {
            std::chrono::duration_cast<
                std::chrono::nanoseconds>(elapsed).count(),
            after - before};
    }

    // -----------------------------------------------------------
    // Table 2: capy::task (capy::thread_pool)
    // -----------------------------------------------------------

    // Col A: Awaitable (native)

    // Native - ioaw_read_stream
    {
        capy::thread_pool pool(1);
        ioaw_read_stream stream;
        capy::run_async(pool.get_executor())(
            capy_accept(stream,
                grid[run][CAPY_TASK][NATIVE_STREAM][NATIVE_EXEC_MODEL]));
        pool.join();
    }

    // Abstract - ioaw_io_read_stream
    {
        capy::thread_pool pool(1);
        ioaw_io_read_stream_impl stream;
        capy::run_async(pool.get_executor())(
            capy_accept(static_cast<ioaw_io_read_stream&>(stream),
                grid[run][CAPY_TASK][ABSTRACT_STREAM][NATIVE_EXEC_MODEL]));
        pool.join();
    }

    // Type erased - capy::any_read_stream
    {
        capy::thread_pool pool(1);
        ioaw_read_stream concrete;
        capy::any_read_stream stream(&concrete);
        capy::run_async(pool.get_executor())(
            capy_accept(stream,
                grid[run][CAPY_TASK][TYPE_ERASED_STREAM][NATIVE_EXEC_MODEL]));
        pool.join();
    }

    // Synchronous - ioaw_sync_read_stream
    {
        capy::thread_pool pool(1);
        ioaw_sync_read_stream stream;
        capy::run_async(pool.get_executor())(
            capy_accept(stream,
                grid[run][CAPY_TASK][SYNC_STREAM][NATIVE_EXEC_MODEL]));
        pool.join();
    }

    // Col B: Sender (via await_sender bridge)

    // Native - sndr_read_stream
    {
        exec::static_thread_pool pool(1);
        static_pool_context ctx;
        sender_as_capy_executor adapter{&pool, &ctx};
        sndr_read_stream stream{&pool};
        std::latch done(1);
        capy::run_async(adapter,
            [&done](auto&&...) noexcept { done.count_down(); })(
            capy_accept_sndr(stream,
                grid[run][CAPY_TASK][NATIVE_STREAM][BRIDGED_EXEC_MODEL]));
        done.wait();
        pool.request_stop();
    }

    // Abstract - sndr_io_read_stream
    {
        exec::static_thread_pool pool(1);
        static_pool_context ctx;
        sender_as_capy_executor adapter{&pool, &ctx};
        sndr_io_read_stream_impl stream{&pool};
        std::latch done(1);
        capy::run_async(adapter,
            [&done](auto&&...) noexcept { done.count_down(); })(
            capy_accept_sndr(
                static_cast<sndr_io_read_stream&>(stream),
                grid[run][CAPY_TASK][ABSTRACT_STREAM][BRIDGED_EXEC_MODEL]));
        done.wait();
        pool.request_stop();
    }

    // Type erased - sndr_any_read_stream
    {
        exec::static_thread_pool pool(1);
        static_pool_context ctx;
        sender_as_capy_executor adapter{&pool, &ctx};
        sndr_any_read_stream stream(sndr_read_stream{&pool});
        std::latch done(1);
        capy::run_async(adapter,
            [&done](auto&&...) noexcept { done.count_down(); })(
            capy_accept_sndr(stream,
                grid[run][CAPY_TASK][TYPE_ERASED_STREAM][BRIDGED_EXEC_MODEL]));
        done.wait();
        pool.request_stop();
    }

    // Synchronous - sndr_sync_read_stream
    {
        exec::static_thread_pool pool(1);
        static_pool_context ctx;
        sender_as_capy_executor adapter{&pool, &ctx};
        sndr_sync_read_stream stream;
        std::latch done(1);
        capy::run_async(adapter,
            [&done](auto&&...) noexcept { done.count_down(); })(
            capy_accept_sndr(stream,
                grid[run][CAPY_TASK][SYNC_STREAM][BRIDGED_EXEC_MODEL]));
        done.wait();
        pool.request_stop();
    }

    // -----------------------------------------------------------
    // Table 3: exec::task (exec::static_thread_pool)
    // -----------------------------------------------------------

    // Col A: Sender (native)

    // Native - sndr_read_stream
    {
        exec::static_thread_pool pool(1);
        static_pool_context ctx;
        pool_scheduler sched{&pool, &ctx};
        sndr_read_stream stream{&pool};
        stdexec::sync_wait(stdexec::starts_on(sched,
            exec_accept(stream,
                grid[run][EXEC_TASK][NATIVE_STREAM][NATIVE_EXEC_MODEL])));
        pool.request_stop();
    }

    // Abstract - sndr_io_read_stream
    {
        exec::static_thread_pool pool(1);
        static_pool_context ctx;
        pool_scheduler sched{&pool, &ctx};
        sndr_io_read_stream_impl stream{&pool};
        auto* mr = get_counting_resource();
        std::pmr::polymorphic_allocator<std::byte> alloc(mr);
        stdexec::sync_wait(
            stdexec::write_env(
                stdexec::starts_on(sched,
                    exec_accept(
                        static_cast<sndr_io_read_stream&>(stream),
                        grid[run][EXEC_TASK][ABSTRACT_STREAM][NATIVE_EXEC_MODEL])),
                stdexec::prop{exec::get_frame_allocator, alloc}));
        pool.request_stop();
    }

    // Type erased - sndr_any_read_stream
    {
        exec::static_thread_pool pool(1);
        static_pool_context ctx;
        pool_scheduler sched{&pool, &ctx};
        sndr_any_read_stream stream(sndr_read_stream{&pool});
        auto* mr = get_counting_resource();
        std::pmr::polymorphic_allocator<std::byte> alloc(mr);
        stdexec::sync_wait(
            stdexec::write_env(
                stdexec::starts_on(sched,
                    exec_accept(stream,
                        grid[run][EXEC_TASK][TYPE_ERASED_STREAM][NATIVE_EXEC_MODEL])),
                stdexec::prop{exec::get_frame_allocator, alloc}));
        pool.request_stop();
    }

    // Synchronous - sndr_sync_read_stream
    {
        exec::static_thread_pool pool(1);
        static_pool_context ctx;
        pool_scheduler sched{&pool, &ctx};
        sndr_sync_read_stream stream;
        stdexec::sync_wait(stdexec::starts_on(sched,
            exec_accept(stream,
                grid[run][EXEC_TASK][SYNC_STREAM][NATIVE_EXEC_MODEL])));
        pool.request_stop();
    }

    // Col B: Awaitable (via as_sender bridge)

    // Native - ioaw_read_stream
    {
        exec::static_thread_pool pool(1);
        static_pool_context ctx;
        pool_scheduler sched{&pool, &ctx};
        sender_as_capy_executor adapter{&pool, &ctx};
        ioaw_read_stream stream;
        stdexec::sync_wait(stdexec::starts_on(sched,
            exec_accept_ioaw(stream, adapter,
                grid[run][EXEC_TASK][NATIVE_STREAM][BRIDGED_EXEC_MODEL])));
        pool.request_stop();
    }

    // Abstract - ioaw_io_read_stream
    {
        exec::static_thread_pool pool(1);
        static_pool_context ctx;
        pool_scheduler sched{&pool, &ctx};
        sender_as_capy_executor adapter{&pool, &ctx};
        ioaw_io_read_stream_impl stream;
        stdexec::sync_wait(stdexec::starts_on(sched,
            exec_accept_ioaw(
                static_cast<ioaw_io_read_stream&>(stream),
                adapter,
                grid[run][EXEC_TASK][ABSTRACT_STREAM][BRIDGED_EXEC_MODEL])));
        pool.request_stop();
    }

    // Type erased - capy::any_read_stream
    {
        exec::static_thread_pool pool(1);
        static_pool_context ctx;
        pool_scheduler sched{&pool, &ctx};
        sender_as_capy_executor adapter{&pool, &ctx};
        ioaw_read_stream concrete;
        capy::any_read_stream stream(&concrete);
        auto* mr = get_counting_resource();
        std::pmr::polymorphic_allocator<std::byte> alloc(mr);
        stdexec::sync_wait(
            stdexec::write_env(
                stdexec::starts_on(sched,
                    exec_accept_ioaw(stream, adapter,
                        grid[run][EXEC_TASK][TYPE_ERASED_STREAM][BRIDGED_EXEC_MODEL])),
                stdexec::prop{exec::get_frame_allocator, alloc}));
        pool.request_stop();
    }

    // Synchronous - ioaw_sync_read_stream
    {
        exec::static_thread_pool pool(1);
        static_pool_context ctx;
        pool_scheduler sched{&pool, &ctx};
        sender_as_capy_executor adapter{&pool, &ctx};
        ioaw_sync_read_stream stream;
        stdexec::sync_wait(stdexec::starts_on(sched,
            exec_accept_ioaw(stream, adapter,
                grid[run][EXEC_TASK][SYNC_STREAM][BRIDGED_EXEC_MODEL])));
        pool.request_stop();
    }

    } // for (run)

    // -----------------------------------------------------------
    // Print results
    // -----------------------------------------------------------

    constexpr double ops = static_cast<double>(OPS_PER_CELL);

    std::printf(
        "I/O read stream benchmark (stdexec): "
        "%d read_some calls per cell, %d runs\n",
        OPS_PER_CELL, NUM_RUNS);

    char const* row_labels[] = {
        "Native", "Abstract", "Type-erased", "Synchronous"};

    auto print_table = [&](
        char const* title,
        int table,
        char const* col_a_label,
        char const* col_b_label)
    {
        std::printf("\n  %s\n", title);
        std::printf(
            "  %-18s  %-30s  %-30s\n",
            "", col_a_label, col_b_label);
        std::printf(
            "  %-18s  %-30s  %-30s\n",
            "------------------",
            "------------------------------",
            "------------------------------");

        for (int s = 0; s < NUM_STREAMS; ++s)
        {
            double sum[NUM_COLUMNS]{};
            double sum2[NUM_COLUMNS]{};
            double al[NUM_COLUMNS]{};
            for (int c = 0; c < NUM_COLUMNS; ++c)
            {
                for (int r = 1; r <= NUM_RUNS; ++r)
                {
                    double v = static_cast<double>(
                        grid[r][table][s][c].ns) / ops;
                    sum[c] += v;
                    sum2[c] += v * v;
                    al[c] += static_cast<double>(
                        grid[r][table][s][c].allocs);
                }
            }

            double mean[NUM_COLUMNS];
            double sd[NUM_COLUMNS];
            double mean_al[NUM_COLUMNS];
            for (int c = 0; c < NUM_COLUMNS; ++c)
            {
                mean[c] = sum[c] / NUM_RUNS;
                double var = sum2[c] / NUM_RUNS -
                    mean[c] * mean[c];
                sd[c] = std::sqrt(var > 0 ? var : 0);
                mean_al[c] = al[c] / (NUM_RUNS * ops);
            }

            std::printf(
                "  %-18s"
                "  %5.1f +/- %3.1f ns/op  %1.0f al/op"
                "    %5.1f +/- %3.1f ns/op  %1.0f al/op"
                "\n",
                row_labels[s],
                mean[0], sd[0], mean_al[0],
                mean[1], sd[1], mean_al[1]);
        }
    };

    print_table(
        "sender/receiver pipeline",
        SENDER_RECEIVER,
        "A: sender (native)",
        "B: awaitable (bridge)");

    print_table(
        "capy::task",
        CAPY_TASK,
        "A: awaitable (native)",
        "B: sender (bridge)");

    print_table(
        "exec::task",
        EXEC_TASK,
        "A: sender (native)",
        "B: awaitable (bridge)");

    return 0;
}
