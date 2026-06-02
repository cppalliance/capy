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

#include <cuda_runtime.h>

#include <cstddef>

namespace capy = boost::capy;
namespace ex   = capy::example;

namespace {

__global__ void
kernel_A(float* y, int n)
{
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if(i < n)
        y[i] += 1.0f;
}

__global__ void
kernel_B(float* y, int n)
{
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if(i < n)
        y[i] *= 2.0f;
}

// A pre-captured CUDA graph is the inner optimized hot path; the
// coroutine is the outer, data-dependent loop (copy in, launch the graph,
// copy out). Graph replay and coroutine orchestration optimize different
// layers and compose without either subsuming the other.
[[maybe_unused]] capy::task<>
graph_replay(ex::cuda_stream& cs, float* d_y, float* h_y, int n)
{
    cudaStream_t stream = cs.native_handle();

    cudaGraph_t graph;
    cudaGraphExec_t instance;
    dim3 grid(1);
    dim3 block(static_cast<unsigned>(n));

    cudaStreamBeginCapture(stream, cudaStreamCaptureModeGlobal);
    kernel_A<<<grid, block, 0, stream>>>(d_y, n);
    kernel_B<<<grid, block, 0, stream>>>(d_y, n);
    cudaStreamEndCapture(stream, &graph);

    cudaGraphInstantiate(&instance, graph, 0);

    co_await cs.memcpy_h2d(d_y, h_y, n * sizeof(float));
    cudaGraphLaunch(instance, stream);
    co_await cs.synchronize();
    co_await cs.memcpy_d2h(h_y, d_y, n * sizeof(float));

    cudaGraphExecDestroy(instance);
    cudaGraphDestroy(graph);
}

} // namespace
