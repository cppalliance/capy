# CUDA pipeline example

This example demonstrates that `boost::capy::await_sender` and
`boost::capy::as_sender` compose with NVIDIA's `nvexec::stream_scheduler`,
not just with CPU schedulers. Two runnable scenes, plus a third that is
built but not run (P4251R0):

1. **Scene 1 (Direction 1).** A `boost::capy::task` running on
   `boost::capy::thread_pool` `co_await`s a sender whose terminal action is
   a real `__global__` SAXPY kernel scheduled on `nvexec::stream_scheduler`.
   When the CUDA stream signals completion, the coroutine resumes on the
   capy executor with the kernel's result.

2. **Scene 2 (Direction 2).** `boost::capy::test::stream::read_some` is
   exposed as a stdexec sender via `boost::capy::as_sender`, composed with
   `stdexec::upon_error`, and driven by `stdexec::sync_wait`. Two runs: a
   happy-path read, and a peer-close that exercises the `upon_error` arm.

   The example wraps `read_some` (a raw IoAwaitable) rather than
   `boost::capy::read` (a `task<io_result<size_t>>`). The bridge's `start()`
   does not perform symmetric transfer to a wrapped task's own coroutine
   handle, so wrapping a task in `as_sender` hangs. Wrapping a raw
   IoAwaitable works because its `await_suspend` is either ready-with-data
   or returns `noop_coroutine()` after stashing the continuation for the
   peer to resume.

3. **Scene 3 (P4251R0), built but not run.** `handle_request` shows the
   inference-handler shape: a type-erased `any_read_stream` read, GPU
   dispatch via `await_sender` over a real nvexec kernel, and a type-erased
   `any_write_stream` write. It is compiled but not executed (`main` does not
   call it). The paper's listing runs a host `run_model()` under a
   device-side `then()`, which does not compile on nvexec (host call from
   device); this mirrors Scene 1's pattern instead, dispatching a real
   kernel and hopping `continues_on(cpu)` before the host-only bridge, and
   takes a CPU scheduler the paper's signature omits.

The bridge headers (`awaitable_sender.hpp`, `sender_awaitable.hpp`) are
copied verbatim from `bench/stdexec/`; the bridge in the bench was already
written against NVIDIA/stdexec.

## Prerequisites

- NVIDIA GPU and driver visible to `nvidia-smi`.
- CUDA toolkit. On Arch: `pacman -S cuda`. CUDA 13.x works.
- A C++23-capable compiler with both `<coroutine>` support and CUDA
  device-side compilation. Verified locally with clang 22 as host *and*
  CUDA compiler.
- `CMAKE_CXX_STANDARD=23`.

nvc++ from the NVHPC SDK is the nominally blessed compiler for nvexec,
but nvc++ 26.3 does not enable C++20 coroutines (no `__cpp_impl_coroutine`,
`co_return` parses as undefined). capy is built on coroutines, so nvc++
cannot compile capy at present. Clang-cuda is the working alternative.

## Building and running

```
CXX=clang++ cmake -S . -B build \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_STANDARD=23 \
    -DCMAKE_CUDA_COMPILER=clang++ \
    -DCMAKE_CUDA_HOST_COMPILER=clang++ \
    -DCMAKE_CUDA_ARCHITECTURES=89 \
    -DCUDAToolkit_ROOT=/opt/cuda \
    -DBOOST_CAPY_BUILD_STDEXEC_EXAMPLES=ON \
    -DBOOST_CAPY_BUILD_NVEXEC_EXAMPLES=ON
cmake --build build --config Release --target capy_example_cuda_pipeline
./build/example/cuda-pipeline/capy_example_cuda_pipeline
```

Replace `89` with your GPU's compute capability (`nvidia-smi
--query-gpu=compute_cap --format=csv,noheader`).

## Expected output

The exact thread ids vary, but the structure is fixed:

```
main thread: <tid-main>
--- scene 1: await_sender( gpu sender ) ---
  scene1: pre-await on thread <tid-A>
  scene1: post-await on thread <tid-B>
  scene1: y[0] = 5
--- scene 2a: as_sender( read_some ) happy ---
  scene2 happy: read 13 bytes
--- scene 2b: as_sender( read_some ) error ---
  scene2 error: upon_error fired with "eof" (n=0)
all scenes passed
```

Exit status is 0 on success and non-zero on any failed assertion or CUDA
error.

## Scope

Correctness only. No performance measurement; no GPU-side cancellation;
no multi-device topologies. See
`docs/superpowers/specs/2026-05-27-stdexec-gpu-example-design.md` for the
full scope statement.
