//
// Copyright (c) 2026 Steve Gerbino
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// One GPU completion, three notification mechanisms.
//
// The IoAwaitable protocol does not care how completion is detected. The
// same pipeline (fill a device buffer, copy it back, await the stream)
// runs once per mechanism (host-function callback, event polling,
// deferred blocking synchronize) and all three must produce the same
// checksum. See README.md for the multi-threaded scaling tradeoff, which
// a single-GPU box cannot measure.

#include "notification_strategies.hpp"

#include <boost/capy.hpp>
#include <boost/capy/concept/io_awaitable.hpp>
#include <boost/capy/ex/thread_pool.hpp>

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <latch>
#include <semaphore>
#include <system_error>
#include <type_traits>
#include <vector>

namespace capy = boost::capy;
namespace ex   = boost::capy::example;

static_assert(std::is_same_v<
    decltype(ex::make_cuda_error(cudaSuccess)), std::error_code>);

// The whole point: three different mechanisms, all IoAwaitables.
static_assert(capy::IoAwaitable<ex::callback_awaitable>);
static_assert(capy::IoAwaitable<ex::poll_awaitable>);
static_assert(capy::IoAwaitable<ex::deferred_sync_awaitable>);

namespace {

constexpr int buffer_len = 256;
constexpr int fill_value = 7;

__global__ void
fill_kernel(int* p, int n, int v)
{
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if(i < n)
        p[i] = v;
}

// Writes through a null pointer: a sticky cudaErrorIllegalAddress that
// poisons the context. Used by --fault to see which mechanisms still
// deliver the error to the awaiting coroutine.
__global__ void
fault_kernel(int* p)
{
    p[threadIdx.x] = 1;
}

enum class notify
{
    callback,
    poll,
    deferred_sync
};

bool
parse_notify(char const* s, notify& out) noexcept
{
    if(std::strcmp(s, "callback") == 0)
        out = notify::callback;
    else if(std::strcmp(s, "poll") == 0)
        out = notify::poll;
    else if(std::strcmp(s, "deferred-sync") == 0)
        out = notify::deferred_sync;
    else
        return false;
    return true;
}

char const*
name_of(notify how) noexcept
{
    switch(how)
    {
    case notify::callback:      return "callback";
    case notify::poll:          return "poll";
    case notify::deferred_sync: return "deferred-sync";
    }
    return "?";
}

// Await the stream's completion using the selected mechanism. poll waits
// on the event; callback and deferred-sync wait on the stream.
capy::task<std::error_code>
wait(ex::cuda_stream& stream,
     ex::cuda_event& event,
     notify how,
     ex::poll_service& poll_svc,
     ex::sync_service& sync_svc)
{
    switch(how)
    {
    case notify::callback:
        co_return co_await stream.sync_via_callback();
    case notify::poll:
        co_return co_await event.sync_via_poll(poll_svc);
    case notify::deferred_sync:
        co_return co_await stream.sync_via_deferred(sync_svc);
    }
    co_return std::error_code{};
}

// Fill a device buffer, copy it back, await completion via `how`, and
// return the host-side checksum. Identical across mechanisms.
capy::task<long>
run_pipeline(ex::cuda_stream& stream,
             ex::cuda_event& event,
             notify how,
             ex::poll_service& poll_svc,
             ex::sync_service& sync_svc)
{
    auto s = stream.native_handle();

    int* d_buf = nullptr;
    auto err = cudaMallocAsync(
        reinterpret_cast<void**>(&d_buf),
        buffer_len * sizeof(int), s);
    if(err != cudaSuccess)
        co_return -1;

    fill_kernel<<<(buffer_len + 63) / 64, 64, 0, s>>>(
        d_buf, buffer_len, fill_value);

    std::vector<int> host(buffer_len, 0);
    cudaMemcpyAsync(
        host.data(), d_buf, buffer_len * sizeof(int),
        cudaMemcpyDeviceToHost, s);
    cudaFreeAsync(d_buf, s);
    event.record(s);

    auto ec = co_await wait(stream, event, how, poll_svc, sync_svc);
    if(ec)
        co_return -1;

    long sum = 0;
    for(int v : host)
        sum += v;
    co_return sum;
}

// Drive one pipeline run to completion on `pool` and return its checksum.
long
run_one(capy::thread_pool& pool,
        ex::cuda_stream& stream,
        ex::cuda_event& event,
        notify how,
        ex::poll_service& poll_svc,
        ex::sync_service& sync_svc)
{
    long result = 0;
    std::latch done{1};
    capy::run_async(pool.get_executor(),
        [&](long r) { result = r; done.count_down(); })(
            run_pipeline(stream, event, how, poll_svc, sync_svc));
    done.wait();
    return result;
}

// Launch a faulting kernel and await the stream via `how`. Returns the
// error the mechanism reported, or success if it reported none.
capy::task<std::error_code>
run_fault(ex::cuda_stream& stream,
          ex::cuda_event& event,
          notify how,
          ex::poll_service& poll_svc,
          ex::sync_service& sync_svc)
{
    auto s = stream.native_handle();
    fault_kernel<<<1, 32, 0, s>>>(nullptr);
    event.record(s);
    co_return co_await wait(stream, event, how, poll_svc, sync_svc);
}

// The faulted context is dead afterwards, so each mechanism is probed
// in its own process. A mechanism whose coroutine never resumes is
// reported as such after the watchdog expires; the process is then
// exited without teardown because the coroutine is still suspended.
int
fault_main(notify how)
{
    capy::thread_pool pool(4);
    ex::poll_service poll_svc;
    ex::sync_service sync_svc;
    ex::cuda_stream stream;
    ex::cuda_event event;

    std::error_code ec;
    std::binary_semaphore done{0};
    capy::run_async(pool.get_executor(),
        [&](std::error_code e) { ec = e; done.release(); })(
            run_fault(stream, event, how, poll_svc, sync_svc));

    std::cout << name_of(how) << ": ";
    if(! done.try_acquire_for(std::chrono::seconds(3)))
    {
        std::cout << "coroutine never resumed (watchdog expired)\n";
        std::cout.flush();
        std::_Exit(EXIT_FAILURE);
    }
    if(! ec)
    {
        std::cout << "resumed with success despite the fault\n";
        std::cout.flush();
        std::_Exit(EXIT_FAILURE);
    }
    std::cout << "resumed with error: " << ec.message() << "\n";
    std::cout.flush();
    std::_Exit(EXIT_SUCCESS);
}

} // namespace

int
main(int argc, char** argv)
{
    if(argc == 3 && std::strcmp(argv[1], "--fault") == 0)
    {
        notify how;
        if(! parse_notify(argv[2], how))
        {
            std::cerr << "usage: " << argv[0]
                      << " [--fault callback|poll|deferred-sync]\n";
            return EXIT_FAILURE;
        }
        return fault_main(how);
    }
    if(argc != 1)
    {
        std::cerr << "usage: " << argv[0]
                  << " [--fault callback|poll|deferred-sync]\n";
        return EXIT_FAILURE;
    }

    int device_count = 0;
    if(cudaGetDeviceCount(&device_count) != cudaSuccess || device_count == 0)
    {
        std::cout << "No CUDA device available.\n";
        return EXIT_FAILURE;
    }

    // Declaration order fixes teardown: services stop before the pool
    // they post to; the stream/event close first of all.
    capy::thread_pool pool(4);
    ex::poll_service poll_svc;
    ex::sync_service sync_svc;
    ex::cuda_stream stream;
    ex::cuda_event event;

    notify const modes[] =
        { notify::callback, notify::poll, notify::deferred_sync };

    long first = 0;
    bool ok = true;
    std::cout << "mechanism      checksum\n";
    for(std::size_t i = 0; i < std::size(modes); ++i)
    {
        long r = run_one(pool, stream, event, modes[i], poll_svc, sync_svc);
        std::cout << name_of(modes[i]) << "   " << r << "\n";
        if(i == 0)
            first = r;
        else if(r != first)
            ok = false;
    }

    long const expected =
        static_cast<long>(buffer_len) * fill_value;
    if(! ok || first != expected)
    {
        std::cout << "MISMATCH: mechanisms disagree or wrong result "
                     "(expected " << expected << ")\n";
        return EXIT_FAILURE;
    }

    std::cout << "All three mechanisms produced " << first
              << " (identical).\n";
    return EXIT_SUCCESS;
}
