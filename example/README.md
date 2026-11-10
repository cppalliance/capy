# Boost.Capy Examples

This directory contains example programs demonstrating Boost.Capy usage.

The code here, headers included, is example code and not part of Capy. It has
no reference documentation and no stability guarantee, even where it declares
names in namespace `boost::capy`. Copy it and adapt it.

## Taught in the manual

Each of these directories has a page in the
[Example Programs](https://develop.capy.cpp.al/capy/8.examples/8.intro.html)
section of the manual.

| Directory | Page | What it shows |
| --- | --- | --- |
| `hello-task/` | [Hello Task](https://develop.capy.cpp.al/capy/8.examples/8a.hello-task.html) | Minimal Capy program: a task that prints a message |
| `producer-consumer/` | [Producer-Consumer](https://develop.capy.cpp.al/capy/8.examples/8b.producer-consumer.html) | Two tasks communicating via an async event, on a strand |
| `buffer-composition/` | [Buffer Composition](https://develop.capy.cpp.al/capy/8.examples/8c.buffer-composition.html) | Composing buffer sequences without allocation for scatter/gather I/O |
| `mock-stream-testing/` | [Mock Stream Testing](https://develop.capy.cpp.al/capy/8.examples/8d.mock-stream-testing.html) | Unit testing protocol code with mock streams and error injection |
| `type-erased-echo/` | [Type-Erased Echo](https://develop.capy.cpp.al/capy/8.examples/8e.type-erased-echo.html) | Echo server demonstrating the compilation firewall pattern |
| `timeout-cancellation/` | [Timeout with Cancellation](https://develop.capy.cpp.al/capy/8.examples/8f.timeout-cancellation.html) | Racing an operation against a deadline, and cancelling it with a stop token |
| `parallel-fetch/` | [Parallel Fetch](https://develop.capy.cpp.al/capy/8.examples/8g.parallel-fetch.html) | Running multiple operations concurrently with `when_all` |
| `strand-serialization/` | [Strand Serialization](https://develop.capy.cpp.al/capy/8.examples/8k.strand-serialization.html) | Protecting shared state with a strand instead of a mutex |
| `async-mutex/` | [Async Mutex](https://develop.capy.cpp.al/capy/8.examples/8l.async-mutex.html) | Fair FIFO coroutine locking with `async_mutex` |
| `parallel-tasks/` | [Parallel Tasks](https://develop.capy.cpp.al/capy/8.examples/8m.parallel-tasks.html) | Distributing CPU-bound work across a thread pool |
| `custom-executor/` | [Custom Executor](https://develop.capy.cpp.al/capy/8.examples/8n.custom-executor.html) | Implementing the `Executor` concept with a single-threaded run loop |
| `sender-bridge/` | [Bridging a P2300 Sender](https://develop.capy.cpp.al/capy/8.examples/8o.sender-bridge.html) | Awaiting a `std::execution` sender from a Capy coroutine. Requires `BOOST_CAPY_BUILD_P2300_EXAMPLES=ON` and C++23 |
| `asio/` | [Calling Asio from a Capy Coroutine](https://develop.capy.cpp.al/capy/8.examples/8p.asio-use-capy.html) | `use_capy_example.cpp`: Asio operations awaited through a `use_capy` completion token. Requires Boost.Asio |
| | [Using a Capy Stream from Asio](https://develop.capy.cpp.al/capy/8.examples/8t.asio-callbacks.html) | `asio_callbacks.cpp`: a Capy stream driven by Asio completion handlers |
| | [The Dimovian Ideal](https://develop.capy.cpp.al/capy/9.design/9n.WhyNotCobalt.html#_the_dimovian_ideal) | `any_stream.cpp`: stream algorithms written with no Asio headers |
| `gui-integration/` | [GUI Integration](https://develop.capy.cpp.al/capy/8.examples/8q.gui-integration.html) | Running a Capy coroutine on a GUI event loop |
| `early-quitter/` | [Writing Your Own Task Type (Early Quitter)](https://develop.capy.cpp.al/capy/8.examples/8r.early-quitter.html) | A second task type, whose coroutines a stop ends at their current `co_await` with no checks |

## Other programs

These programs have no page of their own. The
[Other Programs in the Repository](https://develop.capy.cpp.al/capy/8.examples/8.intro.html#other-programs-in-the-repository)
table in the manual lists them too.

| Directory | What it is | How it is built |
| --- | --- | --- |
| `awaitable-sender/` | A Capy awaitable wrapped as a `stdexec` sender, the reverse of `sender-bridge/` | `BOOST_CAPY_BUILD_P2300_EXAMPLES=ON` and C++23; fetches NVIDIA stdexec. Test: `capy_example_awaitable_sender_test` |
| `any-sender-size/` | Measures the `exec::any_sender` operation state and the heap allocations made by `connect` and `start` | The same flag as `awaitable-sender/`. Test: `capy_example_any_sender_size` |
| `fabrics/` | Compile-only listings from P4251R0 for HPC fabric libraries | Default build; each fabric check builds when its library is found |
| `cuda/batched-write/` | A batched `write_some` to a CUDA device stream | `BOOST_CAPY_BUILD_CUDA_EXAMPLES=ON`, C++20, and the CUDA toolkit. Test: `capy_example_cuda_batched_write` |
| `cuda/datamovement/` | Compile-only CUDA data-movement listings from P4251R0 | `BOOST_CAPY_BUILD_CUDA_EXAMPLES=ON` and C++20 |
| `cuda/notification-strategies/` | One GPU completion, awaited in three different ways | `BOOST_CAPY_BUILD_CUDA_EXAMPLES=ON` and C++20 |
| `cuda/pipeline/` | Capy tasks and senders on the `stream_scheduler` of NVIDIA `nvexec` | `BOOST_CAPY_BUILD_CUDA_EXAMPLES=ON` plus `BOOST_CAPY_BUILD_NVEXEC_EXAMPLES=ON`, which needs `BOOST_CAPY_BUILD_STDEXEC_EXAMPLES=ON` and C++23 |
| `echo-server-corosio/` | An echo server on Corosio | CMake when a `Boost::corosio` target exists, or its own Jamfile |
| `when-any-cancellation/` | A race of three data sources with `when_any`. **Out of date:** its comments say that the first child to complete wins, and its losing children report success. For the current rules, see [The canceled Convention](https://develop.capy.cpp.al/capy/4.coroutines/4f.cancellation.html#_the_canceled_convention) and [Timeout with Cancellation](https://develop.capy.cpp.al/capy/8.examples/8f.timeout-cancellation.html) | Default build, or B2 |

## Building

### CMake

```bash
cmake -B build -DBOOST_CAPY_BUILD_EXAMPLES=ON
cmake --build build
```

### B2 (BJam)

```bash
b2 example
```

B2 builds only the directories listed in `example/Jamfile`.
