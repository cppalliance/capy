//
// Copyright (c) 2026 Steve Gerbino
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#include "cuda_datamovement.hpp"

#include <boost/capy.hpp>
#include <boost/capy/concept/io_awaitable.hpp>
#include <boost/capy/test/write_stream.hpp>

#include <cstddef>
#include <span>
#include <system_error>
#include <type_traits>
#include <utility>

namespace capy = boost::capy;
namespace ex   = capy::example;

// Intentionally io_env-less: a standard awaitable, not an IoAwaitable.
static_assert(! capy::IoAwaitable<ex::cuda_stream_awaiter>);

// The data-movement awaitables depend on this helper, which the paper
// references but never defines.
static_assert(std::is_same_v<
    decltype(ex::make_cuda_error(cudaSuccess)), std::error_code>);

// The memcpy member functions return IoAwaitables.
static_assert(capy::IoAwaitable<
    decltype(std::declval<ex::cuda_stream&>().memcpy_h2d(
        nullptr, nullptr, std::size_t{0}))>);
static_assert(capy::IoAwaitable<
    decltype(std::declval<ex::cuda_stream&>().memcpy_d2h(
        nullptr, nullptr, std::size_t{0}))>);
static_assert(capy::IoAwaitable<
    decltype(std::declval<ex::cuda_stream&>().synchronize())>);

// GPU device memory satisfies WriteStream and type-erases with zero
// per-operation allocation.
static_assert(capy::WriteStream<ex::cuda_device_stream>);

// A protocol handler compiled once, linked against any transport.
capy::task<>
ingest(capy::any_write_stream& dest, std::span<std::byte const> data)
{
    auto [ec, n] = co_await dest.write_some(
        capy::make_buffer(data.data(), data.size()));
    if(ec)
        co_return;
    // ...protocol logic...
}

// Reference ingest against two transports to force the "one .o, many
// transports" claim to compile. Never executed.
[[maybe_unused]] void
link_check()
{
    ex::cuda_device_stream gpu(nullptr, nullptr);
    capy::any_write_stream gpu_dest(&gpu);     // GPU device memory

    capy::test::write_stream mem;
    capy::any_write_stream mem_dest(&mem);     // in-memory transport

    std::byte payload[8]{};
    (void) ingest(gpu_dest, payload);
    (void) ingest(mem_dest, payload);
}

#if defined(CAPY_EXAMPLE_HAS_NCCL)
#include <nccl.h>

// NCCL interop: a collective enqueues onto the CUDA stream, then
// synchronize() awaits its completion through the same IoAwaitable path.
capy::task<>
all_reduce(ex::cuda_stream& cs, ncclComm_t comm,
    float const* sendbuf, float* recvbuf, std::size_t count)
{
    ncclAllReduce(sendbuf, recvbuf, count, ncclFloat, ncclSum,
        comm, cs.native_handle());
    co_await cs.synchronize();
}
#endif

int main()
{
    return 0;
}
