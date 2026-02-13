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
// Compares the performance of the default recycling frame allocator
// against std::allocator (no recycling). A 4-deep coroutine chain
// is invoked 20 million times using test::run_blocking, once with
// each allocator.
//

#include <boost/capy.hpp>
#include <boost/capy/test/run_blocking.hpp>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <iomanip>
#include <iostream>

using namespace boost::capy;

std::atomic<std::size_t> counter{0};

// These coroutines simulate a "composed operation"
// consisting of layered APIs. For example a user's
// business logic awaiting an HTTP client, awaiting
// a TLS stream, awaiting a tcp_socket

task<> depth_4()
{
    counter.fetch_add(1, std::memory_order_relaxed);
    co_return;
}

task<> depth_3()
{
    for(int i = 0; i < 3; ++i)
        co_await depth_4();
}

task<> depth_2()
{
    for(int i = 0; i < 3; ++i)
        co_await depth_3();
}

task<> depth_1()
{
    for(int i = 0; i < 5; ++i)
        co_await depth_2();
}

task<> bench_loop(std::size_t n)
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

    // With std::allocator (no recycling)
    counter.store(0);
    auto t2 = std::chrono::steady_clock::now();
    {
        test::blocking_context ctx;
        run_async(ctx.get_executor(), std::allocator<std::byte>{},
            [&] { ctx.signal_done(); })(
            bench_loop(iterations));
        ctx.run();
    }
    auto t3 = std::chrono::steady_clock::now();

    auto ms_recycling =
        std::chrono::duration<double, std::milli>(t1 - t0).count();
    auto ms_standard =
        std::chrono::duration<double, std::milli>(t3 - t2).count();

    auto pct = std::round((ms_standard / ms_recycling - 1.0) * 1000.0) / 10.0;

    std::cout
        << iterations << " iterations, "
        << "4-deep coroutine chain\n\n"
        << "  Recycling allocator: "
        << ms_recycling << " ms\n"
        << "  std::allocator:      "
        << ms_standard << " ms\n"
        << "  Speedup:             "
        << std::fixed << std::setprecision(1)
        << pct << "%\n";

    return 0;
}
