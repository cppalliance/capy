# CUDA data-movement example (P4251R0)

Validation that the CUDA data-movement listings from
P4251R0 "Coroutine Completion for GPU Data Movement: Convergent Findings"
are type-correct against the
real `boost::capy` API and CUDA. The paper flags this code as AI-generated
and unverified; this target proves it compiles. Nothing here is executed
at runtime; `example/cuda/batched-write` runs the batched `write_some`.

What is validated:

- `cuda_stream_awaiter`: the io_env-less baseline. Asserted to be a
  standard awaitable but **not** an `IoAwaitable`.
- `cuda_stream`: `memcpy_h2d` / `memcpy_d2h` / `synchronize` return
  `IoAwaitable`s. Because `cudaLaunchHostFunc` passes no status to its
  host function, `await_resume` calls `cudaStreamQuery` after
  resumption so a stream fault surfaces as an error instead of success
  (see the `--fault` probe in `../notification-strategies`).
- NCCL interop: `ncclAllReduce` on `cuda_stream::native_handle()`
  followed by `co_await synchronize()`. Built only when NCCL is found at
  configure time.
- `cuda_device_stream`: satisfies `WriteStream`, type-erases behind
  `any_write_stream`, and the `ingest()` protocol handler compiles once
  against both a GPU stream and an in-memory transport.
- CUDA Graphs (`cuda_graphs.cu`): a captured graph is replayed inside
  a coroutine that drives `cuda_stream` memcpy / synchronize.

The non-GPU listings (the byte-oriented compound result and the
RDMA/libfabric/UCX signatures) do not need CUDA and live in the sibling
`example/fabrics` example. The sender bridge is in `example/cuda/pipeline`.

## Prerequisites

- NVIDIA GPU and driver visible to `nvidia-smi`.
- CUDA toolkit (13.x works). On Arch: `pacman -S cuda`.
- clang as host and CUDA compiler (verified with clang 22).
- `CMAKE_CXX_STANDARD=20`.

## Building

```
CXX=clang++ cmake -S . -B build-cuda -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_STANDARD=20 \
    -DBOOST_CAPY_BUILD_CUDA_EXAMPLES=ON \
    -DCMAKE_CUDA_COMPILER=clang++ \
    -DCMAKE_CUDA_HOST_COMPILER=clang++ \
    -DCMAKE_CUDA_ARCHITECTURES=89 \
    -DCUDAToolkit_ROOT=/opt/cuda
cmake --build build-cuda --config Release --target capy_example_cuda_datamovement
```

Replace `89` with your GPU's compute capability
(`nvidia-smi --query-gpu=compute_cap --format=csv,noheader`).

A clean build is the pass condition; the binary need not be run.

## Scope

No runtime execution and no multi-device topologies. A clean
build with every `static_assert` holding is the whole deliverable. The
NCCL snippet builds only when NCCL is found. NVSHMEM (a GPU member of the
paper's HPC-fabric list) is not verified: `nvshmem_int_put` is device-side
and its headers do not compile under clang-cuda (capy requires clang-cuda,
since nvcc lacks C++20 coroutines). The non-GPU fabric signatures live in
`example/fabrics`, and the sender bridge in `example/cuda/pipeline`.
