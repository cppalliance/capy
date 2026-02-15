//
// Copyright (c) 2026 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

//
// Allocation Example
//
// Compares the performance of three frame allocators: the default
// recycling allocator, mimalloc, and std::allocator (no recycling).
// A 4-deep coroutine chain is invoked 2 million times with each.
//

#include <boost/capy.hpp>
#include <boost/capy/test/run_blocking.hpp>
#if BOOST_CAPY_HAS_MIMALLOC
#include <mimalloc.h>
#endif
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <iomanip>
#include <iostream>
#include <memory_resource>
#include <sstream>

// Prevent HALO from eliding coroutine frame allocations
#if defined(_MSC_VER)
# define CAPY_NOINLINE __declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
# define CAPY_NOINLINE __attribute__((noinline))
#else
# define CAPY_NOINLINE
#endif

using namespace boost::capy;

std::atomic<std::size_t> counter{0};

#if BOOST_CAPY_HAS_MIMALLOC
// Adapts mimalloc to std::pmr::memory_resource
class mi_memory_resource
    : public std::pmr::memory_resource
{
protected:
    void*
    do_allocate(
        std::size_t bytes,
        std::size_t alignment) override
    {
        void* p = mi_malloc_aligned(bytes, alignment);
        if(! p)
            throw std::bad_alloc();
        return p;
    }

    void
    do_deallocate(
        void* p,
        std::size_t,
        std::size_t alignment) override
    {
        mi_free_aligned(p, alignment);
    }

    bool
    do_is_equal(
        memory_resource const& other) const noexcept override
    {
        return this == &other;
    }
};
#endif

// These coroutines simulate a "composed operation"
// consisting of layered APIs. For example a user's
// business logic awaiting an HTTP client, awaiting
// a TLS stream, awaiting a tcp_socket

CAPY_NOINLINE task<> depth_4()
{
    counter.fetch_add(1, std::memory_order_relaxed);
    co_return;
}

CAPY_NOINLINE task<> depth_3()
{
    for(int i = 0; i < 3; ++i)
        co_await depth_4();
}

CAPY_NOINLINE task<> depth_2()
{
    for(int i = 0; i < 3; ++i)
        co_await depth_3();
}

CAPY_NOINLINE task<> depth_1()
{
    for(int i = 0; i < 5; ++i)
        co_await depth_2();
}

CAPY_NOINLINE task<> bench_loop(std::size_t n)
{
    for(std::size_t i = 0; i < n; ++i)
        co_await depth_1();
}

int main()
{
    constexpr std::size_t iterations = 2000000;

    // With recycling allocator
    counter.store(0);
    auto t0 = std::chrono::steady_clock::now();
    {
        test::blocking_context ctx;
        ctx.set_frame_allocator(get_recycling_memory_resource());
        run_async(ctx.get_executor(),
            [&] { ctx.signal_done(); })(
            bench_loop(iterations));
        ctx.run();
    }
    auto t1 = std::chrono::steady_clock::now();

#if BOOST_CAPY_HAS_MIMALLOC
    // With mimalloc
    counter.store(0);
    mi_memory_resource mi_mr;
    auto t2 = std::chrono::steady_clock::now();
    {
        test::blocking_context ctx;
        ctx.set_frame_allocator(&mi_mr);
        run_async(ctx.get_executor(),
            [&] { ctx.signal_done(); })(
            bench_loop(iterations));
        ctx.run();
    }
    auto t3 = std::chrono::steady_clock::now();
#endif

    // With std::allocator (no recycling)
    counter.store(0);
    auto t4 = std::chrono::steady_clock::now();
    {
        test::blocking_context ctx;
        run_async(ctx.get_executor(), std::allocator<std::byte>{},
            [&] { ctx.signal_done(); })(
            bench_loop(iterations));
        ctx.run();
    }
    auto t5 = std::chrono::steady_clock::now();

    auto ms_recycling =
        std::chrono::duration<double, std::milli>(t1 - t0).count();
    auto ms_standard =
        std::chrono::duration<double, std::milli>(t5 - t4).count();

    auto pct_rc_std = std::round(
        (ms_standard / ms_recycling - 1.0) * 1000.0) / 10.0;

    std::ostringstream os;
    os << std::fixed << std::setprecision(1);
    os << iterations << " iterations, "
        << "4-deep coroutine chain\n\n"
        << "  Recycling allocator: "
        << ms_recycling << " ms  (+"
        << pct_rc_std << "% vs std";
#if BOOST_CAPY_HAS_MIMALLOC
    auto ms_mimalloc =
        std::chrono::duration<double, std::milli>(t3 - t2).count();
    auto pct_mi_std = std::round(
        (ms_standard / ms_mimalloc - 1.0) * 1000.0) / 10.0;
    auto pct_rc_mi = std::round(
        (ms_mimalloc / ms_recycling - 1.0) * 1000.0) / 10.0;
    os  << ", +" << pct_rc_mi << "% vs mimalloc)\n"
        << "  mimalloc:            "
        << ms_mimalloc << " ms  (+"
        << pct_mi_std << "% vs std)\n";
#else
    os << ")\n";
#endif
    os  << "  std::allocator:      "
        << ms_standard << " ms\n";

    std::cout << os.str();

    return 0;
}
