//
// Copyright (c) 2025 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#include <boost/capy/ex/run_async.hpp>
#include <boost/capy/ex/execution_context.hpp>
#include <boost/capy/ex/run_on.hpp>
#include <boost/capy/ex/strand.hpp>
#include <boost/capy/task.hpp>

#include <chrono>
#include <iostream>
#include <iomanip>

using namespace boost::capy;

//-----------------------------------------------
//
// Test Execution Context
//
// A minimal execution context for benchmarking.
//
//-----------------------------------------------

class test_context : public execution_context
{
public:
    ~test_context()
    {
        shutdown();
        destroy();
    }
};

//-----------------------------------------------
//
// Test Executor
//
// A minimal executor that satisfies the capy
// dispatcher concept. Dispatches inline for
// benchmarking pure coroutine overhead.
//
//-----------------------------------------------

class test_executor
{
    test_context* ctx_;

public:
    explicit test_executor(test_context& ctx) noexcept
        : ctx_(&ctx)
    {
    }

    test_context& context() const noexcept
    {
        return *ctx_;
    }

    bool operator==(test_executor const& other) const noexcept
    {
        return ctx_ == other.ctx_;
    }

    void on_work_started() const noexcept
    {
    }

    void on_work_finished() const noexcept
    {
    }

    // Dispatcher interface - dispatch inline
    any_coro operator()(any_coro h) const
    {
        return h;
    }

    any_coro dispatch(any_coro h) const
    {
        return h;
    }

    // Post interface - resume inline for benchmarking
    void post(any_coro h) const
    {
        h.resume();
    }

    void defer(any_coro h) const
    {
        h.resume();
    }
};

//-----------------------------------------------
//
// Foreign Awaitable
//
// Simulates awaiting a foreign context (e.g.,
// CPU-bound work on another thread pool).
//
//-----------------------------------------------

struct foreign_awaitable
{
    bool await_ready() const noexcept { return true; }

    template<typename Promise>
    void await_suspend(std::coroutine_handle<Promise>) const noexcept
    {
    }

    void await_resume() const noexcept
    {
    }

    // Affine awaitable protocol
    template<typename D>
    any_coro await_suspend(any_coro h, D const&) const
    {
        return h;
    }
};

//-----------------------------------------------
//
// Depth 1 Benchmarks
//
// Flow: c -> (return)
// Single coroutine, no I/O
//
//-----------------------------------------------

task<int> depth1()
{
    co_return 42;
}

//-----------------------------------------------
//
// Depth 2 Benchmarks
//
// Flow: c1 -> c2
// Two coroutines, executor inherited
//
//-----------------------------------------------

task<int> depth2()
{
    co_return co_await depth1();
}

//-----------------------------------------------
//
// Depth 3 Benchmarks
//
// Flow: c1 -> c2 -> c3
// Three coroutines, executor inherited
//
//-----------------------------------------------

task<int> depth3()
{
    co_return co_await depth2();
}

//-----------------------------------------------
//
// Depth 4 Benchmarks
//
// Flow: c1 -> c2 -> c3 -> c4
// Four coroutines, executor inherited
//
//-----------------------------------------------

task<int> depth4()
{
    co_return co_await depth3();
}

//-----------------------------------------------
//
// Executor Switching with Foreign Awaitable
//
// Flow: !c1 -> c2 -> !c3 -> c4
//                    \
//                      f
//
// c1 on ex1, c2 inherits ex1
// c3 switches to ex2 via run_on, awaits foreign f
// c4 inherits ex2
//
//-----------------------------------------------

task<int> switch_c4()
{
    co_return 42;
}

template<typename D>
task<int> switch_c3(D /*ex2*/, foreign_awaitable& f)
{
    co_await f;                     // foreign context
    co_return co_await switch_c4();
}

template<typename D>
task<int> switch_c2(D ex2, foreign_awaitable& f)
{
    co_return co_await run_on(ex2, switch_c3(ex2, f));
}

template<typename D>
task<int> switch_c1(D ex2, foreign_awaitable& f)
{
    co_return co_await switch_c2(ex2, f);
}

//-----------------------------------------------
//
// Benchmark Harness
//
//-----------------------------------------------

template<typename F>
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
    auto per_op = duration.count() / static_cast<double>(iterations);

    std::cout << std::left << std::setw(40) << name
              << std::right << std::setw(10) << std::fixed << std::setprecision(1)
              << per_op << " ns/op\n";
}

int main()
{
    constexpr std::size_t iterations = 1000000;

    test_context ctx1;
    test_context ctx2;

    test_executor ex1(ctx1);
    test_executor ex2(ctx2);

    strand<test_executor> strand1(ex1);
    strand<test_executor> strand2(ex2);

    foreign_awaitable foreign;

    std::cout << "=== run_async depth benchmarks ===\n\n";

    //-----------------------------------------------
    // Flow: c -> (return)
    //-----------------------------------------------
    run_benchmark("run_async depth=1", iterations, [&]{
        int result = 0;
        run_async(ex1, [&](int v){ result = v; })(depth1());
        (void)result;
    });

    //-----------------------------------------------
    // Flow: c1 -> c2
    //-----------------------------------------------
    run_benchmark("run_async depth=2", iterations, [&]{
        int result = 0;
        run_async(ex1, [&](int v){ result = v; })(depth2());
        (void)result;
    });

    //-----------------------------------------------
    // Flow: c1 -> c2 -> c3
    //-----------------------------------------------
    run_benchmark("run_async depth=3", iterations, [&]{
        int result = 0;
        run_async(ex1, [&](int v){ result = v; })(depth3());
        (void)result;
    });

    //-----------------------------------------------
    // Flow: c1 -> c2 -> c3 -> c4
    //-----------------------------------------------
    run_benchmark("run_async depth=4", iterations, [&]{
        int result = 0;
        run_async(ex1, [&](int v){ result = v; })(depth4());
        (void)result;
    });

    std::cout << "\n=== strand run_async benchmarks ===\n\n";

    //-----------------------------------------------
    // Flow: c -> (return) via strand
    //-----------------------------------------------
    run_benchmark("strand run_async depth=1", iterations, [&]{
        int result = 0;
        run_async(strand1, [&](int v){ result = v; })(depth1());
        (void)result;
    });

    //-----------------------------------------------
    // Flow: c1 -> c2 -> c3 -> c4 via strand
    //-----------------------------------------------
    run_benchmark("strand run_async depth=4", iterations, [&]{
        int result = 0;
        run_async(strand1, [&](int v){ result = v; })(depth4());
        (void)result;
    });

    std::cout << "\n=== run_on benchmarks ===\n\n";

    //-----------------------------------------------
    // Flow: !c1 -> c2 -> !c3 -> c4
    //                    \
    //                      f
    //-----------------------------------------------
    run_benchmark("run_on executor switch + foreign", iterations, [&]{
        int result = 0;
        run_async(ex1, [&](int v){ result = v; })(switch_c1(ex2, foreign));
        (void)result;
    });

    //-----------------------------------------------
    // Flow: !c1 -> c2 -> !c3 -> c4 via strands
    //                    \
    //                      f
    //-----------------------------------------------
    run_benchmark("run_on strand switch + foreign", iterations, [&]{
        int result = 0;
        run_async(strand1, [&](int v){ result = v; })(switch_c1(strand2, foreign));
        (void)result;
    });

    std::cout << "\nDone.\n";
    return 0;
}
