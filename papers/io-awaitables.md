| Document | BOOST1000 |
|----------|-------|
| Date:       | 2026-01-21
| Reply-to:   | Vinnie Falco \<vinnie.falco@gmail.com\><br>Steve Gerbino \<steve@gerbino.co\><br>Amlal El Mahrouss \<amlalelmahrouss@icloud.com\>
| Audience:   | Boost, C++

---

# IoAwaitables: A Coroutines-First Execution Model

## Abstract

This paper asks: *what would an execution model look like if designed from the ground up for coroutine-only asynchronous I/O?*

An execution model answers how asynchronous work is scheduled and dispatched, where operation state is allocated and by whom, and what customization points allow users to adapt the framework's behavior. We propose a **coroutines-only** execution model optimized for CPU-bound I/O workloads. The framework introduces the _IoAwaitable_ protocol: a system for associating a coroutine with an executor, allocator, and stop token, and propagating this context forward through a coroutine chain which may end at an operating system API boundary where asynchronous operations are performed.

We compare our use-case-first driven design against `std::execution` (P2300) and observe significant divergence. Analysis of P2300 and its evolution (P3826) reveals a framework driven by GPU and parallel workloads—P3826's focus is overwhelmingly GPU/parallel with no networking discussion. Core networking concerns—strand serialization, I/O completion contexts, platform integration—remain unaddressed. The query-based context model that P3826 attempts to fix is unnecessary when context propagates forward through coroutine machinery.

Our framework demonstrates what emerges when networking requirements drive the design rather than being adapted to a GPU-focused abstraction.

---

## 1. Introduction

The C++ standardization effort has produced `std::execution` (P2300), a sender/receiver framework for structured concurrency. Its design accommodates heterogeneous computing—GPU kernels, parallel algorithms, and hardware accelerators. The machinery is substantial: domains select algorithm implementations, queries determine completion schedulers, and transforms dispatch work to appropriate backends.

But what does asynchronous I/O actually need?

A network server completes read operations on I/O threads, resumes handlers on application threads, and serializes access to connection state. A file system service batches completions through io_uring, dispatches callbacks to worker pools, and manages buffer lifetimes across suspension points. These patterns repeat across every I/O-intensive application. None of them require domain-based algorithm dispatch. TCP servers do not run on GPUs. Socket reads have one implementation per platform, not a menu of hardware backends.

**This paper explores what emerges when I/O requirements drive the design.**

We find that two operations suffice: `dispatch` for continuations and `post` for independent work. The choice between them is correctness, not performance. Using dispatch while holding a mutex invites deadlock. Using post for every continuation wastes cycles bouncing through queues. But primitives alone don't solve the composition problem. In a chain like `async_http_request → async_read → socket`, who decides which allocator to use? Who determines which executor runs the completion? The answers are discovered from studying the use case:

- **The application decides executor policy.** Thread affinity, strand serialization, priority scheduling; these are deployment decisions. A read operation doesn't care which thread pool it runs on. The application architecture determines where completions should resume.

- **The application decides allocation policy.** Memory strategies: recycling pools, bounded allocation, per-tenant budgets; these are a property of the coroutine chain, not the I/O operation. A socket doesn't care about memory policy; the context in which it runs does.

- **The application sends stop signals.** Cancellation flows downward from application logic, not upward from I/O operations. A timeout, user abort, or graceful shutdown originates at the application layer and propagates to pending operations. The socket doesn't decide when to cancel itself.

- **The execution context owns its I/O objects.** A socket registered with epoll on thread A cannot have completions delivered to thread B's epoll. The physical coupling is inherent. The call site cannot know which event loop the socket uses—only the socket knows.

Execution context flows naturally *forward* through async operation chains. The executor, allocator, and stop token propagate from caller to callee at each suspension point—no backward queries, no completion scheduler inference, no domain transforms. This forward flow eliminates the late-binding problem that `std::execution` struggles to fix: context is known at launch site, not discovered through receiver queries after the work graph is built. Our goal is not to replace `std::execution` for GPU workloads—it is to demonstrate that networking deserves a purpose-built abstraction rather than adaptation to a framework optimized for different requirements.

> **Convention.** Throughout this document we use the following type alias:
> ```cpp
> using coro = std::coroutine_handle<void>;
> ```

---

## 2. What Networking Needs

Networking has specific requirements that differ from parallel computation. A socket read has **one implementation per platform**—there's no algorithm selection, no hardware heterogeneity to manage. What networking actually needs:

- **Platform implementation**: Integration with IOCP, epoll, io_uring
- **Strand serialization**: Ordering guarantees for concurrent access
- **Thread affinity**: Handlers must resume on the correct thread
- **Inline vs queued**: Choosing whether continuations run immediately or are deferred
- **Stack depth control**: Bounding recursion through trampolining or deferred dispatch

Strip these requirements down to their essence. What primitive do they all depend on? **Something that runs work.** Completion contexts are executors. Strands wrap executors. Thread affinity is an executor property. Everything networking needs builds on this primitive. P2762 acknowledges this reality in §4.1:

> "It was pointed out networking objects like sockets are specific to a context: that is necessary when using I/O Completion Ports or a TLS library and yields advantages when using, e.g., io_uring(2)."

### 2.1 The Executor

An executor is to functions what an allocator is to memory: while an allocator controls *where* objects live, an executor controls *where* functions run. Examining decades of asynchronous I/O patterns reveals that a minimal executor needs only two operations. The first operation handles work that continues the current operation—a completion handler that should run as part of the same logical flow. The second handles work that must begin independently—a task that cannot start until after the initiating call returns. These two cases have distinct correctness requirements, captured by `dispatch` and `post`.

#### `dispatch`

When an I/O operation completes at the operating system level, the completion handler must run. The kernel has signaled that data is ready or a connection is established; now user code must process the result. In this context, running the handler immediately—on the current thread, without queuing—is both safe and efficient. No locks are held by the I/O layer. The completion is a natural continuation of the event loop's work.

This pattern is captured by `dispatch`. It runs the provided function immediately if doing so is safe within the executor's rules, or queues it otherwise. For a strand, "safe" means no other work is currently executing on that strand. For a thread pool, it might mean the caller is already on a pool thread. The executor decides; the caller simply requests execution.

This is the common case for I/O completions. The event loop calls `dispatch` with the handler, and in the typical case the handler runs inline without touching any queue. The result is minimal latency and zero allocation for the dispatch itself.

#### `post`

Sometimes inline execution is not just inefficient but incorrect. Consider code that initiates an async operation while holding a mutex, expecting the lock to be released before the completion runs. Or a destructor that launches cleanup work and must return before that work begins. These patterns require a guarantee: the continuation will not execute before the call returns.

The `post` operation provides this guarantee. It always queues work for later execution, never running inline. The caller can rely on control returning before the posted function begins. This makes `post` suitable for breaking call chains, avoiding reentrancy, and ensuring ordering when the caller's state must settle before continuation.

The cost is queue insertion and eventual dequeue—cycles that `dispatch` avoids when inline execution is safe. But when correctness requires deferred execution, `post` is the only sound choice.

### 2.2 The Allocator

Neither callbacks nor coroutines achieve competitive performance without allocation control. Early testing showed that fully type-erased callbacks—using `std::function` at every composition boundary—allocated hundreds of times per invocation and ran an order of magnitude slower than native templates. Non-recycling coroutine configurations perform so poorly, dominated by heap allocation overhead, that they are not worth benchmarking.

**Recycling is mandatory for performance.** Thread-local recycling—caching recently freed memory for immediate reuse—enables zero steady-state allocations. Callbacks achieve this through "delete before dispatch": deallocate operation state before invoking the completion handler. Coroutines achieve it through frame pooling: custom `operator new/delete` recycles frames across operations.

But thread-local recycling optimizes for throughput, not all applications. Different deployments have different needs:

- **Memory conservation**: Embedded systems or resource-constrained environments may prefer bounded pools over unbounded caches
- **Per-tenant limits**: Multi-tenant servers need allocation budgets per client to prevent resource exhaustion
- **Debugging and profiling**: Development builds may want allocation tracking that production builds eliminate

Coroutines are continuously created—every `co_await` may spawn new frames. An execution model must make allocation a first-class customization point that controls *all* coroutine allocations, not just selected operations. Without this, memory policy becomes impossible to enforce.

**The allocator must be present at invocation.** Coroutine frame allocation has a fundamental timing constraint: `operator new` executes before the coroutine body. When a coroutine is called, the compiler allocates the frame first, then begins execution. Any mechanism that injects context later—receiver connection, `await_transform`, explicit method calls—arrives too late.

```cpp
auto t = my_coro(sock);  // operator new called HERE
co_await t;              // await_transform kicks in HERE (too late)
```

P2762 acknowledges that the receiver-based model brings scheduler and operation together "rather late"—at `connect()` time, after the work graph is already built:

> "When the scheduler is injected through the receiver the operation and used scheduler are brought together rather late, i.e., when `connect(sender, receiver)`ing and when the work graph is already built."

For coroutines, this timing is fundamentally incompatible with frame allocation. The allocator must be discoverable from the coroutine's arguments at the moment of invocation.

### 2.3 The `stop_token`

C++20 introduced `std::stop_token` as a cooperative cancellation mechanism. A `stop_source` owns the cancellation state; the token provides a read-only view that can be queried or used to register callbacks. When `stop_source::request_stop()` is called, all associated tokens observe the cancellation and registered callbacks fire.

Cancellation flows downward. The application—not the I/O operation—decides when to stop. A timeout, user abort, or graceful shutdown originates at the application layer and propagates to pending operations. The socket doesn't decide when to cancel itself; it receives a stop request from above.

In our model, the stop token is injected at the coroutine launch site and propagated through the entire chain. Consider:

```
http_client → http_request → write → write_some → socket
```

The top-level caller provides a `stop_token`. Each coroutine in the chain receives it and passes it forward. At the end of the chain, the I/O object—the socket—receives the token and can act on it.

This is where the operating system enters. Modern platforms provide cancellation at the kernel level:

- **Windows IOCP**: `CancelIoEx` cancels pending overlapped operations on a specific handle
- **Linux io_uring**: `IORING_OP_ASYNC_CANCEL` cancels a previously submitted operation by its user data
- **POSIX**: `close()` on the file descriptor, though less graceful, interrupts blocking operations

When the stop token signals cancellation, the I/O object registers a callback that invokes the appropriate OS primitive. The pending operation completes with an error—typically `operation_aborted`—and the coroutine chain unwinds normally through its error path.

This design keeps cancellation cooperative and predictable. No operation is forcibly terminated mid-flight. The I/O layer requests cancellation; the OS acknowledges it; the operation completes with an error; the coroutine handles the error. Each layer respects the boundaries of the next.

### 2.4 Backward Flow in `std::execution`

To understand our design choices, we must first understand the `std::execution` model (P2300) and its architectural assumptions.

In the sender/receiver model:

- A **sender** describes an async operation (e.g., `async_read(socket, buffer)`)
- A **receiver** handles the result via `set_value`, `set_error`, or `set_stopped`
- `connect(sender, receiver)` binds them, returning an `operation_state`
- `start(op_state)` initiates execution

The critical architectural issue: the sender queries the receiver's environment *after* connection to discover context—scheduler, allocator, stop token. P2300R10 states explicitly:

> "In the sender/receiver model, as with coroutines, contextual information about the current execution is most naturally propagated from the consumer to the producer. In sender/receiver, that means that contextual information is associated with the receiver and is queried by the sender and/or operation state after the sender and the receiver are `connect`-ed."

This is the **backward flow** we reference throughout this paper. Context flows from receiver back to sender—the consumer provides information that the producer queries. For GPU workloads, where the work graph is constructed before execution begins, this model works well. The entire computation is described, connected, and then started.

For networking, the timing is wrong. Coroutine frame allocation happens *before* the coroutine body executes—the allocator must be known at invocation, not discovered through receiver queries after `connect()`. The backward flow that serves GPU dispatch is structurally incompatible with coroutine allocation semantics.

---

## 3. Our Solution: The IoAwaitable Protocol

The IoAwaitable protocol associates a coroutine with three resources: executor, allocator, and stop token. Context flows **forward** from caller to callee—no backward queries, no late binding.

```cpp
template<typename A>
concept IoAwaitable =
    requires(
        A a,
        coro h,
        executor_ref ex,
        std::stop_token token)
    {
        a.await_suspend(h, ex, token);
    };
```

The key insight: `await_suspend` receives the executor and stop token as parameters, injected by the caller's `await_transform`. The allocator propagates via `thread_local` storage during a narrow execution window with specific guarantees, ensuring availability at frame allocation time.

The related `IoAwaitableTask` concept formalizes the contract between launch functions (`run_async`, `run_on`) and task types:

```cpp
template<typename T>
concept IoAwaitableTask =
    IoAwaitable<T> &&
    requires { typename T::promise_type; } &&
    requires(
        typename T::promise_type& p,
        typename T::promise_type const& cp,
        executor_ref ex,
        std::stop_token st)
    {
        { p.set_executor(ex) } noexcept;
        { p.set_stop_token(st) } noexcept;
        { cp.executor() } noexcept -> std::same_as<executor_ref>;
        { cp.stop_token() } noexcept -> std::same_as<std::stop_token const&>;
    };
```

Launch functions are the root of a coroutine chain—they must set context directly on the promise rather than going through `await_suspend`. The `IoAwaitableTask` concept ensures the promise provides the injection interface (`set_executor`, `set_stop_token`) and the retrieval interface (`executor`, `stop_token`). This separation allows launch functions to bootstrap context into a cold task before it begins execution, while regular `co_await` chains use the three-argument `await_suspend` for propagation.

Implementing `IoAwaitableTask` also gives coroutines the option to access their context from within the coroutine body. The `io_awaitable_support` mixin's `await_transform` intercepts the tag types returned by `get_executor()` and `get_stop_token()`, returning immediate awaiters that yield the stored values without suspending:

```cpp
task<void> cancellable_work()
{
    executor_ref ex = co_await get_executor();         // never suspends
    std::stop_token token = co_await get_stop_token(); // never suspends
    
    for (int i = 0; i < 1000; ++i)
    {
        if (token.stop_requested())
            co_return;  // Exit gracefully on cancellation
        co_await process_chunk(ex, i);
    }
}
```

These accessors are compile-time dispatched via `if constexpr` in `await_transform`. Because `await_ready()` returns `true`, no actual suspension occurs—the coroutine continues immediately with the requested context value.

### 3.1 Implementing the Protocol

To satisfy `IoAwaitable`, a coroutine return type implements an `await_suspend` overload that accepts all three parameters:

```cpp
struct my_task
{
    struct promise_type : io_awaitable_support<promise_type> { ... };

    std::coroutine_handle<promise_type> h_;

    // This signature makes my_task satisfy IoAwaitable
    coro await_suspend(coro cont, executor_ref ex, std::stop_token token)
    {
        h_.promise().set_stop_token(token);
        h_.promise().set_executor(ex);
        h_.promise().set_continuation(cont);
        return h_;  // Transfer to child coroutine
    }

    bool await_ready() const noexcept { return false; }
    T await_resume() { return h_.promise().result(); }
};
```

Why the concrete `executor_ref`? Executors can and often are custom types, and our type-erasing wrapper can erase any executor type for free. The key insight: parameter lifetimes in calling coroutines extend until the callee coroutine final suspends. This drives all of our designs—the frame allocation we cannot avoid pays for the type erasure we need.

This three-argument `await_suspend` is the contract. The parent's `await_transform` calls it, passing the executor and stop token that flow forward into the child coroutine.

### 3.2 How Does a Coroutine Start?

Two entry points bridge non-coroutine code into the coroutine world:

**`run_async` — from callbacks, main(), event handlers:**

Uses a two-call syntax where the first call captures context and returns a wrapper:

```cpp
// Basic: executor only
run_async(ex)(my_task());

// Full: executor, stop_token, allocator, success handler, error handler
run_async(ex, st, alloc, h1, h2)(my_task());

// Example with handlers
run_async(ioc.get_executor(), source.get_token(),
    [](int result) { std::cout << "Got: " << result << "\n"; },
    [](std::exception_ptr ep) { /* handle error */ }
)(compute_value());
```

What makes this possible is a small but consequential change in C++17: guaranteed evaluation order for postfix expressions. The standard now specifies:

> "The postfix-expression is sequenced before each expression in the expression-list and any default argument." — [expr.call]

In `run_async(ex)(my_task())`, the outer postfix-expression `run_async(ex)` is fully evaluated—returning a wrapper that allocates the trampoline coroutine—before `my_task()` is invoked. This guarantees LIFO destruction order: the trampoline is allocated BEFORE the task and serves as the task's continuation.

**`run_on` — switching executors within coroutines:**

Binds a child task to a different executor while returning to the caller's executor on completion:

```cpp
task<void> parent()
{
    // Child runs on worker_ex, but completion returns here
    int result = co_await run_on(worker_ex, compute_on_worker());
}
```

The executor is stored by value in the awaitable, keeping it alive for the operation's duration.

### 3.3 The C++26 Oversight

`std::execution` positions itself as a universal abstraction for asynchronous work, yet its backward query model fails networking's fundamental requirements. Coroutine frame allocation happens *before* the coroutine body executes—the allocator must be known at invocation, not discovered later through receiver queries. The backward flow that works for GPU dispatch (where the work graph is built first, then executed) is incompatible with I/O patterns where context must be present at the moment of creation. This is not a minor incompatibility to be papered over with adapters; it is a structural mismatch between the model's assumptions and networking's reality. Forward propagation—context flowing from caller to callee at each suspension point—is not an alternative design choice but the only design that respects coroutine allocation semantics.

---

## 4. The Executor

**Terminology note.** We use the term *executor* rather than *scheduler* intentionally. In `std::execution`, schedulers are designed for heterogeneous computing—selecting GPU vs CPU algorithms, managing completion domains, and dispatching to hardware accelerators. Networking has different needs: strand serialization, I/O completion contexts, and thread affinity. By using *executor*, we signal a distinct concept tailored to networking's requirements. This terminology also honors Chris Kohlhoff's executor model in Boost.Asio, which established the foundation for modern C++ asynchronous I/O.

```cpp
template<class E>
concept Executor =
    std::copy_constructible<E> &&
    std::equality_comparable<E> &&
    requires(E& e, E const& ce, std::coroutine_handle<> h) {
        { ce.context() } noexcept -> std::same_as<decltype(ce.context())&>;
        { ce.on_work_started() } noexcept;
        { ce.on_work_finished() } noexcept;
        { ce.dispatch(h) } -> std::convertible_to<std::coroutine_handle<>>;
        { ce.post(h) };
    };
```

Executors are lightweight, copyable handles to execution contexts. Users often provide custom executor types tailored to application needs—priority scheduling, per-connection strand serialization, or specialized logging and instrumentation. An execution model must respect these customizations. It must also support executor composition: wrapping one executor with another. The `strand` we provide, for example, wraps an I/O context's executor to add serialization guarantees without changing the underlying dispatch mechanism.

### 4.1 Dispatch

`dispatch` schedules a coroutine handle for resumption. If the caller is already in the executor's context, the implementation may resume inline; otherwise, the handle is queued. We use `std::coroutine_handle<>` rather than a templated callable because the coroutine frame is already allocated and the handle already type-erased—both come for free.

We use `std::coroutine_handle<>` rather than a templated callable because our model is coroutines-only. This constraint enables optimizations unavailable to general-purpose executors. A coroutine handle is a simple pointer—no allocation, no type erasure overhead, no virtual dispatch. Templated callables, by contrast, require either allocation to store the callable or template instantiation that propagates types through the executor interface. By committing to coroutines, we eliminate this cost entirely.

When an I/O context thread dequeues a completion via `epoll_wait`, `GetQueuedCompletionStatus`, or `io_uring_wait_cqe`, it calls `dispatch` to resume the waiting coroutine. The return value enables symmetric transfer: rather than recursively calling `resume()`, the caller returns the handle to the coroutine machinery for a tail call, preventing stack overflow.

Some contexts prohibit inline execution. A strand currently executing work cannot dispatch inline without breaking serialization—`dispatch` then behaves like `post`, queuing unconditionally.

### 4.2 Post

`post` queues work for later execution. Unlike `dispatch`, it never executes inline—the work item is always enqueued, and `post` returns immediately.

Use `post` for:
- **New work** that is not a continuation of the current operation
- **Breaking call chains** to bound stack depth
- **Safety under locks**—posting while holding a mutex avoids deadlock risk from inline execution

### 4.3 Type Erasure

C++20 coroutines provide type erasure *by construction*—but not through the handle type. `std::coroutine_handle<void>` and `std::coroutine_handle<promise_type>` are both just pointers with identical overhead. The erasure that matters is *structural*:

1. **The frame is opaque**: Callers see only a handle, not the promise's layout
2. **The return type is uniform**: All coroutines returning `task` have the same type, regardless of body
3. **Suspension points are hidden**: The caller doesn't know where the coroutine may suspend

This structural erasure is often lamented as overhead, but we recognize it as opportunity: *the allocation we cannot avoid can pay for the type erasure we need*.

Executor types are fully preserved at call sites even though they're type-erased internally. This enables zero-overhead composition at the API boundary while maintaining uniform internal representation.

### 4.4 Forward Propagation

The executor flows through `await_transform`, which intercepts every `co_await` expression. When a coroutine awaits an `IoAwaitable`, the promise's `await_transform` injects the current executor and stop token:

```cpp
template<IoAwaitable A>
auto await_transform(A&& awaitable) {
    return awaitable_wrapper{
        std::forward<A>(awaitable),
        get_executor(),
        get_stop_token()
    };
}
```

Resolution precedence for executor discovery:
1. **Explicit**: If the awaitable carries its own executor (e.g., `run_on`), use that
2. **I/O object**: If awaiting an I/O operation, use the I/O object's executor
3. **Inherited**: Otherwise, inherit from the parent coroutine

### 4.5 The `execution_context`

The `context()` function returns a reference to the `execution_context`—the object containing the platform-specific reactor or event loop. This is where I/O objects coordinate their necessary global state.

The execution context provides:
- **Platform reactor**: epoll, IOCP, io_uring, or kqueue integration
- **Supporting singletons**: Timer queues, resolver services, signal handlers
- **Orderly shutdown**: `stop()` and `join()` for graceful termination
- **Work tracking**: `on_work_started()` / `on_work_finished()` for run-until-idle semantics

I/O objects hold a reference to their execution context, not just their executor. A socket needs the context to register with the reactor; the executor alone cannot provide this.

### 4.6 Comparison

| Aspect            | Executor                        | Scheduler (`std::execution`)    |
| ----------------- | ------------------------------- | ------------------------------- |
| Purpose           | I/O completion, serialization   | Algorithm dispatch, GPU/CPU     |
| Context discovery | Forward (at `co_await`)         | Backward (query receiver)       |
| Allocation        | Early (before frame)            | Late (at `connect()`)           |
| Type erasure      | Structural (coroutine handles)  | Explicit (`any_sender`)         |
| Operations        | `dispatch`, `post`              | `schedule`, `transfer`          |

---

## 5. The Allocator

Allocator propagation presents a unique challenge for coroutines. Unlike executors and stop tokens, which can be injected at suspension points via `await_transform`, the allocator must be available *before* the coroutine frame exists. This section examines why standard approaches fail and presents our solution.

### 5.1 The Timing Constraint

Coroutine frame allocation has a fundamental timing constraint: `operator new` executes before the coroutine body. When a coroutine is called, the compiler allocates the frame first, then begins execution. Any mechanism that injects context later—receiver connection, `await_transform`, explicit method calls—arrives too late.

```cpp
auto t = my_coro(sock);  // operator new called HERE
co_await t;              // await_transform kicks in HERE (too late)
```

### 5.2 The Awkward Approach

C++ provides exactly one hook at the right time: **`promise_type::operator new`**. The compiler passes coroutine arguments directly to this overload, allowing the promise to inspect parameters and select an allocator. The standard pattern uses `std::allocator_arg_t` as a tag to mark the allocator parameter:

```cpp
// Free function: allocator intrudes on the parameter list
task<int> fetch_data(std::allocator_arg_t, MyAllocator alloc,
                     socket& sock, buffer& buf) { ... }

// Member function: same intrusion
task<void> Connection::process(std::allocator_arg_t, MyAllocator alloc,
                               request const& req) { ... }
```

The promise type must provide multiple `operator new` overloads to handle both cases:

```cpp
struct promise_type {
    // For free functions
    template<typename Alloc, typename... Args>
    static void* operator new(std::size_t sz,
        std::allocator_arg_t, Alloc& a, Args&&...) {
        return a.allocate(sz);
    }

    // For member functions (this is first arg)
    template<typename T, typename Alloc, typename... Args>
    static void* operator new(std::size_t sz,
        T&, std::allocator_arg_t, Alloc& a, Args&&...) {
        return a.allocate(sz);
    }
};
```

This approach works, but it violates encapsulation. The coroutine's parameter list—which should describe the algorithm's interface—is polluted with allocation machinery unrelated to its purpose. A function that fetches data from a socket shouldn't need to know or care about memory policy. Worse, every coroutine in a call chain must thread the allocator through its signature, even if it never uses it directly. The allocator becomes viral, infecting interfaces throughout the codebase.

### 5.3 Our Solution: Thread-Local Propagation

Thread-local propagation is the only approach that maintains clean interfaces while respecting the timing constraint. The premise is simple: **allocator customization happens at launch sites**, not within coroutine algorithms. Functions like `run_async` and `run_on` accept allocator parameters because they represent application policy decisions. Coroutine algorithms don't need to "allocator-hop"—they simply inherit whatever allocator the application has established.

Our approach:

1. **Receive the fully typed allocator at launch time.** The launch site (`run_async`, `run_on`) accepts any allocator type as a template parameter.

2. **Type-erase it.** The allocator is stored as `std::pmr::memory_resource*`, providing a uniform interface for all downstream coroutines.

3. **Maintain lifetime via frame extension.** The type-erased allocator lives in the launch coroutine's frame. Because coroutine parameter lifetimes extend until final suspension, the allocator remains valid for the entire operation chain.

4. **Propagate through thread-locals.** Before any child coroutine is invoked, the current allocator is set in TLS. The child's `promise_type::operator new` reads it.

```cpp
inline thread_local std::pmr::memory_resource* current_allocator = 
    std::pmr::get_default_resource();

// In promise_type::operator new
static void* operator new(std::size_t size) {
    return current_allocator->allocate(size, alignof(std::max_align_t));
}
```

This design keeps allocator policy where it belongs—at the application layer—while coroutine algorithms remain blissfully unaware of memory strategy. The propagation happens during what we call "the window": a narrow interval of execution where the correct state is guaranteed in thread-locals.

### 5.4 The Window

Thread-local propagation relies on a narrow, deterministic execution window. Consider:

```cpp
task<void> parent() {        // parent is RUNNING here
    co_await child();        // child() called while parent is running
}
```

When `child()` is called:
1. `parent` coroutine is **actively executing** (not suspended)
2. `child()`'s `operator new` is called
3. `child()`'s frame is created
4. `child()` returns task
5. THEN `parent` suspends

The window is the period while the parent coroutine body executes. If `parent` sets TLS when it resumes and `child()` is called during that execution, `child`'s `operator new` sees the correct TLS value.

The cleanest hook is `await_resume`—called right before the coroutine body continues:

```cpp
auto initial_suspend() noexcept {
    struct awaiter {
        promise_type* p_;
        bool await_ready() { return false; }
        void await_suspend(coro) {}
        void await_resume() { 
            tls::current_allocator = &p_->alloc_;  // Set when body starts
        }
    };
    return awaiter{this};
}
```

Every time the coroutine resumes (after any `co_await`), it sets TLS to its allocator. When `child()` is called, TLS is already pointing to `parent`'s allocator. The flow:

```
parent resumes → TLS = parent.alloc
    ↓
parent calls child()
    ↓
child operator new → reads TLS → uses parent.alloc
    ↓
child created, returns task
    ↓
parent's await_suspend → parent suspends
    ↓
child resumes → TLS = child.alloc (inherited value)
    ↓
child calls grandchild() → grandchild uses TLS
```

This is safe because:
- TLS is only read in `operator new`
- TLS is set by the currently-running coroutine
- Single-threaded: only one coroutine runs at a time per thread
- No dangling: the coroutine that set TLS is still on the stack when `operator new` reads it

### 5.5 Comparison with std::execution

The sender/receiver model cannot hook coroutine frame allocation in a safe, composable way. The timing is structural: `operator new` runs when the coroutine is called, but receiver connection happens later at `connect()` time. By then, the frame is already allocated. No amount of query machinery can retroactively inject an allocator into an allocation that has already occurred. This is not a missing feature to be added in a future revision—it is an architectural limitation inherent to backward-flow context discovery. Forward propagation, where context is present in the coroutine's arguments at invocation, is the only model that respects the compiler's allocation sequence.

---

## 6. The Stop Token

### 6.1 Cooperative Cancellation

C++20 introduced `std::stop_token` as a cooperative cancellation mechanism. A `stop_source` owns the cancellation state; the token provides a read-only view that can be queried or used to register callbacks. When `stop_source::request_stop()` is called, all associated tokens observe the cancellation and registered callbacks fire.

Cancellation flows downward. The application—not the I/O operation—decides when to stop. A timeout, user abort, or graceful shutdown originates at the application layer and propagates to pending operations.

### 6.2 OS Integration

Modern platforms provide cancellation at the kernel level:

- **Windows IOCP**: `CancelIoEx` cancels pending overlapped operations on a specific handle
- **Linux io_uring**: `IORING_OP_ASYNC_CANCEL` cancels a previously submitted operation by its user data
- **POSIX**: `close()` on the file descriptor, though less graceful, interrupts blocking operations

When the stop token signals cancellation, the I/O object registers a callback that invokes the appropriate OS primitive. The pending operation completes with an error—typically `operation_aborted`—and the coroutine chain unwinds normally through its error path.

### 6.3 Propagation

The stop token is injected at the coroutine launch site and propagated through the entire chain:

```
http_client → http_request → write → write_some → socket
```

The top-level caller provides a `stop_token`. Each coroutine in the chain receives it via `await_suspend` and passes it forward. At the end of the chain, the I/O object receives the token and can act on it.

The propagation mechanism mirrors executor flow. When a parent coroutine awaits a child, `await_transform` intercepts the expression and wraps it:

```cpp
// In parent's promise_type
template<IoAwaitable A>
auto await_transform(A&& awaitable) {
    return awaitable_wrapper{
        std::forward<A>(awaitable),
        get_executor(),
        get_stop_token()  // Token from parent's context
    };
}
```

The wrapper's `await_suspend` then passes the token to the child:

```cpp
// In awaitable_wrapper
auto await_suspend(std::coroutine_handle<> cont) {
    return awaitable_.await_suspend(cont, ex_, token_);
}
```

The child's `await_suspend` stores the token in its promise, making it available for further propagation or for registering cancellation callbacks with I/O objects.

This design keeps cancellation cooperative and predictable. No operation is forcibly terminated mid-flight. The I/O layer requests cancellation; the OS acknowledges it; the operation completes with an error; the coroutine handles the error.

### 6.4 Comparison

The contrast with `std::execution` is clear. In the sender/receiver model, the stop token is discovered through a backward query—`get_stop_token(get_env(receiver))`—available only after `connect()` binds sender to receiver. In our model, the caller provides the token at launch, and it propagates forward via `await_suspend`, available immediately when the coroutine begins. OS integration follows naturally: the I/O object registers a cancellation callback directly with the token, invoking platform primitives like `CancelIoEx` or `IORING_OP_ASYNC_CANCEL` when stop is requested.

---

## 7. Why Not `std::execution`?

If networking is required to integrate with `std::execution`, I/O libraries must pay a complexity tax regardless of whether they benefit from the framework's abstractions.

### 7.1 The Implementation Burden

To participate in the sender/receiver ecosystem, networking code must implement:

- **Query protocol compliance**: `get_env`, `get_domain`, `get_completion_scheduler`—even if only to return defaults
- **Concept satisfaction**: Meet sender/receiver requirements designed for GPU algorithm dispatch
- **Transform machinery**: Domain transforms execute even when they select the only available implementation
- **API surface expansion**: Expose attributes and queries irrelevant to I/O operations

A socket returning `default_domain` still participates in the dispatch protocol. The P3826 machinery runs, finds no customization, and falls through to the default—overhead for nothing.

### 7.2 Type Leakage Through connect_result_t

The sender/receiver model solves a real problem: constructing a compile-time call graph for heterogeneous computation chains. When all types are visible at `connect()` time, the compiler can optimize across operation boundaries—inlining GPU kernel launches, eliminating intermediate buffers, and selecting optimal memory transfer strategies. For workloads where dispatch overhead is measured in nanoseconds and operations complete in microseconds, this visibility enables meaningful optimization.

Networking operates in a different regime. I/O latency is measured in tens of microseconds (NVMe storage) to hundreds of milliseconds (network round-trips). A 10-nanosecond dispatch optimization is irrelevant when the operation takes 100,000 nanoseconds. The compile-time call graph provides no benefit—there is no GPU kernel to inline, no heterogeneous dispatch to optimize.

Yet the sender/receiver model requires type visibility regardless:

```cpp
// std::execution pattern
execution::sender auto snd = socket.async_read(buf);
execution::receiver auto rcv = /* ... */;
auto state = execution::connect(snd, rcv);  // Type: connect_result_t<Sender, Receiver>
```

The `connect_result_t` type encodes the full operation state. Algorithms that compose senders must propagate these types:

```cpp
// From P2300: operation state types leak into composed operations
template<class S, class R>
struct _retry_op {
    using _child_op_t = stdexec::connect_result_t<S&, _retry_receiver<S, R>>;
    optional<_child_op_t> o_;  // Nested operation state, fully typed
};
```

For networking, this creates the template tax we sought to avoid (§2.1)—N×M instantiations, compile time growth, implementation details exposed through every API boundary—without the optimization payoff that justifies it for GPU workloads. Our design achieves zero type leakage; composed algorithms expose only `task` return types.

### 7.3 The Core Question

The question is not whether P2300/P3826 break networking code. They don't—defaults work. The question is whether networking should pay for abstractions it doesn't use.

| Abstraction | Networking Need | GPU/Parallel Need |
|-------------|-----------------|-------------------|
| Domain-based dispatch | None | Critical |
| Completion scheduler queries | Unused | Required |
| Sender transforms | Pass-through only | Algorithm selection |
| Typed operation state | ABI liability | Optimization opportunity |

Our analysis suggests the cost is not justified when a simpler, networking-native design achieves the same goals without the tax.

---

## 8. The IoAwaitable Protocol Specification

This section provides formal requirements for each participant in the _IoAwaitable_ protocol. Implementors should follow these specifications to ensure interoperability across coroutine boundaries.

### 8.1 Coroutine Launch Function

A launch function (e.g., `run_async`, `run_on`) bridges non-coroutine code into the coroutine world.

**Requirements:**

1. Accept or provide an executor
2. Accept or default a stop token
3. Set thread-local allocator before invoking the child coroutine
4. Create an initial awaitable wrapper or trampoline coroutine
5. Call the child's `await_suspend(continuation, executor, stop_token)`
6. Manage the trampoline lifetime (LIFO destruction order via C++17 evaluation guarantees)

### 8.2 Awaitable Type

Any type that can be `co_await`ed and participates in context propagation.

**Requirements:**

1. Implement `await_suspend(std::coroutine_handle<> cont, executor_ref ex, std::stop_token token)`
2. Store or forward the executor and stop token as needed
3. Return a `std::coroutine_handle<>` for symmetric transfer (or `void`/`bool` per standard rules)
4. Implement `await_ready()` and `await_resume()` per standard awaitable requirements

**Non-normative note:** Implementors may wish to enforce protocol compliance at API boundaries. When a compliant coroutine's `await_transform` calls the three-argument `await_suspend`, a non-compliant awaitable (lacking this signature) will produce a compile error. Similarly, a compliant awaitable awaited from a non-compliant coroutine will fail to compile. This provides static checking that both sides of each suspension point participate in the protocol:

```cpp
template<typename A>
auto await_transform(A&& a) {
    static_assert(IoAwaitable<A>,
        "Awaitable does not satisfy IoAwaitable; "
        "await_suspend(coro, executor_ref, stop_token) is required");
    return awaitable_wrapper{std::forward<A>(a), executor_, stop_token_};
}
```

### 8.3 Task-like Type

A coroutine return type whose awaitable form satisfies `IoAwaitable`. Task types that support direct context injection by launch functions satisfy the stronger `IoAwaitableTask` concept.

**Requirements for `IoAwaitable`:**

1. Define a `promise_type` that inherits or implements context storage
2. Provide `await_suspend(cont, ex, token)` that:
   - Stores executor and stop token in the promise
   - Sets the continuation handle
   - Returns the task's coroutine handle (symmetric transfer)
3. The promise's `await_transform` must intercept child awaitables and inject context
4. Support `operator new` overloads for allocator propagation (inspect arguments or read TLS)
5. On final suspend, resume the continuation via the stored executor's `dispatch`

**Additional requirements for `IoAwaitableTask`:**

1. The promise must provide `set_executor(executor_ref)` and `set_stop_token(std::stop_token)` for context injection
2. The promise must provide `executor()` and `stop_token()` accessors for context retrieval
3. All four methods must be `noexcept`

The `IoAwaitableTask` requirements enable launch functions to bootstrap context into a task before it begins execution, without going through the awaitable machinery.

### 8.4 I/O Object

An I/O object (socket, timer, resolver) initiates OS-level async operations. It is not itself a coroutine—operating system APIs are not coroutines—but it returns an awaitable that bridges the coroutine world to the kernel.

**Requirements:**

1. Return an awaitable from async methods (e.g., `socket.async_read(buf)`)
2. The awaitable's `await_suspend` must:
   - Receive `(cont, ex, token)` from the calling coroutine
   - Store the continuation handle and executor
   - Optionally register a stop callback with the token for cancellation
   - Initiate the OS operation (submit to IOCP, epoll, io_uring)
3. On operation completion (from OS callback):
   - Call `ex.dispatch(continuation)` to resume the suspended coroutine
   - Store the result for retrieval via `await_resume()`
4. The I/O object is owned by its execution context reference (for reactor registration)

---

## 9. Miscellaneous

This section is non-normative and demonstrates some aspects which may be required by implementors.

### 9.1 The `io_awaitable_support` Mixin

The `io_awaitable_support` CRTP mixin simplifies promise type implementation by providing the machinery for storing and retrieving execution context:

```cpp
template<typename Derived>
class io_awaitable_support {
    executor_ref executor_;
    std::stop_token stop_token_;

public:
    void set_executor(executor_ref ex) { executor_ = ex; }
    void set_stop_token(std::stop_token st) { stop_token_ = st; }
    
    // Awaitables for retrieving context within the coroutine
    auto await_transform(get_executor_t) {
        struct awaitable {
            executor_ref ex_;
            bool await_ready() const noexcept { return true; }
            void await_suspend(std::coroutine_handle<>) const noexcept {}
            executor_ref await_resume() const noexcept { return ex_; }
        };
        return awaitable{executor_};
    }
    
    auto await_transform(get_stop_token_t) {
        struct awaitable {
            std::stop_token st_;
            bool await_ready() const noexcept { return true; }
            void await_suspend(std::coroutine_handle<>) const noexcept {}
            std::stop_token await_resume() const noexcept { return st_; }
        };
        return awaitable{stop_token_};
    }
};
```

Promise types inherit from this mixin to gain:

- Storage for executor and stop token received via `await_suspend`
- `await_transform` overloads that enable `co_await get_executor()` and `co_await get_stop_token()` within the coroutine body
- Consistent context propagation through the awaitable chain

This mixin encapsulates the boilerplate that every IoAwaitable-compatible promise type would otherwise duplicate.

---

## 10. Conclusion

We have presented an execution model designed from the ground up for coroutine-driven asynchronous I/O:

1. **Minimal executor abstraction**: Two operations—`dispatch` and `post`—suffice for I/O workloads.

2. **Clear responsibility model**: The application decides execution, allocation, and stop policy.

3. **Complete type hiding**: Executor types do not leak into public interfaces. Platform I/O types remain hidden in translation units. Composed algorithms expose only `task` return types. This directly enables ABI stability.

4. **Forward context propagation**: Execution context flows with control flow, not against it. No backward queries. _IoAwaitable_ protocol injects context through `await_transform`.

5. **Conscious tradeoff**: One pointer indirection per I/O operation (~1-2 nanoseconds) buys encapsulation, ABI stability, and fast compilation. For I/O-bound workloads where operations take 10,000+ nanoseconds, this cost is negligible.

The comparison with `std::execution` (§2.4, §7) is instructive: that framework's complexity serves GPU workloads, not networking. The relatively large number of related papers suggests a design that is not yet mature and still finding its footing. P3826 adds machinery to fix problems networking doesn't have: domain-based algorithm dispatch, completion scheduler queries, sender transforms. Our design sidesteps these issues entirely.

This divergence suggests that **networking deserves first-class design consideration**, not adaptation to frameworks optimized for heterogeneous computing. The future of asynchronous C++ need not be a single universal abstraction—it may be purpose-built frameworks that excel at their primary use cases while remaining interoperable at the boundaries

---

## References

1. [N4242](https://wg21.link/n4242) — Executors and Asynchronous Operations, Revision 1 (2014)
2. [N4482](https://wg21.link/n4482) — Some notes on executors and the Networking Library Proposal (2015)
3. [P2300R10](https://wg21.link/p2300) — std::execution (Michał Dominiak, Georgy Evtushenko, Lewis Baker, Lucian Radu Teodorescu, Lee Howes, Kirk Shoop, Eric Niebler)
4. [P2762R2](https://wg21.link/p2762) — Sender/Receiver Interface for Networking (Dietmar Kühl)
5. [P3552R3](https://wg21.link/p3552) — Add a Coroutine Task Type (Dietmar Kühl, Maikel Nadolski)
6. [P3826R2](https://wg21.link/p3826) — Fix or Remove Sender Algorithm Customization (Lewis Baker, Eric Niebler)
7. [Boost.Asio](https://www.boost.org/doc/libs/release/doc/html/boost_asio.html) — Asynchronous I/O library (Chris Kohlhoff)

---

## Acknowledgements

This paper builds on the foundational work of many contributors to C++ asynchronous programming:

**Chris Kohlhoff** for Boost.Asio, which has served the C++ community for over two decades and established many of the patterns we build upon—and some we consciously depart from. The executor model in this paper honors his pioneering work.

**Lewis Baker** for his work on C++ coroutines, the Asymmetric Transfer blog series, and his contributions to P2300 and P3826. His explanations of symmetric transfer and coroutine optimization techniques directly informed our design.

**Dietmar Kühl** for P2762 and P3552, which explore sender/receiver networking and coroutine task types. His clear articulation of design tradeoffs—including the late-binding problem and cancellation overhead concerns—helped crystallize our understanding of where the sender model introduces friction for networking.

The analysis in this paper is not a critique of these authors' contributions, but rather an exploration of whether networking's specific requirements are best served by adapting to general-purpose abstractions or by purpose-built designs.

---

## 11. Closing Thoughts

Reference implementations of this protocol exist in Boost.Capy (the executor and task primitives) and Boost.Corosio (the I/O objects and platform integration). These libraries arose from use-case-first driven development with a simple mandate: produce a networking library built for coroutines-only. Every design decision—forward context propagation, type-erased executors, the thread-local allocation window—emerged from solving real problems in production I/O code.

The future of C++ depends less on papers and more on practitioners who ship working code. Open source library authors are the true pioneers—they discover what works by building systems that people actually use. Standards follow implementations, not the reverse. The IoAwaitable protocol is offered in that spirit: not as a theoretical construct, but as a distillation of patterns proven in practice.
