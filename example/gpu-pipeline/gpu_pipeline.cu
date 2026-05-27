//
// Copyright (c) 2026 Steve Gerbino
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

//
// Scene 1 (Direction 1): a capy coroutine awaits a sender whose
// terminal action is a real CUDA __global__ kernel scheduled on
// nvexec::stream_scheduler.
//
// Scene 2 (Direction 2): a capy IoAwaitable (capy::read over a
// deterministic in-process stream pair) is exposed as a stdexec
// sender, then composed with stdexec::upon_error, and consumed
// via stdexec::sync_wait. Both the happy path and an injected-eof
// path are exercised.
//

#include "awaitable_sender.hpp"
#include "sender_awaitable.hpp"

#include <boost/capy.hpp>
#include <boost/capy/test/stream.hpp>

#include <stdexec/execution.hpp>
#include <exec/static_thread_pool.hpp>
#include <nvexec/stream_context.cuh>

#include <cassert>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <latch>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>

#include <cuda_runtime.h>

namespace capy = boost::capy;
namespace ex   = stdexec;

namespace {

void cuda_check(cudaError_t e, char const* where)
{
    if (e != cudaSuccess)
    {
        std::cerr << where << ": " << cudaGetErrorString(e) << '\n';
        std::abort();
    }
}

// Scene 1: capy coroutine awaits a nvexec-scheduled SAXPY kernel.
// Returns the host-side value at y[0] after kernel completion.
//
// Pipeline:
//   just(N, a, x, y)
//     | continues_on(gpu)                  switch onto nvexec stream
//     | nvexec::launch(<<<grid, block>>>)  __global__ kernel on stream
//     | continues_on(cpu)                  transfer completion back to host
//
// The trailing continues_on(cpu) is load-bearing: the as-written
// nvexec adapters complete on device, but the bridge's
// bridge_receiver is host-only. The host hop must happen before the
// bridge connects.
capy::task<float>
scene1(nvexec::stream_scheduler gpu,
       stdexec::scheduler auto cpu)
{
    constexpr int   N           = 1 << 16;
    constexpr int   BLOCK       = 256;
    constexpr int   GRID        = (N + BLOCK - 1) / BLOCK;
    constexpr float a           = 3.0f;

    float* d_x = nullptr;
    float* d_y = nullptr;
    cuda_check(cudaMalloc(&d_x, N * sizeof(float)), "cudaMalloc x");
    cuda_check(cudaMalloc(&d_y, N * sizeof(float)), "cudaMalloc y");

    std::vector<float> h_x(N, 1.0f);
    std::vector<float> h_y(N, 2.0f);
    cuda_check(cudaMemcpy(d_x, h_x.data(),
        N * sizeof(float), cudaMemcpyHostToDevice), "H2D x");
    cuda_check(cudaMemcpy(d_y, h_y.data(),
        N * sizeof(float), cudaMemcpyHostToDevice), "H2D y");

    auto const enter_tid = std::this_thread::get_id();
    std::cout
        << "  scene1: pre-await on thread "
        << enter_tid << '\n';

    co_await capy::await_sender(
        ex::just(N, a, d_x, d_y)
        | ex::continues_on(gpu)
        | nvexec::launch({.grid_size = GRID, .block_size = BLOCK},
            [] (cudaStream_t, int n, float k, float const* x, float* y) {
                int i = blockIdx.x * blockDim.x + threadIdx.x;
                if (i < n)
                    y[i] = k * x[i] + y[i];
            })
        | ex::continues_on(cpu));

    auto const resume_tid = std::this_thread::get_id();
    std::cout
        << "  scene1: post-await on thread "
        << resume_tid << '\n';

    // The resume thread is a capy worker that never touched CUDA; it
    // has no current context. cudaSetDevice establishes one before
    // the cleanup calls run.
    cuda_check(cudaSetDevice(0), "cudaSetDevice");

    float h_y0 = 0.0f;
    cuda_check(cudaMemcpy(&h_y0, d_y,
        sizeof(float), cudaMemcpyDeviceToHost), "D2H y[0]");

    cuda_check(cudaFree(d_x), "cudaFree x");
    cuda_check(cudaFree(d_y), "cudaFree y");

    co_return h_y0;
}

// Adapter run_async-like driver: kicks off scene1 on the capy
// thread_pool, blocks the caller until it completes, and returns
// the result via the supplied storage.
void
run_scene1(capy::thread_pool& pool, float& out)
{
    std::latch done(1);
    std::exception_ptr err;

    auto on_ok = [&](float v) noexcept {
        out = v;
        done.count_down();
    };
    auto on_err = [&](std::exception_ptr ep) noexcept {
        err = ep;
        done.count_down();
    };

    nvexec::stream_context stream_ctx;
    exec::static_thread_pool cpu_pool(1);
    capy::run_async(
        pool.get_executor(),
        on_ok,
        on_err)(scene1(
            stream_ctx.get_scheduler(),
            cpu_pool.get_scheduler()));

    done.wait();
    if (err)
        std::rethrow_exception(err);
}

// Scene 2: capy::read exposed as a stdexec sender, composed with
// stdexec::upon_error, driven by sync_wait. write_env injects the
// capy executor that the as_sender bridge needs to drive the
// underlying IoAwaitable.
// stream::read_some returns a raw IoAwaitable, which the bridge
// expects. (capy::read returns a task<io_result<size_t>>, and the
// bridge's start() does not perform symmetric transfer to the
// task's own handle, so wrapping a task hangs.)
void
scene2_happy_path(capy::thread_pool& pool)
{
    constexpr std::string_view payload = "payload bytes";

    auto [a, b] = capy::test::make_stream_pair();
    b.provide(payload);

    char buf[64];
    auto sndr = ex::write_env(
        capy::as_sender(
            a.read_some(capy::mutable_buffer(buf, sizeof buf))),
        ex::prop{capy::get_io_executor, pool.get_executor()})
        | ex::upon_error([](auto e) noexcept -> std::size_t {
            if constexpr (std::is_same_v<
                std::decay_t<decltype(e)>, std::error_code>)
            {
                std::cerr
                    << "  scene2 happy: unexpected error: "
                    << e.message() << '\n';
            }
            std::abort();
        });

    auto result = ex::sync_wait(std::move(sndr));
    assert(result.has_value());
    auto const [n] = *result;
    assert(n == payload.size());
    assert(std::string_view(buf, n) == payload);

    std::cout
        << "  scene2 happy: read " << n
        << " bytes\n";
}

void
scene2_error_path(capy::thread_pool& pool)
{
    auto [a, b] = capy::test::make_stream_pair();
    b.close();

    char buf[64];
    bool fired = false;
    std::error_code observed;

    auto sndr = ex::write_env(
        capy::as_sender(
            a.read_some(capy::mutable_buffer(buf, sizeof buf))),
        ex::prop{capy::get_io_executor, pool.get_executor()})
        | ex::upon_error([&](auto e) noexcept -> std::size_t {
            if constexpr (std::is_same_v<
                std::decay_t<decltype(e)>, std::error_code>)
            {
                fired = true;
                observed = e;
            }
            return 0;
        });

    auto result = ex::sync_wait(std::move(sndr));
    assert(result.has_value());
    auto const [n] = *result;

    assert(fired);
    assert(observed);
    std::cout
        << "  scene2 error: upon_error fired with \""
        << observed.message() << "\" (n=" << n << ")\n";
}

} // namespace

// Minimal "send a value through the GPU, get it back" coroutine.
// Sanity check that the smallest plausible shape compiles and runs.
namespace mini {

capy::task<int>
gpu_add_one(int input,
            nvexec::stream_scheduler gpu,
            stdexec::scheduler auto cpu)
{
    int* d_out = nullptr;
    cudaMalloc(&d_out, sizeof(int));

    co_await capy::await_sender(
        ex::just(input, d_out)
        | ex::continues_on(gpu)
        | nvexec::launch({.grid_size = 1, .block_size = 1},
            [](cudaStream_t, int x, int* y) { *y = x + 1; })
        | ex::continues_on(cpu));

    cudaSetDevice(0);
    int h_out;
    cudaMemcpy(&h_out, d_out, sizeof(int),
        cudaMemcpyDeviceToHost);
    cudaFree(d_out);
    co_return h_out;
}

void
run(capy::thread_pool& pool, int input, int& out)
{
    std::latch done(1);
    std::exception_ptr err;

    nvexec::stream_context stream_ctx;
    exec::static_thread_pool cpu_pool(1);
    capy::run_async(
        pool.get_executor(),
        [&](int v) noexcept { out = v; done.count_down(); },
        [&](std::exception_ptr ep) noexcept {
            err = ep; done.count_down(); })(
        gpu_add_one(input,
            stream_ctx.get_scheduler(),
            cpu_pool.get_scheduler()));

    done.wait();
    if (err) std::rethrow_exception(err);
}

} // namespace mini

int main()
{
    std::cout
        << "main thread: "
        << std::this_thread::get_id() << '\n';

    capy::thread_pool pool;

    std::cout << "--- scene 0: minimal gpu_add_one ---\n";
    int out = 0;
    mini::run(pool, 41, out);
    std::cout << "  scene 0: 41 + 1 -> " << out << '\n';
    assert(out == 42);

    std::cout << "--- scene 1: await_sender( gpu sender ) ---\n";
    float y0 = 0.0f;
    run_scene1(pool, y0);
    std::cout << "  scene1: y[0] = " << y0 << '\n';
    // a*x + y = 3*1 + 2 = 5
    assert(y0 == 5.0f);

    std::cout << "--- scene 2a: as_sender( read_some ) happy ---\n";
    scene2_happy_path(pool);

    std::cout << "--- scene 2b: as_sender( read_some ) error ---\n";
    scene2_error_path(pool);

    std::cout << "all scenes passed\n";
    return 0;
}
