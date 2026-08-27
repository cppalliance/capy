//
// Copyright (c) 2026 Steve Gerbino
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

//
// I/O Read Stream Benchmark
//
// Compares three execution models across three stream abstraction
// levels. 20M read_some calls per cell, single thread.
//
// Table 1: sender pipeline   (connect/start)
// Table 2: capy::task        (capy::thread_pool)
// Table 3: bex::task         (sender_thread_pool)
//
// Each table has three rows:
//   Native      — concrete stream, full visibility
//   Abstract    — virtual dispatch, implementation hidden
//   Type erased — value-type erasure
//

#include "allocation_tracker.hpp"
#include "awaitable_sender.hpp"
#include "ioaw_read_stream.hpp"
#include "ioaw_io_read_stream.hpp"
#include "repeat_until.hpp"
#include "sender_awaitable.hpp"
#include "sndr_any_read_stream.hpp"
#include "sndr_io_read_stream.hpp"
#include "sndr_read_stream.hpp"
#include "sndr_sync_read_stream.hpp"
#include "ioaw_sync_read_stream.hpp"
#include "sender_io_env.hpp"

#include <boost/capy.hpp>
#include <boost/capy/io/any_read_stream.hpp>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>

namespace bex = beman::execution;
namespace capy = boost::capy;

static counting_memory_resource g_counting_resource{
    capy::get_recycling_memory_resource()};

auto get_counting_resource() -> std::pmr::memory_resource*
{
    return &g_counting_resource;
}

// ===================================================================
// result collection
// ===================================================================

struct cell_result
{
    long long ns = 0;
    int64_t allocs = 0;
};

static constexpr int OPS_PER_CELL = 20'000'000;
static constexpr int OUTER_LOOPS = 2'000;
static constexpr int INNER_LOOPS = 10'000;

static constexpr int NUM_RUNS    = 5;
static constexpr int NUM_TABLES  = 3;
static constexpr int NUM_STREAMS = 4;
static constexpr int NUM_COLUMNS = 2;

static constexpr int SENDER_RECEIVER = 0;
static constexpr int CAPY_TASK       = 1;
static constexpr int BEMAN_TASK      = 2;

static constexpr int NATIVE_STREAM       = 0;
static constexpr int ABSTRACT_STREAM     = 1;
static constexpr int TYPE_ERASED_STREAM  = 2;
static constexpr int SYNC_STREAM         = 3;

static constexpr int NATIVE_EXEC_MODEL = 0;
static constexpr int BRIDGED_EXEC_MODEL = 1;


// ===================================================================
// Table 1: capy::task
//
// Templated session/accept coroutines instantiated with each
// stream type. The executor comes from io_env via capy::task's
// transform_awaiter.
// ===================================================================

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

// ===================================================================
// Table 1: capy::task — Column B (sender via await_sender bridge)
//
// The stream returns a sender. capy::task consumes it by wrapping
// in await_sender which bridges the sender to an IoAwaitable.
// Single pool: sender_thread_pool with sender_as_capy_executor
// adapter so capy::task can run on it.
// ===================================================================

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

// ===================================================================
// Table 2: bex::task — Column A (sender, native)
//
// Same templated pattern but using bex::task<void, io_env> coroutines on
// sender_thread_pool.
// ===================================================================

template <class Stream>
auto bex_session(
    Stream& stream,
    std::allocator_arg_t,
    std::pmr::polymorphic_allocator<std::byte>) -> bex::task<void, io_env>
{
    char buf[64];
    for (int i = 0; i < INNER_LOOPS; ++i)
        (void)co_await stream.read_some(
            capy::mutable_buffer(buf, sizeof(buf)));
}

template <class Stream>
auto bex_accept(
    Stream& stream,
    cell_result& out,
    std::allocator_arg_t,
    std::pmr::polymorphic_allocator<std::byte> alloc) -> bex::task<void, io_env>
{
    auto before = g_alloc_count.load(std::memory_order_relaxed);
    auto start = std::chrono::steady_clock::now();

    for (int i = 0; i < OUTER_LOOPS; ++i)
        co_await bex_session(stream, std::allocator_arg, alloc);

    auto elapsed = std::chrono::steady_clock::now() - start;
    auto after = g_alloc_count.load(std::memory_order_relaxed);
    out = {std::chrono::duration_cast<
        std::chrono::nanoseconds>(elapsed).count(),
        after - before};
}

// ===================================================================
// Table 2: bex::task — Column B (awaitable via as_sender_lossy bridge)
//
// The stream returns an IoAwaitable. bex::task consumes it by
// wrapping in as_sender_lossy which bridges the awaitable to a sender.
// ===================================================================

template <class Stream>
auto bex_session_ioaw(
    Stream& stream,
    std::allocator_arg_t,
    std::pmr::polymorphic_allocator<std::byte>) -> bex::task<void, io_env>
{
    char buf[64];
    for (int i = 0; i < INNER_LOOPS; ++i)
        (void)co_await capy::as_sender_lossy(
            stream.read_some(
                capy::mutable_buffer(buf, sizeof(buf))));
}

template <class Stream>
auto bex_accept_ioaw(
    Stream& stream,
    cell_result& out,
    std::allocator_arg_t,
    std::pmr::polymorphic_allocator<std::byte> alloc) -> bex::task<void, io_env>
{
    auto before = g_alloc_count.load(std::memory_order_relaxed);
    auto start = std::chrono::steady_clock::now();

    for (int i = 0; i < OUTER_LOOPS; ++i)
        co_await bex_session_ioaw(stream,
            std::allocator_arg, alloc);

    auto elapsed = std::chrono::steady_clock::now() - start;
    auto after = g_alloc_count.load(std::memory_order_relaxed);
    out = {std::chrono::duration_cast<
        std::chrono::nanoseconds>(elapsed).count(),
        after - before};
}

// ===================================================================
// main
// ===================================================================

int main()
{
    cell_result grid[NUM_RUNS + 1][NUM_TABLES][NUM_STREAMS][NUM_COLUMNS]{};

    // run 0 is a warmup pass (results discarded),
    // measured runs are 1..NUM_RUNS
    for (int run = 0; run <= NUM_RUNS; ++run)
    {


    // ---------------------------------------------------------------
    // Table 1: sender/receiver pipeline (repeat_until)
    // ---------------------------------------------------------------

    // Col A: Sender (native)


    // Native — sndr_read_stream
    {
        sender_thread_pool pool(1);
        sndr_read_stream stream{&pool};
        auto sched = pool.get_scheduler();
        int count = OPS_PER_CELL;
        char buf[64];
        auto before = g_alloc_count.load(
            std::memory_order_relaxed);
        auto start = std::chrono::steady_clock::now();
        bex::sync_wait(bex::starts_on(sched,
            repeat_until(
                bex::let_value(bex::just(), [&]() {
                    return stream.read_some(
                        capy::mutable_buffer(buf, sizeof(buf)));
                }),
                [&count]() { return --count == 0; })));
        pool.join();
        auto elapsed =
            std::chrono::steady_clock::now() - start;
        auto after = g_alloc_count.load(
            std::memory_order_relaxed);
        grid[run][SENDER_RECEIVER][NATIVE_STREAM][NATIVE_EXEC_MODEL] = {
            std::chrono::duration_cast<
                std::chrono::nanoseconds>(elapsed).count(),
            after - before};
    }

    // Abstract — sndr_io_read_stream
    {
        sender_thread_pool pool(1);
        sndr_io_read_stream_impl stream{&pool};
        auto sched = pool.get_scheduler();
        int count = OPS_PER_CELL;
        char buf[64];
        auto before = g_alloc_count.load(
            std::memory_order_relaxed);
        auto start = std::chrono::steady_clock::now();
        bex::sync_wait(bex::starts_on(sched,
            repeat_until(
                bex::let_value(bex::just(), [&]() {
                    return static_cast<sndr_io_read_stream&>(
                        stream).read_some(
                            capy::mutable_buffer(buf, sizeof(buf)));
                }),
                [&count]() { return --count == 0; })));
        pool.join();
        auto elapsed =
            std::chrono::steady_clock::now() - start;
        auto after = g_alloc_count.load(
            std::memory_order_relaxed);
        grid[run][SENDER_RECEIVER][ABSTRACT_STREAM][NATIVE_EXEC_MODEL] = {
            std::chrono::duration_cast<
                std::chrono::nanoseconds>(elapsed).count(),
            after - before};
    }

    // Type erased — sndr_any_read_stream
    {
        sender_thread_pool pool(1);
        sndr_any_read_stream stream(sndr_read_stream{&pool});
        auto sched = pool.get_scheduler();
        int count = OPS_PER_CELL;
        char buf[64];
        auto before = g_alloc_count.load(
            std::memory_order_relaxed);
        auto start = std::chrono::steady_clock::now();
        bex::sync_wait(bex::starts_on(sched,
            repeat_until(
                bex::let_value(bex::just(), [&]() {
                    return stream.read_some(
                        capy::mutable_buffer(buf, sizeof(buf)));
                }),
                [&count]() { return --count == 0; })));
        pool.join();
        auto elapsed =
            std::chrono::steady_clock::now() - start;
        auto after = g_alloc_count.load(
            std::memory_order_relaxed);
        grid[run][SENDER_RECEIVER][TYPE_ERASED_STREAM][NATIVE_EXEC_MODEL] = {
            std::chrono::duration_cast<
                std::chrono::nanoseconds>(elapsed).count(),
            after - before};
    }

    // Col B: Awaitable (via as_sender_lossy bridge)


    // Native — ioaw_read_stream
    {
        sender_thread_pool pool(1);
        ioaw_read_stream stream;
        auto sched = pool.get_scheduler();
        int count = OPS_PER_CELL;
        char buf[64];
        auto before = g_alloc_count.load(
            std::memory_order_relaxed);
        auto start = std::chrono::steady_clock::now();
        bex::sync_wait(bex::starts_on(sched,
            repeat_until(
                bex::let_value(bex::just(), [&]() {
                    return capy::as_sender_lossy(stream.read_some(
                        capy::mutable_buffer(buf, sizeof(buf))));
                }),
                [&count]() { return --count == 0; })));
        pool.join();
        auto elapsed =
            std::chrono::steady_clock::now() - start;
        auto after = g_alloc_count.load(
            std::memory_order_relaxed);
        grid[run][SENDER_RECEIVER][NATIVE_STREAM][BRIDGED_EXEC_MODEL] = {
            std::chrono::duration_cast<
                std::chrono::nanoseconds>(elapsed).count(),
            after - before};
    }

    // Abstract — ioaw_io_read_stream
    {
        sender_thread_pool pool(1);
        ioaw_io_read_stream_impl stream;
        auto sched = pool.get_scheduler();
        int count = OPS_PER_CELL;
        char buf[64];
        auto before = g_alloc_count.load(
            std::memory_order_relaxed);
        auto start = std::chrono::steady_clock::now();
        bex::sync_wait(bex::starts_on(sched,
            repeat_until(
                bex::let_value(bex::just(), [&]() {
                    return capy::as_sender_lossy(
                        static_cast<ioaw_io_read_stream&>(
                            stream).read_some(
                                capy::mutable_buffer(
                                    buf, sizeof(buf))));
                }),
                [&count]() { return --count == 0; })));
        pool.join();
        auto elapsed =
            std::chrono::steady_clock::now() - start;
        auto after = g_alloc_count.load(
            std::memory_order_relaxed);
        grid[run][SENDER_RECEIVER][ABSTRACT_STREAM][BRIDGED_EXEC_MODEL] = {
            std::chrono::duration_cast<
                std::chrono::nanoseconds>(elapsed).count(),
            after - before};
    }

    // Type erased — capy::any_read_stream
    {
        sender_thread_pool pool(1);
        ioaw_read_stream concrete;
        capy::any_read_stream stream(&concrete);
        auto sched = pool.get_scheduler();
        int count = OPS_PER_CELL;
        char buf[64];
        auto before = g_alloc_count.load(
            std::memory_order_relaxed);
        auto start = std::chrono::steady_clock::now();
        bex::sync_wait(bex::starts_on(sched,
            repeat_until(
                bex::let_value(bex::just(), [&]() {
                    return capy::as_sender_lossy(stream.read_some(
                        capy::mutable_buffer(buf, sizeof(buf))));
                }),
                [&count]() { return --count == 0; })));
        pool.join();
        auto elapsed =
            std::chrono::steady_clock::now() - start;
        auto after = g_alloc_count.load(
            std::memory_order_relaxed);
        grid[run][SENDER_RECEIVER][TYPE_ERASED_STREAM][BRIDGED_EXEC_MODEL] = {
            std::chrono::duration_cast<
                std::chrono::nanoseconds>(elapsed).count(),
            after - before};
    }


    // Synchronous — sndr_sync_read_stream (Col A)
    {
        sender_thread_pool pool(1);
        sndr_sync_read_stream stream;
        auto sched = pool.get_scheduler();
        int count = OPS_PER_CELL;
        char buf[64];
        auto before = g_alloc_count.load(
            std::memory_order_relaxed);
        auto start = std::chrono::steady_clock::now();
        bex::sync_wait(bex::starts_on(sched,
            repeat_until(
                bex::let_value(bex::just(), [&]() {
                    return stream.read_some(
                        capy::mutable_buffer(buf, sizeof(buf)));
                }),
                [&count]() { return --count == 0; })));
        pool.join();
        auto elapsed =
            std::chrono::steady_clock::now() - start;
        auto after = g_alloc_count.load(
            std::memory_order_relaxed);
        grid[run][SENDER_RECEIVER][SYNC_STREAM][NATIVE_EXEC_MODEL] = {
            std::chrono::duration_cast<
                std::chrono::nanoseconds>(elapsed).count(),
            after - before};
    }

    // Synchronous — ioaw_sync_read_stream (Col B)
    {
        sender_thread_pool pool(1);
        ioaw_sync_read_stream stream;
        auto sched = pool.get_scheduler();
        int count = OPS_PER_CELL;
        char buf[64];
        auto before = g_alloc_count.load(
            std::memory_order_relaxed);
        auto start = std::chrono::steady_clock::now();
        bex::sync_wait(bex::starts_on(sched,
            repeat_until(
                bex::let_value(bex::just(), [&]() {
                    return capy::as_sender_lossy(stream.read_some(
                        capy::mutable_buffer(buf, sizeof(buf))));
                }),
                [&count]() { return --count == 0; })));
        pool.join();
        auto elapsed =
            std::chrono::steady_clock::now() - start;
        auto after = g_alloc_count.load(
            std::memory_order_relaxed);
        grid[run][SENDER_RECEIVER][SYNC_STREAM][BRIDGED_EXEC_MODEL] = {
            std::chrono::duration_cast<
                std::chrono::nanoseconds>(elapsed).count(),
            after - before};
    }

    // ---------------------------------------------------------------
    // Table 2: capy::task (capy::thread_pool)
    // ---------------------------------------------------------------


    // Native — ioaw_read_stream
    {
        capy::thread_pool pool(1);
        ioaw_read_stream stream;
        capy::run_async(pool.get_executor())(
            capy_accept(stream, grid[run][CAPY_TASK][NATIVE_STREAM][NATIVE_EXEC_MODEL]));
        pool.join();
    }

    // Abstract — ioaw_io_read_stream
    {
        capy::thread_pool pool(1);
        ioaw_io_read_stream_impl stream;
        capy::run_async(pool.get_executor())(
            capy_accept(static_cast<ioaw_io_read_stream&>(stream),
                grid[run][CAPY_TASK][ABSTRACT_STREAM][NATIVE_EXEC_MODEL]));
        pool.join();
    }

    // Type erased — capy::any_read_stream
    {
        capy::thread_pool pool(1);
        ioaw_read_stream concrete;
        capy::any_read_stream stream(&concrete);
        capy::run_async(pool.get_executor())(
            capy_accept(stream, grid[run][CAPY_TASK][TYPE_ERASED_STREAM][NATIVE_EXEC_MODEL]));
        pool.join();
    }

    // Synchronous — ioaw_sync_read_stream
    {
        capy::thread_pool pool(1);
        ioaw_sync_read_stream stream;
        capy::run_async(pool.get_executor())(
            capy_accept(stream, grid[run][CAPY_TASK][SYNC_STREAM][NATIVE_EXEC_MODEL]));
        pool.join();
    }

    // Col B: Sender (via await_sender bridge)

    // Native — sndr_read_stream
    {
        sender_thread_pool pool(1);
        sender_as_capy_executor adapter{&pool};
        sndr_read_stream stream{&pool};
        capy::run_async(adapter)(
            capy_accept_sndr(stream, grid[run][CAPY_TASK][NATIVE_STREAM][BRIDGED_EXEC_MODEL]));
        pool.join();
    }

    // Abstract — sndr_io_read_stream
    {
        sender_thread_pool pool(1);
        sender_as_capy_executor adapter{&pool};
        sndr_io_read_stream_impl stream{&pool};
        capy::run_async(adapter)(
            capy_accept_sndr(
                static_cast<sndr_io_read_stream&>(stream),
                grid[run][CAPY_TASK][ABSTRACT_STREAM][BRIDGED_EXEC_MODEL]));
        pool.join();
    }

    // Type erased — sndr_any_read_stream
    {
        sender_thread_pool pool(1);
        sender_as_capy_executor adapter{&pool};
        sndr_any_read_stream stream(sndr_read_stream{&pool});
        capy::run_async(adapter)(
            capy_accept_sndr(stream, grid[run][CAPY_TASK][TYPE_ERASED_STREAM][BRIDGED_EXEC_MODEL]));
        pool.join();
    }

    // Synchronous — sndr_sync_read_stream
    {
        sender_thread_pool pool(1);
        sender_as_capy_executor adapter{&pool};
        sndr_sync_read_stream stream;
        capy::run_async(adapter)(
            capy_accept_sndr(stream, grid[run][CAPY_TASK][SYNC_STREAM][BRIDGED_EXEC_MODEL]));
        pool.join();
    }

    // ---------------------------------------------------------------
    // Table 3: beman::execution::task (bex::task<void, io_env>)
    // ---------------------------------------------------------------


    // Native — sndr_read_stream
    {
        sender_thread_pool pool(1);
        sndr_read_stream stream{&pool};
        auto sched = pool.get_scheduler();
        auto* mr = get_counting_resource();
        bex::sync_wait(bex::starts_on(sched,
            bex_accept(
                stream, grid[run][BEMAN_TASK][NATIVE_STREAM][NATIVE_EXEC_MODEL],
                std::allocator_arg,
                std::pmr::polymorphic_allocator<std::byte>(mr))));
        pool.join();
    }

    // Abstract — sndr_io_read_stream
    {
        sender_thread_pool pool(1);
        sndr_io_read_stream_impl stream{&pool};
        auto sched = pool.get_scheduler();
        auto* mr = get_counting_resource();
        bex::sync_wait(bex::starts_on(sched,
            bex_accept(
                static_cast<sndr_io_read_stream&>(stream),
                grid[run][BEMAN_TASK][ABSTRACT_STREAM][NATIVE_EXEC_MODEL],
                std::allocator_arg,
                std::pmr::polymorphic_allocator<std::byte>(mr))));
        pool.join();
    }

    // Type erased — sndr_any_read_stream
    {
        sender_thread_pool pool(1);
        sndr_any_read_stream stream(sndr_read_stream{&pool});
        auto sched = pool.get_scheduler();
        auto* mr = get_counting_resource();
        bex::sync_wait(bex::starts_on(sched,
            bex_accept(
                stream, grid[run][BEMAN_TASK][TYPE_ERASED_STREAM][NATIVE_EXEC_MODEL],
                std::allocator_arg,
                std::pmr::polymorphic_allocator<std::byte>(mr))));
        pool.join();
    }

    // Synchronous — sndr_sync_read_stream
    {
        sender_thread_pool pool(1);
        sndr_sync_read_stream stream;
        auto sched = pool.get_scheduler();
        auto* mr = get_counting_resource();
        bex::sync_wait(bex::starts_on(sched,
            bex_accept(
                stream, grid[run][BEMAN_TASK][SYNC_STREAM][NATIVE_EXEC_MODEL],
                std::allocator_arg,
                std::pmr::polymorphic_allocator<std::byte>(mr))));
        pool.join();
    }

    // Col B: Awaitable (via as_sender_lossy bridge)

    // Native — ioaw_read_stream
    {
        sender_thread_pool pool(1);
        ioaw_read_stream stream;
        auto sched = pool.get_scheduler();
        auto* mr = get_counting_resource();
        bex::sync_wait(bex::starts_on(sched,
            bex_accept_ioaw(
                stream, grid[run][BEMAN_TASK][NATIVE_STREAM][BRIDGED_EXEC_MODEL],
                std::allocator_arg,
                std::pmr::polymorphic_allocator<std::byte>(mr))));
        pool.join();
    }

    // Abstract — ioaw_io_read_stream
    {
        sender_thread_pool pool(1);
        ioaw_io_read_stream_impl stream;
        auto sched = pool.get_scheduler();
        auto* mr = get_counting_resource();
        bex::sync_wait(bex::starts_on(sched,
            bex_accept_ioaw(
                static_cast<ioaw_io_read_stream&>(stream),
                grid[run][BEMAN_TASK][ABSTRACT_STREAM][BRIDGED_EXEC_MODEL],
                std::allocator_arg,
                std::pmr::polymorphic_allocator<std::byte>(mr))));
        pool.join();
    }

    // Type erased — capy::any_read_stream
    {
        sender_thread_pool pool(1);
        ioaw_read_stream concrete;
        capy::any_read_stream stream(&concrete);
        auto sched = pool.get_scheduler();
        auto* mr = get_counting_resource();
        bex::sync_wait(bex::starts_on(sched,
            bex_accept_ioaw(
                stream, grid[run][BEMAN_TASK][TYPE_ERASED_STREAM][BRIDGED_EXEC_MODEL],
                std::allocator_arg,
                std::pmr::polymorphic_allocator<std::byte>(mr))));
        pool.join();
    }

    // Synchronous — ioaw_sync_read_stream
    {
        sender_thread_pool pool(1);
        ioaw_sync_read_stream stream;
        auto sched = pool.get_scheduler();
        auto* mr = get_counting_resource();
        bex::sync_wait(bex::starts_on(sched,
            bex_accept_ioaw(
                stream, grid[run][BEMAN_TASK][SYNC_STREAM][BRIDGED_EXEC_MODEL],
                std::allocator_arg,
                std::pmr::polymorphic_allocator<std::byte>(mr))));
        pool.join();
    }

    } // for (run)

    // ---------------------------------------------------------------
    // Print results
    // ---------------------------------------------------------------

    constexpr double ops = static_cast<double>(OPS_PER_CELL);

    std::printf(
        "I/O read stream benchmark: "
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
        "beman::execution::task",
        BEMAN_TASK,
        "A: sender (native)",
        "B: awaitable (bridge)");

    return 0;
}
