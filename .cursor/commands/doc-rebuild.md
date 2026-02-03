# Rebuild Capy Documentation

Regenerate the Capy documentation following this structure and style guide.

## Documentation Structure

### 1. Introduction Page (`index.adoc`)

**Opening paragraph** (verbatim, do not change):

> Capy abstracts away sockets, files, and asynchrony with type-erased streams and buffer sequences—code compiles fast because the implementation is hidden. It provides the framework for concurrent algorithms that transact in buffers of memory: networking, serial ports, console, timers, and any platform I/O. This is only possible because Capy is coroutine-only, enabling optimizations and ergonomics that hybrid approaches must sacrifice.

**Required sections** (in order):

1. Title + Opening Paragraph (above)
2. **What This Library Does** (verbatim):
  - Lazy coroutine tasks — `task<T>` with forward-propagating stop tokens and automatic cancellation
  - Buffer sequences — taken straight from Asio and improved
  - Stream concepts — `ReadStream`, `WriteStream`, `ReadSource`, `WriteSink`, `BufferSource`, `BufferSink`
  - Type-erased streams — `any_stream`, `any_read_stream`, `any_write_stream` for fast compilation
  - Concurrency facilities — executors, strands, thread pools, `when_all`, `when_any`
  - Test utilities — mock streams, mock sources/sinks, error injection
3. **What This Library Does Not Do** (verbatim):
  - Networking — no sockets, acceptors, or DNS; that's what Corosio provides
  - Protocols — no HTTP, WebSocket, or TLS; see the Http and Beast2 libraries
  - Platform event loops — no io_uring, IOCP, epoll, or kqueue; Capy is the layer above
  - Callbacks or futures — coroutine-only means no other continuation styles
  - Sender/receiver — Capy uses the IoAwaitable protocol, not `std::execution`
4. **Target Audience** (verbatim):
  - Users of Corosio — portable coroutine networking
  - Users of Http — sans-I/O HTTP/1.1 clients and servers
  - Users of Websocket — sans-I/O WebSocket
  - Users of Beast2 — high-level HTTP/WebSocket servers
  - Users of Burl — high-level HTTP client
   All of these are built on Capy. Understanding its concepts—tasks, buffer sequences, streams, executors—unlocks the full power of the stack.
5. **Design Philosophy** (verbatim):
  - **Use case first.** Buffer sequences, stream concepts, executor affinity—these exist because I/O code needs them, not because they're theoretically elegant.
  - **Coroutines-only.** No callbacks, futures, or sender/receiver. Hybrid support forces compromises; full commitment unlocks optimizations that adapted models cannot achieve.
  - **Address the complaints of C++.** Type erasure at boundaries, minimal dependencies, and hidden implementations keep builds fast and templates manageable.
6. **Requirements** (verbatim):
  **Assumed Knowledge:**
  - C++20 coroutines, concepts, and ranges
  - Basic concurrent programming
   **Compiler Support:**
  - GCC 12+
  - Clang 17+
  - Apple-Clang (macOS 14+)
  - MSVC 14.34+
  - MinGW
   **Dependencies:**
  - None. Capy is self-contained and does not require Boost.
   **Linking:**
  - Capy is a compiled library. Link against `capy`.
7. Code Convention Note - Callout with standard includes/namespaces
8. Quick Example - Minimal working code
9. Next Steps - Links to Quick Start, tutorials, reference

### 2. C++20 Coroutines Tutorial (Second Section)

**Source**: `doc/reference/coro-tutorial.md`

**Instructions**: Insert this tutorial as the second major documentation section. Apply only:

- **Resectioning**: Organize into logical parts matching the existing structure in agent-guide.md
- **Pagination**: Split into multiple pages for reading pace

**Target page structure** (from agent-guide.md):

- Part I: Foundations (`cpp20-coroutines/foundations.adoc`)
  - Functions and the Call Stack
  - What Is a Coroutine?
  - Why Coroutines?
- Part II: C++20 Syntax (`cpp20-coroutines/syntax.adoc`)
  - The Three Keywords
  - Your First Coroutine
  - Awaitables and Awaiters
- Part III: Coroutine Machinery (`cpp20-coroutines/machinery.adoc`)
  - The Promise Type
  - Coroutine Handle
  - Putting It Together
- Part IV: Advanced Topics (`cpp20-coroutines/advanced.adoc`)
  - Symmetric Transfer
  - Coroutine Allocation
  - Exception Handling

**Mapping from tutorial steps**:

- Steps 1-3 → Part I (Foundations)
- Steps 2, 4-5 → Part II (Syntax)
- Steps 4, 6-7 → Part III (Machinery)
- Steps 8-10 → Part IV (Advanced)

### 3. Concurrency Tutorial (Third Section)

**Source**: `doc/reference/concurrency-2.md`

**Instructions**: Insert this tutorial as the third major documentation section. Apply only:

- **Resectioning**: Organize into logical parts for pacing
- **Pagination**: Split into multiple pages

**Target page structure**:

- Part I: Foundations (`concurrency/foundations.adoc`)
  - Why Concurrency Matters
  - Threads—Your Program's Parallel Lives
  - Creating Threads
  - Thread Lifecycle
- Part II: Synchronization (`concurrency/synchronization.adoc`)
  - Race Conditions
  - Mutexes
  - Lock Guards—RAII
  - Deadlocks
- Part III: Advanced Primitives (`concurrency/advanced.adoc`)
  - Atomics
  - Condition Variables
  - Shared Locks (Readers/Writers)
- Part IV: Communication & Patterns (`concurrency/patterns.adoc`)
  - Futures and Promises
  - std::async
  - Thread-Local Storage
  - Practical Patterns (Producer-Consumer, Parallel For)

**Mapping from tutorial parts**:

- Parts 1-4 → Foundations
- Parts 5-8 → Synchronization
- Parts 9-11 → Advanced Primitives
- Parts 12-16 → Communication & Patterns

### 4. Coroutines in Capy (Fourth Section)

This section transitions from general C++ knowledge to Capy-specific library usage. Generate content based on public API and agent-guide.md.

**Target page structure** (`coroutines/`):

- **The task Type** (`coroutines/tasks.adoc`)
  - Declaring `task<T>` coroutines
  - Returning values with `co_return`
  - Awaiting other tasks
  - Lazy execution and symmetric transfer
- **Launching Coroutines** (`coroutines/launching.adoc`)
  - `run_async` — entry point from non-coroutine code
  - Two-call syntax and C++17 evaluation order
  - `run` — executor hopping within coroutine code
  - Handler overloads for results and exceptions
  - The execution model: how tasks get scheduled and resumed
- **Executors and Execution Contexts** (`coroutines/executors.adoc`)
  - The `Executor` concept: `dispatch()` and `post()`
  - `dispatch()` vs `post()` — inline execution vs always queue
  - `executor_ref` — type-erased executor wrapper
  - `thread_pool` — multi-threaded execution context
  - `execution_context` — base class for custom contexts
  - `strand` — serialization without mutexes
  - Single-threaded vs multi-threaded patterns
- **The IoAwaitable Protocol** (`coroutines/io-awaitable.adoc`)
  - The three-argument `await_suspend` signature
  - Forward context propagation vs backward queries
  - `IoAwaitable`, `IoAwaitableTask`, `IoLaunchableTask` concepts
  - Why affinity matters for I/O
- **Stop Tokens and Cancellation** (`coroutines/cancellation.adoc`)
  **Teach from the ground up for complete beginners:**
  *Part 1: The Problem*
  - Why cancellation matters: user hits "Cancel", timeout expires, connection drops
  - The naive approach: boolean flags — why they don't work (races, no standardization)
  - The thread interruption problem: forceful termination corrupts state
  - The goal: cooperative cancellation — ask nicely, let the work clean up
  *Part 2: C++20 Stop Tokens — A General-Purpose Signaling Mechanism*
  **Key insight**: `stop_token` is not merely a cancellation primitive—it implements the Observer pattern, a thread-safe one-to-many notification system. The "stop" naming obscures its generality.
  **The three components**:
  - `std::stop_source` — the **Subject/Publisher**: owns shared state, triggers notifications
  - `std::stop_token` — the **Subscriber View**: read-only, copyable, cheap to pass around
  - `std::stop_callback<F>` — the **Observer Registration**: RAII callback that runs when signaled
  **How they work together**:
  - Source creates tokens via `get_token()`
  - Multiple tokens can share the same state (distribute notification capability)
  - Callbacks register interest; destruction unregisters automatically
  - When `request_stop()` is called, all registered callbacks are invoked
  - **Immediate invocation**: if already signaled, callback runs in constructor
  - Thread-safe: registration and invocation are safe from any thread
  **Type-erased polymorphic observers**:
  - Each `stop_callback<F>` stores a different callable type `F`
  - No virtual functions, no heap allocation per callback
  - Equivalent to `vector<function<void()>>` but with RAII lifetime management
  **The one-shot nature** (BIG WARNING):
  - Can only transition from "not signaled" to "signaled" once
  - No reset mechanism — once `stop_requested()` returns true, it stays true forever
  - `request_stop()` returns `true` only on the first successful call
  - **NOT REUSABLE**: You cannot "un-cancel" a stop_source
  **How to "reset" (workaround)**:
  - Create a new `stop_source` (assigns fresh shared state)
  - Call `get_token()` on the new source
  - Distribute the new token to all components that need it
  - Components must replace their old `stop_token` with the new one
  - This is manual and error-prone — design your system to avoid needing resets
  **Example of the reset pattern**:
  ```cpp
  std::stop_source source;
  // ... distribute source.get_token() to workers ...
  source.request_stop();  // triggered, now permanently signaled

  // To "reset": create entirely new source
  source = std::stop_source{};  // new shared state
  // Must redistribute new tokens to ALL holders of the old token
  // Old tokens are now orphaned (stop_possible() returns false)
  ```
  **Design implication**: If you need repeatable signals, stop_token is the wrong tool. Use condition variables, atomic flags with explicit protocol, or wait for a future resettable signal facility.
  **Beyond cancellation** (the naming hides this):
  - Starting things: "ready" signal triggers initialization
  - Configuration loaded: notify components when config is available
  - Resource availability: signal when database connected, cache warmed
  - Any one-shot broadcast notification scenario
  *Part 3: Stop Tokens in Coroutines*
  - The propagation problem: how does a nested coroutine know to stop?
  - Capy's answer: stop tokens flow downward through `co_await`
  - `get_stop_token()` — retrieve the current stop token inside a task
  - Automatic propagation: child tasks inherit parent's stop token
  - No manual threading: the IoAwaitable protocol handles it
  *Part 4: Responding to Cancellation*
  - Checking: `if (token.stop_requested()) co_return;`
  - Cleanup: RAII ensures resources are released on early exit
  - Partial results: returning what you have vs throwing
  - The `operation_aborted` error code convention
  *Part 5: OS Integration*
  - How stop tokens connect to platform I/O (IOCP, io_uring)
  - Cancelling a pending read/write at the OS level
  - Immediate response vs next-operation-fails
  *Part 6: Patterns*
  - Timeout pattern: `stop_source` + timer → cancel after N seconds
  - User cancellation: UI button triggers `stop_source.request_stop()`
  - Graceful shutdown: cancel all pending work, wait for cleanup
  - `when_any` cancellation: first-to-finish cancels siblings
- **Concurrent Composition** (`coroutines/composition.adoc`)
  - `when_all` — run tasks in parallel, wait for all to complete
  - Result tuple and void filtering
  - `when_any` — run tasks in parallel, return when first completes
  - Stop propagation across siblings — cancelling the losers
  - Error handling: which exceptions propagate?
- **Frame Allocators** (`coroutines/allocators.adoc`)
  - The timing constraint: `operator new` before coroutine body
  - Thread-local propagation and "the window"
  - The `FrameAllocator` concept
  - HALO optimization support

**Reference headers**:

- `<capy/task.hpp>`
- `<capy/ex/run.hpp>`, `<capy/ex/run_async.hpp>`
- `<capy/ex/thread_pool.hpp>`, `<capy/ex/execution_context.hpp>`
- `<capy/ex/executor_ref.hpp>`, `<capy/ex/strand.hpp>`
- `<capy/concept/io_awaitable.hpp>`, `<capy/concept/io_awaitable_task.hpp>`
- `<capy/concept/frame_allocator.hpp>`
- `<capy/when_all.hpp>`, `<capy/when_any.hpp>`

### 5. Buffer Sequences (Fifth Section)

Generate content based on public API and agent-guide.md.

**Core thesis to integrate throughout this section:**

The reflexive C++ answer to "how should I represent a buffer?" is `std::span<std::byte>`. This blocks compositional design. For scatter/gather, developers reach for `span<span<byte>>`—but arrays of buffers don't compose without allocation. To combine `HeaderBuffers` (2 spans) and `BodyBuffers` (3 spans), you must allocate a new array. Every composition allocates. This leads to overload proliferation: separate signatures for single buffer, scatter/gather, string, C API, etc.

The concept-driven alternative: a single templated signature accepting any type modeling `ConstBufferSequence`. This accepts spans, string_views, arrays, vectors, custom types—and **any composition of these without allocation**. The key insight from STL design (Stepanov): algorithms parameterized on concepts, not concrete types, enable composition that concrete types forbid.

Even `std::byte` imposes a semantic opinion. POSIX uses `void*` for semantic neutrality—"raw memory, I move bytes without opining on contents." But `span<void>` doesn't compile. Capy provides `const_buffer` and `mutable_buffer` as semantically neutral buffer types with known layout.

**The middle ground**: Concepts at user-facing APIs (composition, flexibility), concrete spans at type-erasure boundaries (virtual functions). The library handles conversion between layers.

**Target page structure** (`buffers/`):

- **Why Concepts, Not Spans** (`buffers/overview.adoc`)
  - **The I/O use case**: buffers exist to interface with operating system I/O
  - The reflexive answer: `span<byte>` for buffers, `span<span<byte>>` for scatter/gather
  - **The composition problem**: combining buffer sequences requires allocation with spans
  - Example: HTTP headers + body — must allocate to combine with `span<span<byte>>`
  - **The concept-driven solution**: `ConstBufferSequence` accepts any iterable of memory regions
  - Single signature accepts span, string_view, array, vector, custom types
  - Compile-time composition: `cat(header_buffers, body_buffers)` — zero allocation
  - **STL parallel**: Stepanov's insight — algorithms on iterators (concepts), not containers (types)
  - The span reflex is a regression from thirty years of generic programming
- **Buffer Types** (`buffers/types.adoc`)
  - **Why not `std::byte`?** It imposes semantic opinion; POSIX `void*` is neutral
  - `span<void>` doesn't compile — can't express type-agnostic abstraction
  - `const_buffer` — semantically neutral read-only view of contiguous memory
  - `mutable_buffer` — semantically neutral writable view
  - Construction, accessors, prefix removal
  - `make_buffer` — from pointer+size, arrays, vectors, strings
  - **Layout compatibility**: same memory layout as `iovec`/`WSABUF` — no conversion overhead
- **Buffer Sequences** (`buffers/sequences.adoc`)
  - What is a buffer sequence? Bidirectional range with buffer-convertible value type
  - Single buffers are degenerate sequences (one-element range)
  - `ConstBufferSequence` and `MutableBufferSequence` concepts
  - **Heterogeneous composition**: mix string_view, span, custom types — all work
  - Iterating: `begin()`, `end()`, uniform access
  - `consuming_buffers` for incremental consumption
  - **Zero-allocation composition**: combining sequences creates views, not copies
- **System I/O Integration** (`buffers/system-io.adoc`)
  - **The virtual boundary**: spans ARE correct at type-erasure points
  - User-facing API: concepts for composition flexibility
  - Internal virtual boundary: `span<span<byte>>` for type erasure
  - Library converts between layers — users get concepts, OS gets iovecs
  - Translating buffer sequences to `iovec` arrays (POSIX)
  - Translating to `WSABUF` arrays (Windows)
  - Stack-based conversion for small sequences (common case, zero heap)
  - Heap fallback for large sequences
  - The `registered_buffer` optimization (io_uring, IOCP)
- **Buffer Algorithms** (`buffers/algorithms.adoc`)
  - Measuring: `buffer_size`, `buffer_empty`, `buffer_length`
  - Copying: `buffer_copy` with optional `at_most` parameter
  - Real I/O loop patterns from `read()` and `write()`
  - **Practical benefits of concept-based design**:
    - Zero-copy I/O (data never moves unnecessarily)
    - Scatter/gather operations (multiple buffers in one syscall)
    - Custom allocators and memory-mapped buffers
    - Integration with any user-defined buffer type
- **Dynamic Buffers** (`buffers/dynamic.adoc`)
  - The producer/consumer model
  - The `DynamicBuffer` concept: `prepare(n)`, `commit(n)`, `data()`, `consume(n)`
  - Capacity management: `size()`, `max_size()`, `capacity()`
  - `DynamicBufferParam` for safe coroutine parameter passing
  - Implementations: `flat_dynamic_buffer`, `circular_dynamic_buffer`, `vector_dynamic_buffer`, `string_dynamic_buffer`

**Reference headers**:

- `<capy/buffers.hpp>`
- `<capy/buffers/const_buffer.hpp>`, `<capy/buffers/mutable_buffer.hpp>`
- `<capy/buffers/make_buffer.hpp>`
- `<capy/buffers/buffer_copy.hpp>`, `<capy/buffers/consuming_buffers.hpp>`
- `<capy/concept/dynamic_buffer.hpp>`
- `<capy/flat_dynamic_buffer.hpp>`, `<capy/circular_dynamic_buffer.hpp>`

### 6. Stream Concepts (Sixth Section)

Generate content based on public API and agent-guide.md. **Key structure**: For each concept, introduce it, then immediately show its type-erasing wrapper, then demonstrate physical isolation benefits.

**Target page structure** (`streams/`):

- **Overview** (`streams/overview.adoc`)
  - Six concepts for data flow through programs
  - Streams vs Sources/Sinks vs Buffer concepts
  - The type erasure value proposition: compile once, link anywhere
- **Streams (Partial I/O)** (`streams/streams.adoc`)
  - `ReadStream` — `read_some(buffers)` returns `(error_code, size_t)`
  - `any_read_stream` — type-erased wrapper, reference semantics
  - `WriteStream` — `write_some(buffers)` returns `(error_code, size_t)`
  - `any_write_stream` — type-erased wrapper
  - `any_stream` — bidirectional wrapper (both read and write)
  - **Example**: Echo server with `any_stream&` parameter — works with sockets, TLS, mocks
- **Sources and Sinks (Complete I/O with EOF)** (`streams/sources-sinks.adoc`)
  - `ReadSource` — `read(buffers)` fills entirely or returns EOF/error
  - `any_read_source` — type-erased wrapper
  - `WriteSink` — `write(buffers)`, `write(buffers, eof)`, `write_eof()`
  - `any_write_sink` — type-erased wrapper
  - **Example**: HTTP body handler with `any_write_sink&` — caller doesn't know chunked vs content-length
- **Buffer Sources and Sinks (Callee-Owns-Buffers)** (`streams/buffer-concepts.adoc`)
  - `BufferSource` — `pull(arr, max_count)` returns buffer descriptors
  - `any_buffer_source` — type-erased wrapper
  - `BufferSink` — `prepare(arr, max_count)`, `commit(n)`, `commit_eof()`
  - `any_buffer_sink` — type-erased wrapper
  - Zero-copy: source/sink owns buffers, no intermediate copies
  - **Example**: Compression pipeline — source provides compressed data, sink receives decompressed
- **Transfer Algorithms** (`streams/algorithms.adoc`)
  - `read(stream, buffers)` — loops `read_some` until full or error
  - `read(source, dynamic_buffer)` — loops until EOF
  - `write(stream, buffers)` — loops `write_some` until all written
  - `push_to(BufferSource, WriteSink/WriteStream)` — caller-owns-buffers transfer
  - `pull_from(ReadSource/ReadStream, BufferSink)` — callee-owns-buffers transfer
- **Physical Isolation** (`streams/isolation.adoc`)
  - The compilation firewall pattern
  - Header declares `task<> process(any_stream&)` — no template, no transport dependency
  - Implementation in `.cpp` — only this file recompiles when logic changes
  - Callers wrap concrete streams: `any_stream s{my_tcp_socket}; process(s);`
  - Build time benefits: faster incremental builds, smaller binaries
  - **Example**: Library API that accepts `any_read_source&` for body data — works with files, memory, network

**Wrapper characteristics** (document in each relevant page):

- Reference semantics: wrap existing objects without ownership
- Preallocate coroutine frame at construction for zero steady-state allocation
- Move-only (non-copyable); cached frame reused across operations
- Wrapped object must outlive wrapper

**Reference headers**:

- `<capy/concept/read_stream.hpp>`, `<capy/concept/write_stream.hpp>`
- `<capy/concept/read_source.hpp>`, `<capy/concept/write_sink.hpp>`
- `<capy/concept/buffer_source.hpp>`, `<capy/concept/buffer_sink.hpp>`
- `<capy/io/any_stream.hpp>`, `<capy/io/any_read_stream.hpp>`, `<capy/io/any_write_stream.hpp>`
- `<capy/io/any_read_source.hpp>`, `<capy/io/any_write_sink.hpp>`
- `<capy/io/any_buffer_source.hpp>`, `<capy/io/any_buffer_sink.hpp>`
- `<capy/read.hpp>`, `<capy/write.hpp>`
- `<capy/io/push_to.hpp>`, `<capy/io/pull_from.hpp>`

### 7. Example Programs (Seventh Section)

A catalog of complete, working example programs. **One listing per page.** Each example demonstrates a focused use case with full source code, build instructions, and explanation.

Generate examples that showcase Capy features. Examples should progress from simple to complex.

**Target page structure** (`examples/`):

- **Hello Task** (`examples/hello-task.adoc`)
  - Simplest possible `task<>` coroutine
  - `run_async` to launch from main
  - `thread_pool` as execution context
  - Shows: basic task creation, launching, completion
- **Producer-Consumer** (`examples/producer-consumer.adoc`)
  - Two tasks communicating via `async_event`
  - Demonstrates coroutine synchronization
  - Shows: `async_event`, `when_all`, multiple concurrent tasks
- **Buffer Composition** (`examples/buffer-composition.adoc`)
  - Composing HTTP-style message: headers + body
  - Zero-allocation buffer sequence composition
  - Shows: `const_buffer`, buffer sequences, `cat()`, scatter/gather
- **Mock Stream Testing** (`examples/mock-stream-testing.adoc`)
  - Unit testing a protocol parser with mock streams
  - Error injection with `fuse`
  - Shows: `test::read_stream`, `test::write_stream`, `fuse`, `run_blocking`
- **Type-Erased Echo** (`examples/type-erased-echo.adoc`)
  - Echo logic in a `.cpp` file accepting `any_stream&`
  - Demonstrates physical isolation / compilation firewall
  - Shows: `any_stream`, type erasure, build isolation
- **Timeout with Cancellation** (`examples/timeout-cancellation.adoc`)
  - Operation with timeout using stop tokens
  - Demonstrates cooperative cancellation
  - Shows: `std::stop_source`, `std::stop_token`, cancellation propagation
- **Parallel Fetch** (`examples/parallel-fetch.adoc`)
  - Multiple operations in parallel with `when_all`
  - First-wins pattern with `when_any`
  - Shows: `when_all`, `when_any`, concurrent composition
- **Custom Dynamic Buffer** (`examples/custom-dynamic-buffer.adoc`)
  - Implementing `DynamicBuffer` for a custom allocation strategy
  - Shows: concept modeling, `prepare`/`commit`/`consume` pattern
- **Echo Server with Corosio** (`examples/echo-server-corosio.adoc`)
  - Complete echo server using Corosio sockets
  - Demonstrates Capy + Corosio integration
  - Shows: `tcp::acceptor`, `tcp::socket`, `any_stream`, real networking
  - Requires: Corosio library
- **Stream Pipeline** (`examples/stream-pipeline.adoc`)
  - Data transformation chain: source → transform → sink
  - Demonstrates `BufferSource` and `BufferSink` composition
  - Shows: `push_to`, `pull_from`, processing chains

**Example format** (each page):

1. **Title and one-sentence description**
2. **What you'll learn** (bullet points)
3. **Prerequisites** (other examples or sections to read first)
4. **Full source code** (complete, compilable)
5. **Build instructions** (CMake snippet)
6. **Walkthrough** (explain key sections)
7. **Exercises** (optional variations to try)

---

## Style Guide

**Apply these rules throughout all documentation.**

### Tone

- **Second person**: "You will configure," "You create a task," not "I think" or "We will learn"
- **Avoid assumptions**: Never use "simple," "easy," "obviously," "just," "straightforward" — these frustrate readers who struggle
- **Friendly but formal**: No jargon, memes, slang, or emoji
- **Focus on outcomes, not process**: Instead of "we will learn how to install," write "you will install"

### Structure (Each Section/Page)

- **Introduction**: What is it? Why learn it? What will you do? What will you accomplish?
- **Prerequisites**: Explicit checklist with links to prior sections
- **Steps**: Procedural, numbered, each with intro sentence and closing transition
- **Conclusion**: Summarize accomplishments, suggest next steps

### Section Arc

- Opening (what reader will accomplish) → Simple case with code → Build complexity → Bridge to next
- Reader should have something working at the end — practical, not theoretical

### Transitions

- Each step ends with what was accomplished and where they're going next
- Provides context and motivation to continue
- Example: "You have now created your first task. Next, you will learn how to launch it on an executor."

### Code Blocks

- **Before**: High-level explanation of what the code does and why
- **Code**: Show the complete, compilable snippet
- **After**: Explain important details, gotchas, variations
- Every command gets a description before and explanation after

### Exposition/Code Balance

- Early sections: more prose, full paragraphs, short code snippets (API flavor)
- Middle sections: longer examples as concepts build
- End sections: full programs if appropriate — skip boilerplate (long include lists)

### Content Rules

- **What/Why/How order**: Always explain in that sequence
- **Gotchas over happy-path**: Document thread safety, lifetimes, platform quirks prominently
- **Grammar**: "That" (essential, no comma) vs "which" (nonessential, comma)
- **No unexplained forward refs**: Introduce concepts before referencing them
- **Prose over member lists**: Don't enumerate all class members; write prose about important ones with example code
- **Comprehensiveness without assumptions**: Explicitly include everything the reader needs

### Banned Phrases

- "Simply" / "Just" / "Easy" / "Obviously" / "Straightforward"
- "As you can see" / "It's clear that"
- "We" when referring to the reader (use "you")
- "I" (first person singular)

---

## Agent Guide Reference

Point to `doc/agent-guide.md` for:

- Capy-specific section outlines (C++20 Coroutines, I/O Awaitables, Buffers, etc.)
- Extra instructions (use `thread_pool` in examples)
- Requirements notes

---

## Rebuild Process

### Phase 1: Outline

- Read `doc/` structure and public API headers
- Create section/page structure with one-line descriptions per page
- Identify gaps: public symbols without documentation, stale references

### Phase 2: Linear Generation

- Work section by section, page by page, in order
- Complete each page fully before moving to the next
- Apply style guide rules throughout
- Reference agent-guide.md directives for Capy-specific structure

### Phase 3: Validation

- Verify all public symbols are documented
- Check cross-references resolve correctly
- Remove stale references to renamed/deleted API
- Confirm examples compile (or note dependencies)
