---
Boost.Capy specific instructions
---

# Research:
- research/mazieres-coro.md
- research/ana-lucia-coro.md

# Extra Instructions
- use `thread_pool` and `thread_pool::get_executor` in examples which need a context or executor

## Introduction
- Requirements: Familiarity with C++20 and C++20 coroutines
- Notes: This library offers coroutine concepts and concrete types, buffer concepts and concrete types, stream concepts and concrete types, generic algorithms that work on buffers and streams, plus a thread pool and a strand executor along with a full suite of testing utilities that include mock objects and error generators

## Section: Introduction To C++20 Coroutines

### Part I: Foundations

1. **Functions and the Call Stack**
   - How normal function calls work
   - Stack frames and local variables
   - The limitation: run-to-completion

2. **What Is a Coroutine?**
   - Suspendable functions
   - Saving and restoring state
   - Cooperative vs preemptive multitasking

3. **Why Coroutines?**
   - Asynchronous programming without callbacks
   - Generators and lazy sequences
   - State machines made simple

### Part II: C++20 Syntax

4. **The Three Keywords**
   - `co_await` — suspend and wait
   - `co_yield` — produce a value and suspend
   - `co_return` — complete the coroutine

5. **Your First Coroutine**
   - A simple generator example
   - What the compiler transforms
   - The coroutine frame

6. **Awaitables and Awaiters**
   - The awaitable concept
   - `await_ready`, `await_suspend`, `await_resume`
   - `std::suspend_always` and `std::suspend_never`

### Part III: The Coroutine Machinery

7. **The Promise Type**
   - `get_return_object()`
   - `initial_suspend()` and `final_suspend()`
   - `return_void()` / `return_value()` / `yield_value()`
   - `unhandled_exception()`

8. **Coroutine Handle**
   - `std::coroutine_handle<P>`
   - `resume()`, `destroy()`, `done()`
   - Accessing the promise

9. **Putting It Together**
   - Building a complete `task<T>` type
   - Building a complete `generator<T>` type

### Part IV: Advanced C++20 Topics

10. **Symmetric Transfer**
    - Avoiding stack overflow
    - Tail-call optimization for coroutines
    - Returning `coroutine_handle` from `await_suspend`

11. **Coroutine Allocation**
    - The coroutine frame heap allocation
    - Heap Allocation eLision Optimization (HALO)

12. **Exception Handling**
    - Exceptions in coroutines
    - `unhandled_exception()` behavior

## Section: Introduction to I/O Awaitables

### Concept Hierarchy

```mermaid
graph TD
    A[IoAwaitable] --> B[IoAwaitableTask]
    B --> C[IoLaunchableTask]
    
    A -- "await_suspend(coro, executor_ref, stop_token)" --> A
    B -- "+ promise_type with set/get executor & stop_token" --> B
    C -- "+ handle(), release(), exception(), result()" --> C
```

### What is IoAwaitable?
- The three-argument `await_suspend` signature
- Forward context propagation vs backward queries
- Reference: `<boost/capy/concept/io_awaitable.hpp>`

### IoAwaitableTask
- Promise-level context storage and retrieval
- The bidirectional capability: receive and propagate
- Reference: `<boost/capy/concept/io_awaitable_task.hpp>`

### IoLaunchableTask
- Interface for launch functions
- Lifetime management: `handle()`, `release()`
- Completion access: `exception()`, `result()`
- Reference: `<boost/capy/concept/io_launchable_task.hpp>`

### The Executor
- The `Executor` concept: `dispatch()` and `post()`
- `executor_ref`: type-erased executor wrapper
- Forward propagation via `await_transform`
- Reference: `<boost/capy/concept/executor.hpp>`, `<boost/capy/ex/executor_ref.hpp>`

### The Stop Token
- Cooperative cancellation with `std::stop_token`
- Downward flow from application to I/O
- OS integration (IOCP, io_uring, etc.)
- Propagation through the coroutine chain

### The Allocator
- The timing constraint: `operator new` before coroutine body
- Thread-local propagation and "the window"
- The `FrameAllocator` concept
- Reference: `<boost/capy/concept/frame_allocator.hpp>`

### Launching Coroutines

#### `run_async`
- Entry point from non-coroutine code
- Two-call syntax and C++17 evaluation order
- Handler overloads for results and exceptions
- Reference: `<boost/capy/ex/run_async.hpp>`

#### `run_on`
- Executor hopping within coroutine code
- Binding a child task to a different executor
- Reference: `<boost/capy/ex/run_on.hpp>`

## Section: Capy Library

1. **The `task<T>` Type**
   - Declaring `task<T>` coroutines
   - Returning values with `co_return`
   - Awaiting other tasks
   - Lazy execution and symmetric transfer

2. **Error Handling with `io_result`**
   - `io_result<T>` and structured bindings
   - Error codes vs exceptions
   - Propagating errors through `co_await`

3. **Buffers**
   - `flat_dynamic_buffer` and `circular_dynamic_buffer`
   - The DynamicBuffer concept
   - Buffer sequences and buffer views

4. **Stream Concepts**
   - `ReadStream` and `WriteStream`
   - `ReadSource` and `WriteSink`
   - Composed `read()` and `write()` operations

5. **Concurrent Composition**
   - `when_all` for parallel execution
   - Result tuple and void filtering
   - Stop propagation across siblings

6. **Cancellation**
   - Stop tokens and stop sources
   - Cooperative cancellation patterns
   - Cleanup on early exit

7. **Synchronization Primitives**
   - `coro_lock` for mutual exclusion
   - `async_event` for signaling

8. **Executors and Strands**
   - `executor_ref` and `any_executor`
   - `strand` for serialization
   - `thread_pool` and `execution_context`

9. **Frame Allocators**
   - Coroutine frame allocation
   - `frame_allocator` and recycling
   - HALO optimization support

## Section: Buffers

### Part I: Philosophy and Design

1. **Platform-Agnostic I/O Algorithms**
   - Generic `read()`, `write()`, stream algorithms work with any conforming type
   - No dependency on platform I/O (IOCP, io_uring, epoll, kqueue)
   - Algorithms implemented against concepts, not concrete types
   - Same code works with real sockets, SSL streams, mock objects, or custom implementations
   - Enables testing without actual network I/O

2. **Why Buffers?**
   - I/O operations work with contiguous memory regions
   - The fundamental unit: `(pointer, size)` pair
   - OS reads/writes bytes from/to linear addresses

3. **Scatter/Gather I/O**
   - Motivation: avoid copying when data isn't contiguous
   - Real-world examples:
     - HTTP message: headers + body written together
     - WebSocket frame: frame header + payload
   - `readv()`/`writev()` system calls (vectored I/O)
   - Efficiency: fewer syscalls, zero-copy message assembly

### Part II: Buffer Types

4. **const_buffer and mutable_buffer**
   - `const_buffer`: read-only view of contiguous bytes
   - `mutable_buffer`: writable view of contiguous bytes
   - Construction, accessors, prefix removal
   - Reference: `<boost/capy/buffers.hpp>`

5. **Creating Buffers with make_buffer**
   - From pointer + size
   - From C arrays, std::array, std::vector
   - From std::string, std::string_view
   - Reference: `<boost/capy/buffers/make_buffer.hpp>`

### Part III: Buffer Sequences

6. **What is a Buffer Sequence?**
   - Bidirectional range with buffer-convertible value type
   - Single buffers are degenerate sequences
   - Reference: `ConstBufferSequence`, `MutableBufferSequence` concepts

7. **Iterating Buffer Sequences**
   - `begin()` and `end()` for uniform access
   - `consuming_buffers` for incremental consumption
   - Real I/O loop patterns from `read()` and `write()`
   - Reference: `<boost/capy/buffers/consuming_buffers.hpp>`

### Part IV: Buffer Algorithms

8. **Measuring Buffers**
   - `buffer_size`: total bytes across all buffers
   - `buffer_empty`: check if total size is zero
   - `buffer_length`: number of buffers (not bytes)

9. **Copying Buffers**
   - `buffer_copy`: copy between buffer sequences
   - Optional `at_most` parameter
   - Reference: `<boost/capy/buffers/buffer_copy.hpp>`

### Part V: Dynamic Buffers

10. **The Producer/Consumer Model**
    - Dynamic buffers as intermediate storage between producer and consumer
    - Producer: network I/O writes data into the buffer
    - Consumer: application reads and processes the data
    - Synchronization through prepare/commit/consume prevents overflow/underflow

11. **The DynamicBuffer Concept**
    - Producer side: `prepare(n)` -> write data -> `commit(n)`
    - Consumer side: `data()` -> read data -> `consume(n)`
    - Capacity management: `size()`, `max_size()`, `capacity()`
    - Reference: `<boost/capy/concept/dynamic_buffer.hpp>`

12. **DynamicBufferParam for Coroutines**
    - Safe parameter passing rules
    - Lvalue vs rvalue constraints
    - The `is_dynamic_buffer_adapter` tag

13. **Provided Implementations**
    - `flat_dynamic_buffer`: linear, single-buffer sequences
    - `circular_dynamic_buffer`: ring buffer (classic producer/consumer)
    - `vector_dynamic_buffer`: growable, backed by std::vector
    - `string_dynamic_buffer`: backed by std::string

## Section: I/O Streams

I/O streams enable data to flow through different parts of a program, represented as buffer sequences. Six concepts define how data is produced and consumed: streams handle partial transfers, sources and sinks handle complete transfers with EOF signaling, and buffer sources/sinks use the callee-owns-buffers pattern for zero-copy efficiency. Type-erasing wrappers let you express APIs independent of the underlying transport.

### Part I: Stream Concepts (Partial I/O)

The stream concepts are intentionally modeled after the Boost.Asio concepts _AsyncReadStream_ and _AsyncWriteStream_.

1. **ReadStream**
   - Provides `read_some(buffers)` for partial read operations
   - Returns `(error_code, size_t)` - may transfer less than requested
   - Caller owns the buffers; stream fills as much as available
   - On success: at least 1 byte read; on EOF: `ec == cond::eof`, `n == 0`
   - Reference: `<boost/capy/concept/read_stream.hpp>`

2. **WriteStream**
   - Provides `write_some(buffers)` for partial write operations
   - Returns `(error_code, size_t)` - may transfer less than requested
   - Caller owns the buffers
   - Reference: `<boost/capy/concept/write_stream.hpp>`

### Part II: Source/Sink Concepts (Complete I/O with EOF)

Sources and sinks build on streams.

3. **ReadSource**
   - Provides `read(buffers)` for complete read operations
   - Fills entire buffer or returns EOF/error with partial count
   - Caller owns the buffers
   - Reference: `<boost/capy/concept/read_source.hpp>`

4. **WriteSink**
   - Provides `write(buffers)`, `write(buffers, eof)`, `write_eof()`
   - Complete write with explicit EOF signaling
   - Caller owns the buffers
   - After successful `write_eof()` or `write(buffers, true)`, no further writes permitted
   - Reference: `<boost/capy/concept/write_sink.hpp>`

### Part III: Buffer Concepts (Callee-Owns-Buffers)

5. **BufferSource**
   - Provides `pull(arr, max_count)` - fills array with `const_buffer` descriptors
   - Returns `(error_code, count)` where `count == 0` means source exhausted
   - Source owns the buffers; caller must consume all before next `pull()`
   - Enables zero-copy: source provides pointers to its internal storage
   - Reference: `<boost/capy/concept/buffer_source.hpp>`

6. **BufferSink**
   - Provides `prepare(arr, max_count)` - synchronous, returns writable buffer count
   - Provides `commit(n)`, `commit(n, eof)`, `commit_eof()` - async finalization
   - Sink owns the buffers; caller writes directly into sink's internal storage
   - Enables zero-copy: no intermediate buffer needed
   - Reference: `<boost/capy/concept/buffer_sink.hpp>`

### Part IV: Transfer Algorithms

7. **Composed Read/Write**
   - `read(stream, buffers)` - loops `read_some` until buffer full or error
   - `read(source, dynamic_buffer)` - loops until EOF into growable buffer
   - `write(stream, buffers)` - loops `write_some` until all written
   - Reference: `<boost/capy/read.hpp>`, `<boost/capy/write.hpp>`

8. **push_to (Caller Owns Buffers)**
   - Source provides buffers via `pull()`, data is pushed to destination
   - `push_to(BufferSource, WriteSink)` - complete transfer with EOF signaling
   - `push_to(BufferSource, WriteStream)` - streaming with partial writes
   - Reference: `<boost/capy/io/push_to.hpp>`

9. **pull_from (Callee Owns Buffers)**
   - Sink provides buffers via `prepare()`, data is pulled from source
   - `pull_from(ReadSource, BufferSink)` - complete reads into sink's buffers
   - `pull_from(ReadStream, BufferSink)` - streaming with partial reads
   - Naming reflects buffer ownership; no buffer-to-buffer variant (would require redundant copying)
   - Reference: `<boost/capy/io/pull_from.hpp>`

### Part V: Type-Erasing Wrappers

Each concept has a corresponding type-erasing wrapper in `<boost/capy/io/>`:

10. **Stream Wrappers**
    - `any_read_stream` - wraps any `ReadStream`
    - `any_write_stream` - wraps any `WriteStream`
    - `any_stream` - wraps bidirectional (both `ReadStream` and `WriteStream`)

11. **Source/Sink Wrappers**
    - `any_read_source` - wraps any `ReadSource`
    - `any_write_sink` - wraps any `WriteSink`
    - `any_buffer_source` - wraps any `BufferSource`
    - `any_buffer_sink` - wraps any `BufferSink`

12. **Wrapper Characteristics**
    - Reference semantics: wrap existing objects without ownership
    - Preallocate coroutine frame at construction for zero steady-state allocation
    - Move-only (non-copyable); cached frame reused across operations
    - Constructor takes reference to concrete type; wrapped object must outlive wrapper

### Part VI: Processing Chains

13. **Composing Transformations**
    - Data flows: Source -> Transform -> Transform -> Sink
    - Each component satisfies a concept (e.g., `BufferSource`, `WriteSink`)
    - Chain compression, decompression, encryption, framing, chunked encoding
    - Intermediate transforms implement both source and sink concepts

### Part VII: Transport-Independent APIs

14. **The Key Value Proposition**
    - Type-erasing wrappers allow APIs independent of underlying transport
    - Same code works with corosio sockets, Boost.Asio adaptors, TLS streams, mock streams
    - Echo server example:
      ```cpp
      task<> handle_connection(any_stream& stream) {
          char buf[1024];
          for(;;) {
              auto [ec, n] = co_await stream.read_some(mutable_buffer(buf));
              if(ec == cond::eof) break;
              if(ec.failed()) co_return;
              auto [wec, wn] = co_await write(stream, const_buffer(buf, n));
              if(wec.failed()) co_return;
          }
      }
      ```
    - HTTP body example:
      ```cpp
      task<> send_body(any_write_sink& body, std::string_view data) {
          auto [ec, n] = co_await body.write(make_buffer(data), true);
      }
      ```
    - The concrete transport is hidden; library code works with any conforming implementation

## Section: Testing Facilities

### Part I: Test Execution

1. **run_blocking**
   - Execute coroutines synchronously for unit tests
   - Uses `inline_executor` that executes inline via `dispatch()`
   - Blocks the calling thread until the task completes
   - Supports handlers for results and exceptions
   - Supports `std::stop_token` for cancellation
   - Reference: `<boost/capy/test/run_blocking.hpp>`

2. **fuse**
   - Systematic error injection at successive failure points
   - Two phases: error code mode, then exception mode
   - `armed()`: runs test repeatedly, failing at each `maybe_fail()` point
   - `inert()`: runs test once, `maybe_fail()` always succeeds
   - `fail()`: explicit test failure with source location tracking
   - Dependency injection: no-op when used outside `armed()`/`inert()`
   - Works with both regular functions and coroutines
   - Reference: `<boost/capy/test/fuse.hpp>`

### Part II: Buffer Testing

3. **bufgrind**
   - Iterates all split points of a buffer sequence
   - Returns `(b1, b2)` pairs where concatenation equals original
   - Configurable step size for faster iteration
   - Preserves mutability: mutable input yields mutable slices
   - Reference: `<boost/capy/test/bufgrind.hpp>`

4. **buffer_to_string**
   - Converts buffer sequences to `std::string`
   - Variadic: concatenates multiple buffer sequences
   - Useful for verifying buffer contents in tests
   - Reference: `<boost/capy/test/buffer_to_string.hpp>`

### Part III: Mock Streams

5. **read_stream**
   - Mock `ReadStream` for testing read operations
   - `provide(sv)`: supply data for subsequent reads
   - `read_some(buffers)`: partial read, returns `(error_code, size_t)`
   - `max_read_size` constructor parameter simulates chunked delivery
   - Integrated with `fuse` for error injection
   - Returns `error::eof` when no data remains
   - Reference: `<boost/capy/test/read_stream.hpp>`

6. **write_stream**
   - Mock `WriteStream` for testing write operations
   - `write_some(buffers)`: partial write, returns `(error_code, size_t)`
   - `data()`: retrieve written data as string view
   - `expect(sv)`: set expected data, fails on mismatch
   - `max_write_size` constructor parameter simulates chunked delivery
   - Integrated with `fuse` for error injection
   - Reference: `<boost/capy/test/write_stream.hpp>`

### Part IV: Mock Sources and Sinks

7. **read_source**
   - Mock `ReadSource` for testing complete read operations
   - `provide(sv)`: supply data for subsequent reads
   - `read(buffers)`: complete read, fills buffers entirely
   - Integrated with `fuse` for error injection
   - Returns `error::eof` when no data remains
   - Reference: `<boost/capy/test/read_source.hpp>`

8. **write_sink**
   - Mock `WriteSink` for testing complete write operations
   - `write(buffers)`: complete write, returns `(error_code, size_t)`
   - `write(buffers, eof)`: write with optional EOF signal
   - `write_eof()`: signal end-of-stream, returns `(error_code)`
   - `data()`: retrieve written data as string view
   - `expect(sv)`: set expected data, fails on mismatch
   - `eof_called()`: check if EOF was signaled
   - Integrated with `fuse` for error injection
   - Reference: `<boost/capy/test/write_sink.hpp>`

