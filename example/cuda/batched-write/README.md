# CUDA batched write example (P4251R0)

Runs the batched `write_some` of `cuda_device_stream` from
`example/cuda/datamovement`. Three host buffers are gathered through
`any_write_stream` in a single `write_some`: every buffer is enqueued as
its own `cudaMemcpyAsync`, one `cudaLaunchHostFunc` follows the last, and
the coroutine suspends once. The program checks that the returned count
is the sum of the buffer sizes and that the device holds the buffers'
concatenation, and exits non-zero otherwise.

Observed on an RTX 4060 (CUDA 13.3, clang 22):

```
batched write_some: 3 buffers, one await, 9 of 9 bytes, device holds "abcdefghi": ok
```
