# CUDA notification-strategies example

One GPU completion, three notification mechanisms, one protocol.

The same pipeline (fill a device buffer, copy it back to host, await the
CUDA stream) is awaited three structurally different ways. All three are
`boost::capy::IoAwaitable`s, and all three produce the identical result
at runtime. This demonstrates that the IoAwaitable protocol is
independent of how asynchronous completion is detected: a host-function
callback is only one option, and not the best-scaling one.

## The three mechanisms

| Mechanism | How completion is detected | Resumes on |
|-----------|----------------------------|------------|
| `callback_awaitable` | `cudaLaunchHostFunc` enqueued on the stream | a CUDA driver thread re-posts through the executor |
| `poll_awaitable` | a service thread loops `cudaEventQuery` on a recorded event | the poll thread posts when ready |
| `deferred_sync_awaitable` | a service thread runs blocking `cudaStreamSynchronize` | the service thread posts when it returns |

Each awaitable captures the executor and posts the continuation through
it, so the coroutine always resumes on a worker thread, never on a CUDA
or service thread.

The callback mechanism is the only one that cannot report a stream error
through its host function; `cudaLaunchHostFunc` does not pass completion
status to the callback, so `callback_awaitable` always resumes with
success — this is an inherent limitation of the API.

### Service lifetime

`poll_service` and `sync_service` own threads that post continuations to
the worker executor. Construct them after the worker `thread_pool` and
destroy them before it, so no continuation is ever posted to a destroyed
executor. The driver joins every pipeline (via a `std::latch`) before
shutdown, so no wait is outstanding at teardown.

## Scaling tradeoff

This example proves the three mechanisms are *equivalent in result*. It
does not measure their *throughput under load*, which a single-GPU
developer box cannot show. That comparison needs many worker threads
driving a server-class GPU.

For that measurement: E. Cano, M. Fila, A. Krasznahorkay,
"Scheduling for Next Generation Triggers", CHEP 2026,
<https://indico.cern.ch/event/1471803/contributions/6967272/>. They
report that the CUDA host-function callback handler scales poorly as the
number of worker threads grows, while event polling and deferred
synchronization remain stable. In a multi-threaded framework, prefer the
poll or deferred-sync mechanisms; reach for the callback mechanism for
its simplicity in low-concurrency settings.

## Prerequisites

- NVIDIA GPU and driver visible to `nvidia-smi`.
- CUDA toolkit 13.x.
- clang as host and CUDA compiler (verified with clang 22).
- `CMAKE_CXX_STANDARD=20`.

## Building and running

```
CXX=clang++ cmake -S . -B build-cuda -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_STANDARD=20 \
    -DBOOST_CAPY_BUILD_CUDA_EXAMPLES=ON \
    -DCMAKE_CUDA_COMPILER=clang++ \
    -DCMAKE_CUDA_HOST_COMPILER=clang++ \
    -DCMAKE_CUDA_ARCHITECTURES=89 \
    -DCUDAToolkit_ROOT=/opt/cuda
cmake --build build-cuda --config Release \
    --target capy_example_cuda_notification_strategies
./build-cuda/example/cuda/notification-strategies/capy_example_cuda_notification_strategies
```

Replace `89` with your GPU's compute capability
(`nvidia-smi --query-gpu=compute_cap --format=csv,noheader`).

Unlike the sibling `cuda/datamovement` example, this one is meant to be
run. The pass condition is all three mechanisms printing the same
checksum and a zero exit code.
