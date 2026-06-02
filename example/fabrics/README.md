# Fabrics example (P4251R0)

The transport-neutral, non-GPU listings from P4251R0 "IoAwaitables for GPU
Data Movement". Validation that the paper's byte-oriented and
HPC-fabric calls are type-correct against the real `boost::capy` API and the
installed fabric libraries. Nothing here is executed; a clean build is the
deliverable.

Unlike the `cuda/` examples, this needs **no CUDA toolchain**: only a
C++20 compiler and `boost::capy`, plus whichever fabric libraries happen to
be installed.

What is validated:

- `read_with_reset`: `read_some` delivers `(error_code, n)` via structured
  bindings; the coroutine branches on a partial-read condition with no
  sender channel to choose. Pure capy, no transport library.
- HPC-fabric send signatures, each built only when its library is found:
  - libibverbs `ibv_post_send` (RDMA / InfiniBand)
  - libfabric `fi_send` (OFI completion-queue model)
  - UCX `ucp_tag_send_nbx` (progress-engine callback model)

NCCL and NVSHMEM are the GPU members of the paper's fabric list; NCCL is
exercised by the `cuda/datamovement` example, and NVSHMEM's device API does
not compile under clang-cuda (see that example's notes).

## Building

This builds as part of the normal example set (`BOOST_CAPY_BUILD_EXAMPLES`).
The fabric checks activate automatically when their libraries are present:

- libibverbs: `libibverbs` package.
- libfabric: `libfabric` package.
- UCX: `openucx` package, or pass
  `-DCAPY_UCX_INCLUDE_DIR=<dir> -DCAPY_UCX_LIBRARY=<libucp.so>` to point at a
  non-standard location (for example the UCX bundled in the NVIDIA HPC SDK).

```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_STANDARD=20
cmake --build build --config Release --target capy_example_fabrics
```

A clean build is the pass condition; the binary need not be run.
