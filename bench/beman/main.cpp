//
// Copyright (c) 2026 Steve Gerbino
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

//
// Type Erasure Benchmark
//
// Measures per-operation overhead of type erasure for async I/O
// through a null stream (no real I/O, just posts to the executor).
// 100M read_some calls per column, single thread.
//
// Column A — Awaitable + virtual stream
//   Virtual base returns an awaitable. coroutine_handle<> is already
//   type-erased, so no per-operation allocation is needed.
//   One session instantiation.
//
// Column B — Sender, monomorphized (control)
//   Concrete stream, template session. Zero allocation but requires
//   one instantiation per stream type.
//
// Column C — Sender + type-erased (any_read_sender)
//   Virtual base returns any_read_sender. connect() must heap-allocate
//   the operation state because its type is erased.
//   One session instantiation, one allocation per read_some.
//
// Column D — D4126 pure sender pipeline (no coroutine body)
//   Virtual base returns an awaitable. The D4126 bridge wraps it as
//   a sender with connect() that uses frame_cb — a synthetic
//   coroutine frame matching compiler ABI. The session loop is
//   driven by connect/start and a callback receiver, no coroutine.
//   The op_state is stored inline and reused each iteration.
//   Zero per-operation allocation, one instantiation.
//   This is the column that justifies as_sender: the awaitable is
//   consumed by the sender protocol, not by a coroutine.
//

#include "awt_any_stream.hpp"
#include "d4126_bridge.hpp"
#include "ioaw_any_stream.hpp"
#include "sender_any_stream.hpp"
#include "sender_io_env.hpp"
#include "sender_stream.hpp"

#include <boost/capy.hpp>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <latch>
#include <memory>

namespace bex = beman::execution;
namespace capy = boost::capy;



// ===================================================================
// allocation counter
// ===================================================================

static std::atomic<int64_t> g_alloc_count{0};

void* operator new(std::size_t n)
{
    g_alloc_count.fetch_add(1, std::memory_order_relaxed);
    void* p = std::malloc(n);
    if (!p)
        throw std::bad_alloc();
    return p;
}

void operator delete(void* p) noexcept
{
    std::free(p);
}

void operator delete(void* p, std::size_t) noexcept
{
    std::free(p);
}

// ===================================================================
// Column A — Awaitable + virtual stream (capy::thread_pool)
//
// Virtual base returns an IoAwaitable. The awaitable gets the
// executor from io_env (passed by capy::task's transform_awaiter)
// and calls executor.post(h). Uses capy::thread_pool — the real
// production executor. One session instantiation.
// ===================================================================

capy::task<> inner_a(ioaw_stream_base& stream)
{
    char buf[64];
    for (int i = 0; i < 10'000; ++i)
        (void)co_await stream.read_some(
            capy::mutable_buffer(buf, sizeof(buf)));
}

capy::task<> benchmark_a()
{
    ioaw_any_stream stream;

    auto before = g_alloc_count.load(std::memory_order_relaxed);
    auto start = std::chrono::steady_clock::now();

    for (int i = 0; i < 10'000; ++i)
        co_await inner_a(stream);

    auto elapsed = std::chrono::steady_clock::now() - start;
    auto after = g_alloc_count.load(std::memory_order_relaxed);
    auto us = std::chrono::duration_cast<
        std::chrono::microseconds>(elapsed).count();
    auto allocs = after - before;

    std::printf(
        "A  awaitable + virtual    %10lld us  %5.1f ns/op  "
        "%lld allocs (%4.2f/op)\n",
        static_cast<long long>(us),
        static_cast<double>(us) * 1000.0 / 100'000'000.0,
        static_cast<long long>(allocs),
        static_cast<double>(allocs) / 100'000'000.0);
}

// ===================================================================
// Column B — Sender, monomorphized (control)
//
// Concrete stream, template session function. The sender's op_state
// is inline (embeds work_item). Zero per-operation allocation, but
// each stream type requires a separate session instantiation.
// ===================================================================

using sender_stream_t = sender_stream<sender_executor>;

template <class Stream>
auto inner_b(
    Stream& stream,
    std::allocator_arg_t,
    std::pmr::polymorphic_allocator<std::byte>) -> io_task<>
{
    char buf[64];
    for (int i = 0; i < 10'000; ++i)
        (void)co_await stream.read_some(buf);
}

template <class Stream>
auto benchmark_b(
    sender_thread_pool* pool,
    Stream& stream,
    std::allocator_arg_t,
    std::pmr::polymorphic_allocator<std::byte> alloc) -> io_task<>
{
    auto before = g_alloc_count.load(std::memory_order_relaxed);
    auto start = std::chrono::steady_clock::now();

    for (int i = 0; i < 10'000; ++i)
        co_await inner_b(stream, std::allocator_arg, alloc);

    auto elapsed = std::chrono::steady_clock::now() - start;
    auto after = g_alloc_count.load(std::memory_order_relaxed);
    auto us = std::chrono::duration_cast<
        std::chrono::microseconds>(elapsed).count();
    auto allocs = after - before;

    std::printf(
        "B  sender monomorphized   %10lld us  %5.1f ns/op  "
        "%lld allocs (%4.2f/op)\n",
        static_cast<long long>(us),
        static_cast<double>(us) * 1000.0 / 100'000'000.0,
        static_cast<long long>(allocs),
        static_cast<double>(allocs) / 100'000'000.0);
}

// ===================================================================
// Column C — Sender + type-erased (any_read_sender)
//
// Virtual base returns any_read_sender which wraps a concrete sender
// in an inline buffer. When consumed, a factory function calls
// ex::connect through a type-erased receiver (callback_receiver)
// and heap-allocates the resulting operation state. This is the
// structural cost of type-erasing a sender: connect(sender, receiver)
// produces op_state<S, R> whose type depends on both arguments, so
// type erasure forces a heap allocation.
// ===================================================================

auto inner_c(
    sender_stream_base& stream,
    std::allocator_arg_t,
    std::pmr::polymorphic_allocator<std::byte>) -> io_task<>
{
    char buf[64];
    for (int i = 0; i < 10'000; ++i)
        (void)co_await stream.read_some(
            capy::mutable_buffer(buf, sizeof(buf)));
}

auto benchmark_c(
    sender_thread_pool* pool,
    sender_stream_base& stream,
    std::allocator_arg_t,
    std::pmr::polymorphic_allocator<std::byte> alloc) -> io_task<>
{
    auto before = g_alloc_count.load(std::memory_order_relaxed);
    auto start = std::chrono::steady_clock::now();

    for (int i = 0; i < 10'000; ++i)
        co_await inner_c(stream, std::allocator_arg, alloc);

    auto elapsed = std::chrono::steady_clock::now() - start;
    auto after = g_alloc_count.load(std::memory_order_relaxed);
    auto us = std::chrono::duration_cast<
        std::chrono::microseconds>(elapsed).count();
    auto allocs = after - before;

    std::printf(
        "C  sender type-erased     %10lld us  %5.1f ns/op  "
        "%lld allocs (%4.2f/op)\n",
        static_cast<long long>(us),
        static_cast<double>(us) * 1000.0 / 100'000'000.0,
        static_cast<long long>(allocs),
        static_cast<double>(allocs) / 100'000'000.0);
}

// ===================================================================
// Column D — D4126 pure sender pipeline (no coroutine body)
//
// Virtual base returns an awaitable. The D4126 bridge wraps it as
// a sender with connect() that uses frame_cb — a synthetic
// coroutine frame matching compiler ABI. The session loop is
// driven by connect/start and a callback receiver, no coroutine.
// The op_state is stored inline and reused each iteration.
// Zero per-operation allocation, one instantiation.
//
// This is the column that justifies as_sender: the awaitable is
// consumed by the sender protocol, not by a coroutine.
// ===================================================================

using awt_stream_t = awt_stream<sender_executor>;
using awt_any_stream_t = awt_any_stream<sender_executor>;
using awt_stream_base_t = awt_stream_base<sender_executor>;

struct pipeline_runner;

struct pipeline_receiver
{
    using receiver_concept = bex::receiver_t;
    pipeline_runner* runner_;

    struct env_t {};
    auto get_env() const noexcept -> env_t { return {}; }

    void set_value(std::size_t) && noexcept;
    void set_stopped() && noexcept {}

    template <class E>
    void set_error(E&&) && noexcept { std::terminate(); }
};

using d_sender_t = d4126_connect_sender<awt_stream_t::read_awaitable>;
using d_op_t = d_sender_t::op_state<pipeline_receiver>;

struct pipeline_runner
{
    awt_stream_base_t& stream_;
    int remaining_;
    std::latch& done_;
    int64_t alloc_before_;
    std::chrono::steady_clock::time_point start_time_;

    alignas(d_op_t) char op_buf_[sizeof(d_op_t)];
    bool op_active_ = false;

    void destroy_op()
    {
        if (op_active_)
        {
            reinterpret_cast<d_op_t*>(op_buf_)->~d_op_t();
            op_active_ = false;
        }
    }

    void run_one()
    {
        if (remaining_ <= 0)
        {
            destroy_op();
            auto elapsed =
                std::chrono::steady_clock::now() - start_time_;
            auto allocs =
                g_alloc_count.load(std::memory_order_relaxed)
                - alloc_before_;
            auto us = std::chrono::duration_cast<
                std::chrono::microseconds>(elapsed).count();

            std::printf(
                "D  D4126 pipeline         %10lld us  %5.1f ns/op  "
                "%lld allocs (%4.2f/op)\n",
                static_cast<long long>(us),
                static_cast<double>(us) * 1000.0 / 100'000'000.0,
                static_cast<long long>(allocs),
                static_cast<double>(allocs) / 100'000'000.0);
            done_.count_down();
            return;
        }
        --remaining_;

        destroy_op();

        char buf[64];
        auto* op = ::new (op_buf_) d_op_t(
            d_sender_t{stream_.read_some(
                capy::mutable_buffer(buf, sizeof(buf)))}
            .connect(pipeline_receiver{this}));
        op_active_ = true;
        bex::start(*op);
    }
};

void pipeline_receiver::set_value(std::size_t) && noexcept
{
    runner_->run_one();
}

// ===================================================================
// main
// ===================================================================

int main()
{
    std::printf(
        "type erasure benchmark: "
        "100,000,000 read_some calls per column\n\n");

    std::printf(
        "   %-24s %12s %10s  %s\n",
        "description", "time", "ns/op", "allocations");
    std::printf(
        "   %-24s %12s %10s  %s\n",
        "------------------------", "----------",
        "----------", "-------------------");

    // Column A — awaitable + virtual stream (capy::thread_pool)
    {
        capy::thread_pool pool(1);
        capy::run_async(pool.get_executor())(benchmark_a());
        pool.join();
    }

    // Column B — sender, monomorphized
    {
        sender_thread_pool pool(1);
        auto ex = pool.get_executor();
        sender_stream_t stream{ex};
        pool_scheduler sched{ex};
        auto* mr = capy::get_recycling_memory_resource();
        bex::sync_wait(bex::starts_on(sched,
            benchmark_b(
                &pool, stream,
                std::allocator_arg,
                std::pmr::polymorphic_allocator<std::byte>(mr))));
        pool.join();
    }

    // Column C — sender, type-erased
    {
        sender_thread_pool pool(1);
        auto ex = pool.get_executor();
        sender_any_stream<sender_stream_t> stream{ex};
        pool_scheduler sched{ex};
        auto* mr = capy::get_recycling_memory_resource();
        bex::sync_wait(bex::starts_on(sched,
            benchmark_c(
                &pool, stream,
                std::allocator_arg,
                std::pmr::polymorphic_allocator<std::byte>(mr))));
        pool.join();
    }

    // Column D — D4126 pure sender pipeline
    {
        sender_thread_pool pool(1);
        awt_any_stream_t stream{pool.get_executor()};
        std::latch done(1);
        pipeline_runner runner{
            stream, 100'000'000, done,
            g_alloc_count.load(std::memory_order_relaxed),
            std::chrono::steady_clock::now()};
        runner.run_one();
        done.wait();
        pool.join();
    }

    return 0;
}
