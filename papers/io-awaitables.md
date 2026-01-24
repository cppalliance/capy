| Document | BOOST1000 |
|----------|-------|
| Date:       | 2026-01-21
| Reply-to:   | Vinnie Falco \<vinnie.falco@gmail.com\><br>Steve Gerbino \<steve@gerbino.co\><br>Amlal El Mahrouss \<amlalelmahrouss@icloud.com\>
| Audience:   | Boost, C++

---

# IoAwaitables: A Coroutines-First Execution Model

## Abstract

This paper asks: *what would an execution model look like if designed from the ground up for coroutine-only asynchronous I/O?*

An execution model answers how asynchronous work is scheduled and dispatched, where operation state is allocated and by whom, and what customization points allow users to adapt the framework's behavior. We propose a **coroutines-only** execution model optimized for CPU-bound I/O workloads. The framework introduces the _IoAwaitable_ protocol: a system for associating a coroutine with an executor, stop token, and allocator, and propagating this context forward through a coroutine chain which can end at an operating system API boundary where asynchronous operations are performed.

We compare our use-case-first driven design against `std::execution` (P2300) and observe significant divergence. Analysis of P2300 and its evolution (P3826) reveals a framework driven by GPU and parallel workloads—P3826's focus is overwhelmingly GPU/parallel with no networking discussion. Core networking concerns—strand serialization, I/O completion contexts, platform integration—remain unaddressed. The query-based context model that P3826 attempts to fix is unnecessary when context propagates forward through coroutine machinery.

Our framework demonstrates what emerges when networking requirements drive the design rather than being adapted to a GPU-focused abstraction.

---

## 1. Introduction

The C++ standardization effort has produced `std::execution` (P2300), a sender/receiver framework for structured concurrency. Its design accommodates heterogeneous computing—GPU kernels, parallel algorithms, and hardware accelerators. The machinery is substantial: domains select algorithm implementations, queries determine completion schedulers, and transforms dispatch work to appropriate backends.

But what does asynchronous I/O actually need?

A network server completes read operations on I/O threads, resumes handlers on application threads, and serializes access to connection state. A file system service batches completions through io_uring, dispatches callbacks to worker pools, and manages buffer lifetimes across suspension points. These patterns repeat across every I/O-intensive application. None of them require domain-based algorithm dispatch. TCP servers do not run on GPUs. Socket reads have one implementation per platform, not multiple hardware backends.

**This paper explores what emerges when I/O requirements drive the design.**

We find that two operations for resuming coroutines suffice: `dispatch` for continuations and `post` for independent work. The choice between them is correctness, not performance. Using dispatch while holding a mutex invites deadlock. Using post for every continuation wastes cycles bouncing through queues. But primitives alone don't solve the composition problem. In a chain like `async_http_request → async_read → socket`, who decides which allocator to use? Who determines which executor runs the completion? How are operations canceled? The answers are discovered from studying the use case:

- **The application decides executor policy.** Thread affinity, strand serialization, priority scheduling; these are deployment decisions. A read operation doesn't care which thread pool it runs on. The application architecture determines where completions should resume.

- **The application sends stop signals.** Cancellation flows downward from application logic, not upward from I/O operations. A timeout, user abort, or graceful shutdown originates at the application layer and propagates to pending operations. The socket doesn't decide when to cancel itself.

- **The application decides allocation policy.** Memory strategies: recycling pools, bounded allocation, per-tenant budgets; these are a property of the coroutine chain, not the I/O operation. A socket doesn't care about memory policy; the context in which it runs does.

- **The execution context owns its I/O objects.** A socket registered with epoll on thread A cannot have completions delivered to thread B's epoll. The physical coupling is inherent. The call site cannot know which event loop the socket uses—only the socket knows.

Execution context flows naturally *forward* through async operation chains. The executor, stop token, and allocator propagate from caller to callee at each suspension point. This forward flow eliminates the late-binding problem that `std::execution` struggles to fix: context is known at launch site, not discovered through receiver queries after the work graph is built. Our goal is not to replace `std::execution` for GPU workloads—it is to demonstrate that networking deserves a purpose-built abstraction rather than adaptation to a framework optimized for different requirements.

> **Convention.** Throughout this document we use the following type alias:
> ```cpp
> using coro = std::coroutine_handle<void>;
> ```

---

## 2. What Networking Needs

Networking has specific requirements that differ from parallel computation. A socket read has **one implementation per platform**—there's no algorithm selection, no hardware heterogeneity to manage. There are other important concerns:

- **Platform implementation**: Integration with IOCP, epoll, io_uring
- **Strand serialization**: Ordering guarantees for concurrent access
- **Thread affinity**: Handlers must resume on the correct thread
- **Inline vs queued**: Choosing whether continuations run immediately or are deferred
- **Stack depth control**: Bounding recursion through trampolining or deferred dispatch

Strip these requirements down to their essence. What primitive do they all depend on? **Something that runs work**. An _executor_. Completion contexts, strands, and thread affinity are all built on executors. I/O objects must be bound to an executor's context to work correctly. P2762 acknowledges this reality in §4.1:

> "It was pointed out networking objects like sockets are specific to a context: that is necessary when using I/O Completion Ports or a TLS library and yields advantages when using, e.g., io_uring(2)."

### 2.1 The Executor

An executor is to functions what an allocator is to memory: while an allocator controls *where* objects live, an executor controls *where* functions run. Examining decades of asynchronous I/O patterns reveals that a minimal executor needs only two operations. The first operation handles work that continues the current operation—a completion handler that should run as part of the same logical flow. The second handles work that must begin independently—a task that cannot start until after the initiating call returns. These two cases have distinct correctness requirements, captured by `dispatch` and `post`.

#### `dispatch`

When an I/O operation completes at the operating system level, the suspended coroutine must resume. The kernel has signaled that data is ready or a connection is established; now user code must process the result. In this context, resuming the coroutine immediately, on the current thread and without queuing, is both safe and efficient. No locks are held by the I/O layer. The completion is a natural continuation of the event loop's work.

This pattern is captured by `dispatch`. It resumes the provided coroutine immediately if doing so is safe within the executor's rules, or queues it otherwise. For a strand, "safe" means no other coroutine is currently executing on that strand. For a thread pool, it might mean the caller is already on a pool thread. The executor decides; the caller simply requests execution.

This is the common case for I/O completions. The event loop calls `dispatch` with the coroutine, and in the typical case the coroutine resumes inline without touching any queue. The result is minimal latency and zero allocation for the dispatch itself.

#### `post`

Sometimes inline execution is not just inefficient but incorrect. Consider code that initiates an async operation while holding a mutex, expecting the lock to be released before the completion runs. Or a destructor that launches cleanup work and must return before that work begins. These patterns require a guarantee: the coroutine will not execute before the call returns.

The `post` operation provides this guarantee. It always queues coroutines for later execution, never running inline. The caller can rely on control returning before the coroutine begins. This makes `post` suitable for breaking call chains, avoiding reentrancy, and ensuring ordering when the caller's state must settle before continuation.

The cost is queue insertion and eventual dequeue—cycles that `dispatch` avoids when inline execution is safe. But when correctness requires deferred execution, `post` is the only sound choice.

### 2.2 The `stop_token`

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

### 2.3 The Allocator

Neither callbacks nor coroutines achieve competitive performance without allocation control. Early testing showed that fully type-erased callbacks—using `std::function` at every composition boundary—allocated hundreds of times per invocation and ran an order of magnitude slower than native templates. Non-recycling coroutine configurations perform so poorly, dominated by heap allocation overhead, that they are not worth benchmarking.

**Recycling is mandatory for performance.** Thread-local recycling—caching recently freed memory for immediate reuse—enables zero steady-state allocations. Callbacks achieve this through "delete before dispatch": deallocate operation state before invoking the completion handler. Coroutines achieve it through frame pooling: custom `operator new/delete` recycles frames across operations.

But thread-local recycling optimizes for throughput, not all applications. Different deployments have different needs:

- **Memory conservation**: Embedded systems or resource-constrained environments may prefer bounded pools over unbounded caches
- **Per-tenant limits**: Multi-tenant servers need allocation budgets per client to prevent resource exhaustion
- **Debugging and profiling**: Development builds may want allocation tracking that production builds eliminate

Coroutines are continuously created—every `co_await` may spawn new frames. An execution model must make allocation a first-class customization point that controls *all* coroutine allocations, not just selected operations. Without this, memory policy becomes impossible to enforce.

**The allocator must be present at invocation.** Coroutine frame allocation has a fundamental timing constraint: `operator new` executes before the coroutine body. When a coroutine is called, the compiler allocates the frame first, then begins execution. Any mechanism that injects context later—receiver connection, `await_transform`, explicit method calls—arrives too late.

P2762 acknowledges that the receiver-based model brings scheduler and operation together "rather late"—at `connect()` time, after the work graph is already built:

> "When the scheduler is injected through the receiver the operation and used scheduler are brought together rather late, i.e., when `connect(sender, receiver)`ing and when the work graph is already built."

For coroutines, this timing is fundamentally incompatible with frame allocation. The allocator must be discoverable from the coroutine's launch site before the coroutine frame is created.

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

The same timing problem applies to stop tokens. In `std::execution`, the token is discovered via `get_stop_token(get_env(receiver))`—available only after `connect()`. In our model, the token propagates forward alongside the executor via the same `await_suspend` signature, available from the moment the coroutine begins.

---

## 3. Our Solution: The _IoAwaitable_ Protocol

The _IoAwaitable_ protocol associates a coroutine with three resources: executor, stop token, and allocator. Context propagates **forward** from caller to callee using a well-defined, easy to understand protocol.

```cpp
template< typename A >
concept IoAwaitable =
    requires(
        A a, coro h, executor_ref ex, std::stop_token token )
    {
        a.await_suspend( h, ex, token );
    };
```

The key insight: `await_suspend` receives the executor and stop token as parameters, injected by the caller's `await_transform`. The allocator propagates via `thread_local` storage during a narrow execution window with specific guarantees, ensuring availability at frame allocation time.

Whereas _IoAwaitable_ allows _receiving_ propagated context, the related refinement _IoAwaitableTask_ extends this concept with the promise interface needed to both **receive** propagated context and **propagate** it to child coroutines:

```cpp
template<typename T>
concept IoAwaitableTask =
    IoAwaitable<T> &&
    requires { typename T::promise_type; } &&
    requires(
        typename T::promise_type& p,
        typename T::promise_type const& cp,
        executor_ref ex,
        std::stop_token st,
        coro cont )
    {
        { p.set_executor(ex) } noexcept;
        { p.set_stop_token(st) } noexcept;
        { p.set_continuation(cont, ex) } noexcept;
        { cp.executor() } noexcept -> std::same_as< executor_ref >;
        { cp.stop_token() } noexcept -> std::same_as< std::stop_token const& >;
        { cp.complete() } noexcept -> std::same_as< coro >;
    };
```

The _IoAwaitableTask_ concept ensures the promise provides the injection interface (`set_executor`, `set_stop_token`) for receiving context when awaited, and the retrieval interface (`executor`, `stop_token`) for propagating context to children via `await_transform`. This bidirectional capability is what distinguishes a task from a simple awaitable—the task participates fully in the context propagation chain. The `set_continuation` and `complete` functions work together to implement the **same-executor optimization**:

```cpp
coro promise_type::complete() const noexcept
{
    if( ! cont_ )
        return std::noop_coroutine();
    if( executor_ == caller_ex_ ) // a single pointer comparison internally
        return cont_;
    return caller_ex_.dispatch( cont_ );
}
```

When a parent `co_await`s a child task, it calls `set_continuation( cont, caller_ex )`, storing both its coroutine handle and its executor. At completion, `complete()` compares the child's executor against the stored caller executor: if they match, it returns the continuation directly for zero-overhead symmetric transfer; if they differ (as with `run_on`), it dispatches through the caller's executor to ensure the parent resumes in its expected execution context. This design avoids unnecessary dispatch overhead in the common case while preserving correct executor affinity when executors change.

The _IoLaunchableTask_ concept further refines _IoAwaitableTask_ with the interface needed by launch functions:

```cpp
template<typename T>
concept IoLaunchableTask =
    IoAwaitableTask<T> &&
    requires( T& t, T const& ct, typename T::promise_type const& cp )
    {
        { ct.handle() } noexcept -> std::same_as< std::coroutine_handle< typename T::promise_type> >;
        { cp.exception() } noexcept -> std::same_as< std::exception_ptr >;
        { t.release() } noexcept;
    } &&
    ( std::is_void_v< decltype(std::declval<T&>().await_resume()) > ||
     requires( typename T::promise_type& p ) {
         p.result();
     });
```

Launch functions like `run_async` and `run_on` bootstrap context directly into a task—they call `set_executor` and `set_stop_token` on the promise rather than going through the three-argument `await_suspend`. The _IoLaunchableTask_ concept adds the requirements these functions need: `handle()` and `release()` for lifetime management, plus `exception()` and `result()` for completion handling.

- **`run_async`** is the root of a coroutine chain, launching from non-coroutine code
- **`run_on`** performs executor hopping from within coroutine code, binding a child task to a different executor

Because launch functions are constrained on the concept rather than a concrete type, they work with any conforming task:

```cpp
template< IoLaunchableTask Task >
void run_async( executor_ref ex, Task task );

template< IoLaunchableTask Task >
auto run_on( executor_ref ex, Task task );
```

This decoupling enables library authors to write launch utilities that work with any conforming task type, and users to define custom task types that integrate seamlessly with existing launchers.

Implementing _IoAwaitableTask_ also gives coroutines the option to access their context from within the coroutine body. The `io_awaitable_support` mixin's `await_transform` intercepts the tag types `this_coro::executor` and `this_coro::stop_token`, returning immediate awaiters that yield the stored values without suspending:

```cpp
task<void> cancellable_work()
{
    executor_ref ex = co_await this_coro::executor;         // never suspends
    std::stop_token token = co_await this_coro::stop_token; // never suspends
    
    for (int i = 0; i < 1000; ++i)
    {
        if (token.stop_requested())
            co_return;  // Exit gracefully on cancellation
        co_await process_chunk(ex, i);
    }
}
```

These accessors are compile-time dispatched via `if constexpr` in `await_transform`. Because `await_ready()` returns `true`, no actual suspension occurs—the coroutine continues immediately with the requested context value.

### `executor_ref`

The type-erasing wrapper `executor_ref` is part of the protocol and appears in signatures. A good implementation achieves type-erasure using only two pointers:

```cpp
class executor_ref
{
    void const* ex_ = nullptr;
    detail::executor_vtable const* vt_ = nullptr;
    ...
```

This implementation leverages a key property of coroutines: parameter lifetimes in calling coroutines extend until the callee's final suspension. Launch functions preserve a copy of the user's typed _Executor_. The `executor_ref` type-erases it by holding a pointer to the stored value. As the wrapper propagates through the call chain, the original executor remains valid—it cannot go out of scope until all coroutines in the chain are destroyed.



### 3.1 Satisfying _IoAwaitable_

To meet these requirements, a coroutine return type implements an `await_suspend` overload that accepts all three parameters:

```cpp
struct my_task
{
    struct promise_type : io_awaitable_support< promise_type > { ... };
    std::coroutine_handle< promise_type > h_;
    bool await_ready() const noexcept { return false; }
    T await_resume() { return h_.promise().result(); }

    // This signature makes my_task satisfy IoAwaitable
    coro await_suspend( coro cont, executor_ref ex, std::stop_token token )
    {
        h_.promise().set_executor( ex );
        h_.promise().set_stop_token( token );
        h_.promise().set_continuation( cont, ex );
        return h_;  // Transfer to child coroutine
    }
};
```

This three-argument `await_suspend` is the contract. The parent's `await_transform` calls it, passing the executor and stop token that flow forward into the child coroutine.

Why the concrete `executor_ref`? Callers who write `co_await this_coro::executor` always receive the same predictable type, regardless of what executor was originally bound at the launch site. Executors are often custom types, and our type-erasing wrapper erases any executor type at zero runtime cost. This drives all of our designs: the frame allocation we cannot avoid pays for the type erasure we need.

### 3.2 Satisfying _IoAwaitableTask_

While _IoAwaitable_ handles context *reception*, the _IoAwaitableTask_ refinement adds the promise interface needed to *store and propagate* that context. The concept requires six additional promise methods:

- `set_executor(executor_ref)` / `executor()` — injection and retrieval
- `set_stop_token(std::stop_token)` / `stop_token()` — injection and retrieval
- `set_continuation(coro, executor_ref)` / `complete()` — continuation management

The executor and stop token share identical propagation mechanics—both injected via the same `await_suspend` signature and retrieved via symmetric accessors. These values flow through `await_transform`, which intercepts every `co_await` expression. When a coroutine awaits an _IoAwaitable_, the promise's `await_transform` injects the current executor and stop token:

```cpp
template<IoAwaitable A>
auto await_transform( A&& awaitable ) {
    return awaitable_wrapper{
        std::forward<A>( awaitable ),
        this_coro::executor,
        this_coro::stop_token
    };
}
```

Note that `set_continuation` receives *both* the continuation handle and the caller's executor. This enables the **same-executor optimization** in `complete()`:

```cpp
coro promise_type::complete() const noexcept
{
    if( ! cont_ )
        return std::noop_coroutine();
    if( executor_ == caller_ex_ )        // Single pointer comparison
        return cont_;                    // Zero-overhead symmetric transfer
    return caller_ex_.dispatch( cont_ );
}
```

When the child's executor matches the caller's, the continuation returns directly for symmetric transfer; otherwise, it dispatches through the caller's executor to ensure the parent resumes in its expected context.

> **Non-normative note.** The `io_awaitable_support` CRTP mixin (§8.1) provides all six required methods and is offered as a convenience. It is not proposed for standardization and is not strictly necessary—implementors may write the boilerplate themselves.


### 3.3 Satisfying _IoLaunchableTask_

The _IoLaunchableTask_ concept extends _IoAwaitableTask_ with the interface needed by launch functions like `run_async` and `run_on`. These functions bootstrap context directly into a task—they call `set_executor` and `set_stop_token` on the promise rather than going through the three-argument `await_suspend`. To do this, they need additional capabilities:

- `handle()` on the task — returns the typed `std::coroutine_handle<promise_type >` for direct frame access
- `release()` on the task — transfers ownership so the launch function can manage lifetime
- `exception()` on the promise — returns the stored `std::exception_ptr` for error handling
- `result()` on the promise — returns the stored value (required only for non-void tasks)

A task type satisfies _IoLaunchableTask_ by implementing these methods:

```cpp
template<typename T>
struct task
{
    std::coroutine_handle<promise_type> h_;

    std::coroutine_handle<promise_type> handle() const noexcept
    {
        return h_;
    }

    void release() noexcept
    {
        h_ = nullptr;
    }

    struct promise_type : io_awaitable_support<promise_type>
    {
        std::exception_ptr ep_;
        std::optional<T> result_;

        std::exception_ptr exception() const noexcept { return ep_; }
        T&& result() noexcept { return std::move(*result_); }
    };
};
```

The `handle()` method provides access to the typed coroutine handle, allowing launch functions to resume the coroutine and access the promise. The `release()` method transfers ownership—after calling it, the task wrapper no longer destroys the frame, leaving lifetime management to the launch function.

For `task<void>`, the `result()` method is not required since there is no value to retrieve. The concept uses a disjunction to handle this:

```cpp
( std::is_void_v< decltype(std::declval<T&>().await_resume()) > ||
  requires( typename T::promise_type& p ) { p.result(); } );
```

> **Non-normative note.** Unlike the `io_awaitable_support` mixin which provides promise methods, the `handle()` and `release()` methods are task-specific. The exception and result storage shown above is illustrative—implementations may use different strategies such as `std::variant` for result/exception storage.

### 3.4 How Does a Coroutine Start?

Two basic functions are needed to get things going in coroutine world, and authors can define their own custom launch functions to suit their needs.

**`run_async`** — launch from callbacks, main(), event handlers, top level of a coroutine chain.

This uses a two-call syntax where the first call captures context and returns a wrapper. The executor parameter is required. The remaining parameters are optional:

* `std::stop_token` to propagate cancelation signals
* `alloc` used to allocate **all** frames in the coroutine chain
* `h1`, invoked with the task's value at final suspend
* `h2`, invoked with `std::exception_ptr` on exception

```cpp
// Basic: executor only
run_async( ex )( my_task() );

// Full: executor, stop_token, allocator, success handler, error handler
run_async( ex, st, alloc, h1, h2 )( my_task() );

// Example with handlers
run_async( ioc.get_executor(), source.get_token(),
    [](int result) { std::cout << "Got: " << result << "\n"; },
    [](std::exception_ptr ep) { /* handle error */ }
)( compute_value() );
```

While the syntax is unfortunate, it is _the only way_ given the timing constraints of frame allocation. And hey, its better than callback hell. What makes this possible is a small but consequential change in C++17: guaranteed evaluation order for postfix expressions. The standard now specifies:

> "The postfix-expression is sequenced before each expression in the expression-list and any default argument." — [expr.call]

In `run_async(ex)(my_task())`, the outer postfix-expression `run_async(ex)` is fully evaluated—returning a wrapper that allocates the trampoline coroutine—before `my_task()` is invoked. This guarantees LIFO destruction order: the trampoline is allocated BEFORE the task and serves as the task's continuation.

**`run_on`** — switching executors within coroutines.

This binds a child task to a different executor while returning to the caller's executor on completion:

```cpp
task<void> parent()
{
    // Child runs on worker_ex, but completion returns here
    int result = co_await run_on( worker_ex, compute_on_worker() );
}
```

The executor is stored by value in the calling awaitable's frame, keeping it alive for the operation's duration.

### 3.5 The C++26 Oversight

`std::execution` positions itself as a universal abstraction for asynchronous work, yet its backward query model fails networking's fundamental requirements. Coroutine frame allocation happens *before* the coroutine body executes—the allocator must be known at invocation, not discovered later through receiver queries. The backward flow that works for GPU dispatch (where the work graph is built first, then executed) is incompatible with I/O patterns where context must be present at the moment of creation. This is not a minor incompatibility to be papered over with adapters; it is a structural mismatch between the model's assumptions and networking's reality. Forward propagation—context flowing from caller to callee at each suspension point—is not an alternative design choice but the only design that respects coroutine allocation semantics.

P2300 itself acknowledges this timing in its "Dependently-typed senders" section:

> "In the sender/receiver model, as with coroutines, contextual information about the current execution is most naturally propagated from the consumer to the producer. In coroutines, that means information like stop tokens, allocators and schedulers are propagated from the calling coroutine to the callee. In sender/receiver, that means that that contextual information is associated with the receiver and is queried by the sender and/or operation state **after** the sender and the receiver are `connect`-ed."

The consequence is stated plainly:

> "The implication of the above is that the sender alone does not have all the information about the async computation it will ultimately initiate; some of that information is provided late via the receiver."

This "late" information includes the allocator. Consider what happens with a coroutine-based sender:

```cpp
// P2300 query model: allocator discovered AFTER connect
template<receiver Rcvr>
struct my_operation_state {
    Rcvr rcvr_;
    
    void start() & noexcept {
        // Allocator available HERE, via receiver query...
        auto alloc = get_allocator(get_env(rcvr_));
        // ...but coroutine frame was allocated BEFORE connect() returned
    }
};

// The sender is created first, THEN connected:
task<int> async_work();              // Frame allocated NOW
auto sndr = async_work();            
auto op = connect(sndr, receiver);   // Allocator available NOW—too late
start(op);
```

The coroutine frame is allocated at invocation, but the allocator isn't queryable until `connect`—after the allocation has already happened. No amount of query machinery can retroactively inject an allocator into an allocation that has already occurred. This isn't a missing feature; it's a fundamental ordering constraint that the backward query model cannot satisfy.

---

## 4. Executor concept

**Terminology note.** We use the term _Executor_ rather than *scheduler* intentionally. In `std::execution`, schedulers are designed for heterogeneous computing—selecting GPU vs CPU algorithms, managing completion domains, and dispatching to hardware accelerators. Networking has different needs: strand serialization, I/O completion contexts, and thread affinity. By using *executor*, we signal a distinct concept tailored to networking's requirements. This terminology also honors Christopher Kohlhoff's executor model in Boost.Asio, which established the foundation for modern C++ asynchronous I/O.

```cpp
template<class E>
concept Executor =
    std::copy_constructible<E> &&
    std::equality_comparable<E> &&
    requires( E& e, E const& ce, std::coroutine_handle<> h ) {
        { ce.context() } noexcept;
        requires std::is_lvalue_reference_v<decltype(ce.context())> &&
            std::derived_from<
                std::remove_reference_t<decltype(ce.context())>,
                execution_context>;
        { ce.on_work_started() } noexcept;
        { ce.on_work_finished() } noexcept;

        // Work submission
        { ce.dispatch( h ) } -> std::convertible_to< std::coroutine_handle<> >;
        { ce.post(h) };
    };
```

Executors are lightweight, copyable handles to execution contexts. Users often provide custom executor types tailored to application needs—priority scheduling, per-connection strand serialization, or specialized logging and instrumentation. An execution model must respect these customizations. It must also support executor composition: wrapping one executor with another. The `strand` we provide, for example, wraps an I/O context's executor to add serialization guarantees without changing the underlying dispatch mechanism.

C++20 coroutines provide type erasure *by construction*—but not through the handle type. `std::coroutine_handle<void>` and `std::coroutine_handle<promise_type>` are both just pointers with identical overhead. The erasure that matters is *structural*:

1. **The frame is opaque**: Callers see only a handle, not the promise's layout
2. **The return type is uniform**: All coroutines returning `task` have the same type, regardless of body
3. **Suspension points are hidden**: The caller doesn't know where the coroutine may suspend

This structural erasure is often lamented as overhead, but we recognize it as opportunity: *the allocation we cannot avoid can pay for the type erasure we need*. In our model, executor type-erasure happens late; only after the API has locked in the executor choice. Executor types are fully preserved at call sites even though they're type-erased internally. This enables zero-overhead composition at the API boundary while maintaining uniform internal representation.

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

### 4.3 The `execution_context`

An executor's `context()` function returns a reference to the `execution_context`, the proposed base class for any object that runs work (often containing the platform reactor or event loop). I/O objects coordinate global state here. Implementations install services—singletons with well-defined shutdown and destruction ordering for safe resource release. This design borrows heavily from Boost.Asio.

```cpp
class execution_context
{
public:
    class service
    {
    public:
        virtual ~service() = default;
    protected:
        service() = default;
        virtual void shutdown() = 0;
    };

    execution_context( execution_context const& ) = delete;
    execution_context& operator=( execution_context const& ) = delete;
    ~execution_context();
    execution_context();

    template<class T> bool has_service() const noexcept;
    template<class T> T* find_service() const noexcept;
    template<class T> T& use_service();
    template<class T, class... Args> T& make_service( Args&&... args ;

    std::pmr::memory_resource* get_frame_allocator() const noexcept;

    void set_frame_allocator( std::pmr::memory_resource* mr ) noexcept;

    template<class Allocator>
        requires (!std::is_pointer_v<Allocator>)
    void set_frame_allocator( Allocator const& a );

protected:
    void shutdown() noexcept;
    void destroy() noexcept;
};
```

Derived classes can provide:
- **Platform reactor**: epoll, IOCP, io_uring, or kqueue integration
- **Supporting singletons**: Timer queues, resolver services, signal handlers
- **Orderly shutdown**: `stop()` and `join()` for graceful termination
- **Work tracking**: `on_work_started()` / `on_work_finished()` for run-until-idle semantics
- **Threads**: for example `thread_pool`.

I/O objects hold a reference to their execution context, and do not have an associated executor. A socket needs the context to register with the reactor; the executor alone cannot provide this.

#### Frame Allocator

The `execution_context` provides `set_frame_allocator` and `get_frame_allocator` as customization points for launchers when no allocator is specified at the launch site. Since every launcher requires an _Executor_, the execution context naturally coordinates frame allocation policy. The default allocator can optimize for speed using recycling with thread-local pools, or for economy on constrained platforms. Using `std::pmr::memory_resource*` allows implementations to change the default without breaking ABI. Applications can set a policy once via `set_frame_allocator`, and all coroutines launched with the default will use it—including those in foreign libraries, without propagating allocator template parameters or recompiling.

### 4.4 Comparison

| Aspect            | Executor                        | Scheduler (`std::execution`)    |
| ----------------- | ------------------------------- | ------------------------------- |
| Purpose           | I/O completion, serialization   | Algorithm dispatch, GPU/CPU     |
| Context discovery | Forward (at `co_await`)         | Backward (query receiver)       |
| Allocation        | Early (before frame)            | Late (at `connect()`)           |
| Type erasure      | Structural (coroutine handles)  | Explicit (`any_sender`)         |
| Operations        | `dispatch`, `post`              | `schedule`, `transfer`          |

---

## 5. The Allocator

Achieving high performance levels with coroutines demands allocator customization, yet allocator propagation presents a unique challenge. Unlike executors and stop tokens, which can be injected at suspension points via `await_transform`, the allocator must be available *before* the coroutine frame exists. This section examines why standard approaches fail and presents our solution.

### 5.1 The Timing Constraint

Coroutine frame allocation has a fundamental timing constraint: `operator new` executes before the coroutine body. When a coroutine is called, the compiler allocates the frame first, then begins execution. Any mechanism that injects context later—receiver connection, `await_transform`, explicit method calls—arrives too late.

```cpp
auto t = my_coro(sock);  // operator new called HERE
co_await t;              // await_transform kicks in HERE (too late)

spawn( my_coro(sock) );  // my_coro(sock) evaluated BEFORE calling spawn (too late)
```

### 5.2 The Awkward Approach

C++ provides exactly one hook at the right time: **`promise_type::operator new`**. The compiler passes coroutine arguments directly to this overload, allowing the promise to inspect parameters and select an allocator. The standard pattern uses `std::allocator_arg_t` as a tag to mark the allocator parameter:

```cpp
// Free function: allocator intrudes on the parameter list
task<int> fetch_data( std::allocator_arg_t, MyAllocator alloc,
                      socket& sock, buffer& buf ) { ... }

// Member function: same intrusion
task<void> Connection::process( std::allocator_arg_t, MyAllocator alloc,
                                request const& req) { ... }
```

The promise type must provide multiple `operator new` overloads to handle both cases:

```cpp
struct promise_type {
    // For free functions
    template< typename Alloc, typename... Args >
    static void* operator new( std::size_t sz,
        std::allocator_arg_t, Alloc& a, Args&&...) {
        return a.allocate(sz);
    }

    // For member functions (this is first arg)
    template< typename T, typename Alloc, typename... Args >
    static void* operator new( std::size_t sz,
        T&, std::allocator_arg_t, Alloc& a, Args&&...) {
        return a.allocate(sz);
    }
};
```

This approach works, but it violates encapsulation. The coroutine's parameter list—which should describe the algorithm's interface—is polluted with allocation machinery unrelated to its purpose. A function that fetches data from a socket shouldn't need to know or care about memory policy. Worse, every coroutine in a call chain must thread the allocator through its signature, even if it never uses it directly. The allocator becomes viral, infecting interfaces throughout the codebase.

### 5.3 Our Solution: Thread-Local Propagation

Thread-local propagation is the only approach that maintains clean interfaces while respecting the timing constraint. The premise is simple: **allocator customization happens at launch sites**, not within coroutine algorithms. Functions like `run_async` and `run_on` accept allocator parameters because they represent application policy decisions. Coroutine algorithms don't need to "allocator-hop"—they simply inherit whatever allocator the application has established.

Our approach:

1. **Receive the allocator at launch time.** The launch site (`run_async`, `run_on`) accepts a fully-typed _Allocator_ parameter, or a `std::pmr::memory_resource*` at the caller's discretion.

2. **Type-erase it.** Typed allocators are stored as `std::pmr::memory_resource*`, providing a uniform interface for all downstream coroutines.

3. **Maintain lifetime via frame extension.** The allocator lives in the launch coroutine's frame. Because coroutine parameter lifetimes extend until final suspension, the allocator remains valid for the entire operation chain.

4. **Propagate through thread-locals.** Before any child coroutine is invoked, the current allocator is set in TLS. The child's `promise_type::operator new` reads it. This is an example implementation (non-normative):

```cpp
// Thread-local accessor (returns reference to enable setting)
inline std::pmr::memory_resource*&
current_frame_allocator() noexcept {
    static thread_local std::pmr::memory_resource* mr = nullptr;
    return mr;
}

// In promise_type::operator new
static void* operator new( std::size_t size ) {
    auto* mr = current_frame_allocator();
    if(!mr)
        mr = std::pmr::get_default_resource();

    // Store allocator pointer at end of frame for correct deallocation
    std::size_t total = size + sizeof(std::pmr::memory_resource*);
    void* raw = mr->allocate(total, alignof(std::max_align_t));
    *reinterpret_cast<std::pmr::memory_resource**>(
        static_cast<char*>(raw) + size) = mr;
    return raw;
}

static void operator delete( void* ptr, std::size_t size ) {
    // Read the allocator pointer from the end of the frame
    auto* mr = *reinterpret_cast<std::pmr::memory_resource**>(
        static_cast<char*>(ptr) + size);
    std::size_t total = size + sizeof(std::pmr::memory_resource*);
    mr->deallocate(ptr, total, alignof(std::max_align_t));
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

TLS remains valid between `await_suspend` and `await_resume`:

```cpp
auto initial_suspend() noexcept {
    struct awaiter {
        promise_type* p_;
        bool await_ready() const noexcept { return false; }
        void await_suspend(coro) const noexcept {
            // Capture TLS allocator while it's still valid
            p_->set_frame_allocator( current_frame_allocator() );
        }
        void await_resume() const noexcept {
            // Restore TLS when body starts executing
            if( p_->frame_allocator() )
                current_frame_allocator() = p_->frame_allocator();
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

---

## 6. Why Not `std::execution`?

If networking is required to integrate with `std::execution`, I/O libraries must pay a complexity tax regardless of whether they benefit from the framework's abstractions.

### 6.1 The Implementation Burden

To participate in the sender/receiver ecosystem, networking code must implement:

- **Query protocol compliance**: `get_env`, `get_domain`, `get_completion_scheduler`—even if only to return defaults
- **Concept satisfaction**: Meet sender/receiver requirements designed for GPU algorithm dispatch
- **Transform machinery**: Domain transforms execute even when they select the only available implementation
- **API surface expansion**: Expose attributes and queries irrelevant to I/O operations

A socket returning `default_domain` still participates in the dispatch protocol. The P3826 machinery runs, finds no customization, and falls through to the default—overhead for nothing.

### 6.2 Type Leakage Through connect_result_t

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

For networking, this creates the template tax we sought to avoid (§2.1)—N×M instantiations, compile time growth, implementation details exposed through every API boundary—without the optimization payoff that justifies it for GPU workloads. Our design achieves zero type leakage; composed algorithms expose only concrete _Task_ return types.

### 6.3 The Core Question

The question is not whether P2300/P3826 break networking code. They don't—defaults work. The question is whether networking should pay for abstractions it doesn't use.

| Abstraction | Networking Need | GPU/Parallel Need |
|-------------|-----------------|-------------------|
| Domain-based dispatch | None | Critical |
| Completion scheduler queries | Unused | Required |
| Sender transforms | Pass-through only | Algorithm selection |
| Typed operation state | ABI liability | Optimization opportunity |

Our analysis suggests the cost is not justified when a simpler, networking-native design achieves the same goals without the tax.

---

## 7. The IoAwaitable Protocol Specification

This section provides formal requirements for each participant in the _IoAwaitable_ protocol. The concepts form a refinement hierarchy: _IoLaunchableTask_ refines _IoAwaitableTask_, which refines _IoAwaitable_. Implementors should follow these specifications to ensure interoperability across coroutine boundaries.

### 7.1 Satisfying _IoAwaitable_

The _IoAwaitable_ concept is the foundation of the protocol. Any type that can be `co_await`ed and participates in context propagation must satisfy this concept.

**Requirements:**

1. Implement `await_suspend(std::coroutine_handle<> cont, executor_ref ex, std::stop_token token)`
2. Store or forward the executor and stop token as needed
3. Return a `std::coroutine_handle<>` for symmetric transfer (or `void`/`bool` per standard rules)
4. Implement `await_ready()` and `await_resume()` per standard awaitable requirements

**Example implementation:**

```cpp
struct my_awaitable
{
    bool await_ready() const noexcept { return false; }

    // This signature satisfies IoAwaitable
    coro await_suspend( coro cont, executor_ref ex, std::stop_token token )
    {
        // Store context for the operation
        cont_ = cont;
        ex_ = ex;
        token_ = token;
        start_async_operation();
        return std::noop_coroutine();
    }

    T await_resume() { return result_; }
};
```

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

### 7.2 Satisfying _IoAwaitableTask_

The _IoAwaitableTask_ concept refines _IoAwaitable_ with the promise interface needed to both **receive** propagated context and **propagate** it to child coroutines. This is what distinguishes a task from a simple awaitable—the task participates fully in the context propagation chain.

**Additional requirements (beyond _IoAwaitable_):**

1. Define a nested `promise_type`
2. The promise must provide injection methods:
   - `set_executor(executor_ref)` — stores the executor (must be `noexcept`)
   - `set_stop_token(std::stop_token)` — stores the stop token (must be `noexcept`)
   - `set_continuation(coro, executor_ref)` — stores the continuation handle and caller's executor (must be `noexcept`)
3. The promise must provide retrieval methods:
   - `executor()` — returns `executor_ref` (must be `noexcept`)
   - `stop_token()` — returns `std::stop_token const&` (must be `noexcept`)
   - `complete()` — returns a `coro` for `final_suspend` to use for symmetric transfer (must be `noexcept`)
4. The promise's `await_transform` must intercept child awaitables and inject context
5. Support `operator new` overloads for allocator propagation (inspect arguments or read TLS)

**Example implementation:**

```cpp
template<typename T>
struct task
{
    struct promise_type : io_awaitable_support<promise_type>
    {
        executor_ref executor_;
        executor_ref caller_ex_;
        std::stop_token stop_token_;
        coro cont_;

        void set_executor( executor_ref ex ) noexcept { executor_ = ex; }
        void set_stop_token( std::stop_token st ) noexcept { stop_token_ = st; }
        void set_continuation( coro c, executor_ref ex ) noexcept
        {
            cont_ = c;
            caller_ex_ = ex;
        }

        executor_ref executor() const noexcept { return executor_; }
        std::stop_token const& stop_token() const noexcept { return stop_token_; }

        coro complete() const noexcept
        {
            if( ! cont_ )
                return std::noop_coroutine();
            if( executor_ == caller_ex_ )
                return cont_;  // Same-executor optimization
            return caller_ex_.dispatch( cont_ );
        }
        // ... result storage, initial/final suspend, etc.
    };

    std::coroutine_handle<promise_type> h_;

    bool await_ready() const noexcept { return false; }
    T await_resume() { return h_.promise().result(); }

    // Satisfies IoAwaitable and enables IoAwaitableTask
    coro await_suspend( coro cont, executor_ref ex, std::stop_token token )
    {
        h_.promise().set_executor( ex );
        h_.promise().set_stop_token( token );
        h_.promise().set_continuation( cont, ex );
        return h_;  // Transfer to child coroutine
    }
};
```

Note that `set_continuation` receives *both* the continuation handle and the caller's executor. This enables the **same-executor optimization** in `complete()`: when the child's executor matches the caller's, the continuation returns directly for symmetric transfer; otherwise, it dispatches through the caller's executor to ensure the parent resumes in its expected context.

> **Non-normative note.** The `io_awaitable_support` CRTP mixin (§8.1) provides all six required promise methods and is offered as a convenience. It is not proposed for standardization—implementors may write the boilerplate themselves.

### 7.3 Satisfying _IoLaunchableTask_

The _IoLaunchableTask_ concept refines _IoAwaitableTask_ with the interface needed by launch functions like `run_async` and `run_on`. These functions bootstrap context directly into a task—they call `set_executor` and `set_stop_token` on the promise rather than going through the three-argument `await_suspend`.

**Additional requirements (beyond _IoAwaitableTask_):**

1. The task must provide `handle()` returning `std::coroutine_handle<promise_type>` (must be `noexcept`)
2. The task must provide `release()` to transfer ownership without destroying the frame (must be `noexcept`)
3. The promise must provide `exception()` returning any stored `std::exception_ptr` (must be `noexcept`)
4. For non-void tasks, the promise must provide `result()` returning the stored value

**Example implementation:**

```cpp
template<typename T>
struct task
{
    struct promise_type : io_awaitable_support<promise_type>
    {
        std::exception_ptr ep_;
        std::optional<T> result_;

        std::exception_ptr exception() const noexcept { return ep_; }
        T&& result() noexcept { return std::move(*result_); }

        // ... plus all IoAwaitableTask requirements
    };

    std::coroutine_handle<promise_type> h_;

    std::coroutine_handle<promise_type> handle() const noexcept
    {
        return h_;
    }

    void release() noexcept
    {
        h_ = nullptr;
    }

    // ... plus all IoAwaitableTask requirements
};
```

The `handle()` method provides access to the typed coroutine handle, allowing launch functions to resume the coroutine and access the promise. The `release()` method transfers ownership—after calling it, the task wrapper no longer destroys the frame, leaving lifetime management to the launch function.

For `task<void>`, the `result()` method is not required since there is no value to retrieve. The concept uses a disjunction to handle this:

```cpp
( std::is_void_v< decltype(std::declval<T&>().await_resume()) > ||
  requires( typename T::promise_type& p ) { p.result(); } );
```

> **Non-normative note.** Unlike the `io_awaitable_support` mixin which provides promise methods, the `handle()` and `release()` methods are task-specific. The exception and result storage shown above is illustrative—implementations may use different strategies such as `std::variant` for result/exception storage.

### 7.4 Implementing a Launcher

A launch function (e.g., `run_async`, `run_on`) bridges non-coroutine code into the coroutine world or performs executor hopping within a coroutine chain. Launch functions are constrained on _IoLaunchableTask_ to work with any conforming task type:

```cpp
template<Executor Ex, IoLaunchableTask Task>
void run_async( Ex const& ex, Task task );  // caller responsible for extending lifetime

template<Executor Ex, IoLaunchableTask Task>
auto run_on( Ex const& ex, Task task );     // caller responsible for extending lifetime
```

**Requirements:**

1. Accept or provide an executor
2. Accept or default a stop token
3. Set thread-local allocator before invoking the child coroutine
4. Bootstrap context via `set_executor` and `set_stop_token` on the promise
5. Manage the task lifetime via `handle()` and `release()`
6. Handle completion via `exception()` and `result()` on the promise

**Example implementation sketch:**

```cpp
template<Executor Ex, IoLaunchableTask Task>
void run_async( Ex const& ex, std::stop_token token, Task task )
{   // caller responsible for extending lifetime
    auto& promise = task.handle().promise();

    // Bootstrap context directly into the promise
    promise.set_executor( ex );
    promise.set_stop_token( token );
    promise.set_continuation( /* completion handler */, ex );

    // Transfer ownership and start execution
    task.release();
    ex.post( task.handle() );
}
```

> **Non-normative note.** This simplified example has the allocator ordering problem described in §5.1: the task's frame is allocated before `run_async` is called, so any thread-local allocator setup would arrive too late. A correct implementation uses the two-call syntax shown in §3.4—`run_async(ex)(my_task())`—where the first call returns a wrapper that sets up the allocator before the task expression is evaluated. A complete implementation is beyond the scope of this example.

Because launch functions are constrained on the concept rather than a concrete type, they work with any conforming task implementation. This decoupling enables library authors to write launch utilities that interoperate with user-defined task types.

---

## 8. Miscellaneous

This section is non-normative and demonstrates some aspects which may be required by implementors.

### 8.1 The `io_awaitable_support` Mixin

This utility simplifies promise type implementation by providing all machinery required for _IoAwaitableTask_ compliance:

```cpp
template<typename Derived>
class io_awaitable_support
{
    executor_ref executor_;
    std::stop_token stop_token_;
    std::pmr::memory_resource* alloc_ = nullptr;
    executor_ref caller_ex_;
    coro cont_;

public:
    static void* operator new( std::size_t size );
    static void operator delete( void* ptr, std::size_t size );

    void set_frame_allocator( std::pmr::memory_resource* alloc ) noexcept;
    std::pmr::memory_resource* frame_allocator() const noexcept;

    void set_continuation( coro cont, executor_ref caller_ex ) noexcept;
    coro complete() const noexcept;

    void set_stop_token( std::stop_token token ) noexcept;
    std::stop_token const& stop_token() const noexcept;

    void set_executor( executor_ref ex ) noexcept;
    executor_ref executor() const noexcept;

    template<typename A>
    decltype(auto) transform_awaitable( A&& a );

    template<typename T>
    auto await_transform( T&& t );
};
```

Promise types inherit from this mixin to gain:

- **Frame allocation**: `operator new`/`delete` using the thread-local frame allocator, with the allocator pointer stored at the end of each frame for correct deallocation
- **Frame allocator storage**: `set_frame_allocator`/`frame_allocator` for propagation to child tasks
- **Continuation support**: `set_continuation`/`complete` implementing the same-executor optimization
- **Stop token storage**: `set_stop_token`/`stop_token` for cancellation propagation
- **Executor storage**: `set_executor`/`executor` for executor affinity
- **Awaitable transformation**: `await_transform` intercepts `get_stop_token_tag` and `get_executor_tag`, delegating all other awaitables to `transform_awaitable`

The `await_transform` method uses `if constexpr` to dispatch tag types to immediate awaiters (where `await_ready()` returns `true`), enabling `co_await get_executor()` and `co_await get_stop_token()` without suspension. Other awaitables pass through to `transform_awaitable`, which derived classes can override to add custom transformation logic.

This mixin encapsulates the boilerplate that every _IoLaunchableTask_-compatible promise type would otherwise duplicate.

---

## 9. Conclusion

We have presented an execution model designed from the ground up for coroutine-driven asynchronous I/O:

1. **Minimal executor abstraction**: Two operations—`dispatch` and `post`—suffice for I/O workloads.

2. **Clear responsibility model**: The application decides execution, allocation, and stop policy.

3. **Complete type hiding**: Executor types do not leak into public interfaces. Platform I/O types remain hidden in translation units. Composed algorithms expose only concrete Task return types. This directly enables ABI stability.

4. **Forward context propagation**: Execution context flows with control flow, not against it. No backward queries. _IoAwaitableTask_ injects context through `await_transform`.

5. **Conscious tradeoff**: One pointer indirection per I/O operation (~1-2 nanoseconds) buys encapsulation, ABI stability, and fast compilation. For I/O-bound workloads where operations take 10,000+ nanoseconds, this cost is negligible.

6. **Borrows from existing practice**: Our design is heavily inspired by Boost.Asio. It gets most things right.

The comparison with `std::execution` (§2.4, §6) is instructive: that framework's complexity serves GPU workloads, not networking. The relatively large number of related papers suggests a design that is not yet mature and still finding its footing. P3826 adds machinery to fix problems networking doesn't have: domain-based algorithm dispatch, completion scheduler queries, sender transforms. Our design sidesteps these issues entirely.

This divergence suggests that **networking deserves first-class design consideration**, not adaptation to frameworks optimized for heterogeneous computing. The future of asynchronous C++ need not be a single universal abstraction—it may be purpose-built frameworks that excel at their primary use cases while remaining interoperable at the boundaries

---

## 10. Closing Thoughts

A reference implementation of this protocol exists as a complete library: Boost.Capy. It is also the foundation for the Boost.Corosio library which offers sockets, timers, signals, DNS resolution, and integration on multiple platforms. These libraries arose from use-case-first driven development with a simple mandate: produce a networking library built only for coroutines. Every design decision: forward context propagation, type-erased executors, the thread-local allocation window, emerged from solving real problems in production I/O code.

The future of C++ depends less on papers and more on practitioners who ship working code. Open source library authors are the true pioneers—they discover what works by building systems that people actually use. Standards should follow implementations, not the reverse. The _IoAwaitable_ protocol is offered in that spirit: not as a theoretical construct, but as a distillation of patterns proven in practice.

---

## 11. Wording

> **Non-normative note.** The wording below is not primarily intended for standardization. Its purpose is to demonstrate how a networking-focused, use-case-first design produces a dramatically leaner specification footprint. Compare this compact specification against the machinery required by P2300/P3826—domains, completion schedulers, sender transforms, query protocols—and observe how much simpler an execution model becomes when designed specifically for I/O workloads.

### 11.1 Header `<io_awaitable>` synopsis [ioawait.syn]

```cpp
namespace std {
  // [ioawait.concepts], concepts
  template<class A> concept io_awaitable = see-below;
  template<class T> concept io_awaitable_task = see-below;
  template<class T> concept io_launchable_task = see-below;
  template<class E> concept executor = see-below;

  // [ioawait.execref], class executor_ref
  class executor_ref;

  // [ioawait.execctx], class execution_context
  class execution_context;

  // [ioawait.launch], launch functions
  template<executor Ex, class... Args>
    unspecified run_async(Ex const& ex, Args&&... args);

  template<executor Ex, io_launchable_task Task>
    unspecified run_on(Ex const& ex, Task task);

  // [ioawait.thiscoro], namespace this_coro
  namespace this_coro {
    struct executor_tag {};
    struct stop_token_tag {};
    inline constexpr executor_tag executor{};
    inline constexpr stop_token_tag stop_token{};
  }
}
```

### 11.2 Concepts [ioawait.concepts]

#### 11.2.1 Concept `io_awaitable` [ioawait.concepts.awaitable]

```cpp
template<class A>
concept io_awaitable =
  requires(A a, coroutine_handle<> h, executor_ref ex, stop_token token) {
    a.await_suspend(h, ex, token);
  };
```

*Remarks:* A type satisfying `io_awaitable` can receive execution context (executor and stop token) via the three-argument `await_suspend` signature.

#### 11.2.2 Concept `io_awaitable_task` [ioawait.concepts.task]

```cpp
template<class T>
concept io_awaitable_task =
  io_awaitable<T> &&
  requires { typename T::promise_type; } &&
  requires(typename T::promise_type& p,
           typename T::promise_type const& cp,
           executor_ref ex, stop_token st, coroutine_handle<> cont) {
    { p.set_executor(ex) } noexcept;
    { p.set_stop_token(st) } noexcept;
    { p.set_continuation(cont, ex) } noexcept;
    { cp.executor() } noexcept -> same_as<executor_ref>;
    { cp.stop_token() } noexcept -> same_as<stop_token const&>;
    { cp.complete() } noexcept -> same_as<coroutine_handle<>>;
  };
```

*Remarks:* A type satisfying `io_awaitable_task` can both receive and propagate execution context through a coroutine chain.

#### 11.2.3 Concept `io_launchable_task` [ioawait.concepts.launch]

```cpp
template<class T>
concept io_launchable_task =
  io_awaitable_task<T> &&
  requires(T& t, T const& ct, typename T::promise_type const& cp) {
    { ct.handle() } noexcept -> same_as<coroutine_handle<typename T::promise_type>>;
    { cp.exception() } noexcept -> same_as<exception_ptr>;
    { t.release() } noexcept;
  } &&
  (is_void_v<decltype(declval<T&>().await_resume())> ||
   requires(typename T::promise_type& p) { p.result(); });
```

*Remarks:* A type satisfying `io_launchable_task` provides the interface needed by launch functions to bootstrap context and manage task lifetime.

#### 11.2.4 Concept `executor` [ioawait.concepts.executor]

```cpp
template<class E>
concept executor =
  copy_constructible<E> &&
  equality_comparable<E> &&
  requires(E& e, E const& ce, coroutine_handle<> h) {
    { ce.context() } noexcept -> see-below;
    { ce.on_work_started() } noexcept;
    { ce.on_work_finished() } noexcept;
    { ce.dispatch(h) } -> convertible_to<coroutine_handle<>>;
    { ce.post(h) };
  };
```

*Remarks:* The expression `ce.context()` shall return an lvalue reference to a type publicly derived from `execution_context`.

### 11.3 Class `executor_ref` [ioawait.execref]

```cpp
namespace std {
  class executor_ref {
    void const* ex_ = nullptr;                    // exposition only
    unspecified const* vt_ = nullptr;             // exposition only

  public:
    executor_ref() = default;
    executor_ref(executor_ref const&) = default;
    executor_ref& operator=(executor_ref const&) = default;

    template<executor E>
      executor_ref(E const& e) noexcept;

    explicit operator bool() const noexcept;
    bool operator==(executor_ref const&) const noexcept = default;

    execution_context& context() const noexcept;
    void on_work_started() const noexcept;
    void on_work_finished() const noexcept;
    coroutine_handle<> dispatch(coroutine_handle<> h) const;
    void post(coroutine_handle<> h) const;
  };
}
```

```cpp
template<executor E>
  executor_ref(E const& e) noexcept;
```
*Effects:* Type-erases `e`. The referent must remain valid for the lifetime of `*this` and any copies.

```cpp
explicit operator bool() const noexcept;
```
*Returns:* `true` if `*this` refers to an executor, otherwise `false`.

```cpp
coroutine_handle<> dispatch(coroutine_handle<> h) const;
```
*Effects:* Equivalent to calling `dispatch(h)` on the referenced executor.
*Returns:* A coroutine handle suitable for symmetric transfer.

```cpp
void post(coroutine_handle<> h) const;
```
*Effects:* Equivalent to calling `post(h)` on the referenced executor.

### 11.4 Class `execution_context` [ioawait.execctx]

```cpp
namespace std {
  class execution_context {
  public:
    class service;

    execution_context();
    execution_context(execution_context const&) = delete;
    execution_context& operator=(execution_context const&) = delete;
    ~execution_context();

    template<class T> bool has_service() const noexcept;
    template<class T> T* find_service() const noexcept;
    template<class T> T& use_service();
    template<class T, class... Args> T& make_service(Args&&... args);

    pmr::memory_resource* get_frame_allocator() const noexcept;
    void set_frame_allocator(pmr::memory_resource* mr) noexcept;

  protected:
    void shutdown() noexcept;
    void destroy() noexcept;
  };

  class execution_context::service {
  public:
    virtual ~service() = default;
  protected:
    service() = default;
    virtual void shutdown() = 0;
  };
}
```

```cpp
template<class T> T& use_service();
```
*Effects:* If a service of type `T` exists, returns a reference to it. Otherwise, creates one via `make_service<T>()` and returns a reference to the new service.
*Returns:* A reference to the service of type `T`.

```cpp
pmr::memory_resource* get_frame_allocator() const noexcept;
```
*Returns:* The memory resource used for coroutine frame allocation, or `pmr::get_default_resource()` if none was set.

```cpp
void set_frame_allocator(pmr::memory_resource* mr) noexcept;
```
*Effects:* Sets the memory resource used for coroutine frame allocation.

### 11.5 Launch functions [ioawait.launch]

```cpp
template<executor Ex, class... Args>
  unspecified run_async(Ex const& ex, Args&&... args);
```
*Returns:* A callable object `f` such that `f(task)` launches `task` with executor `ex`.
*Remarks:* `Args` may include a `stop_token`, an allocator, and completion handlers. The two-call syntax ensures the allocator is established before `task` is evaluated.

```cpp
template<executor Ex, io_launchable_task Task>
  unspecified run_on(Ex const& ex, Task task);
```
*Returns:* An awaitable that, when `co_await`ed, executes `task` on `ex` and resumes the caller on its original executor upon completion.
*Remarks:* The caller's executor is restored via `complete()` after `task` finishes.

### 11.6 Namespace `this_coro` [ioawait.thiscoro]

```cpp
namespace std::this_coro {
  struct executor_tag {};
  struct stop_token_tag {};
  inline constexpr executor_tag executor{};
  inline constexpr stop_token_tag stop_token{};
}
```

The `this_coro` namespace provides tag objects that can be awaited within a coroutine to retrieve execution context information.

```cpp
inline constexpr executor_tag executor;
```
*Remarks:* When awaited via `co_await this_coro::executor` inside a coroutine whose promise type supports executor access (e.g., inherits from `io_awaitable_support`), yields the `executor_ref` bound to that coroutine. This operation never suspends.

```cpp
inline constexpr stop_token_tag stop_token;
```
*Remarks:* When awaited via `co_await this_coro::stop_token` inside a coroutine whose promise type supports stop token access (e.g., inherits from `io_awaitable_support`), yields the `std::stop_token` propagated to that coroutine. This operation never suspends.

---

## Acknowledgements

This paper builds on the foundational work of many contributors to C++ asynchronous programming:

**Chris Kohlhoff** for Boost.Asio, which has served the C++ community for over two decades and established many of the patterns we build upon—and some we consciously depart from. The executor model in this paper honors his pioneering work.

**Lewis Baker** for his work on C++ coroutines, the Asymmetric Transfer blog series, and his contributions to P2300 and P3826. His explanations of symmetric transfer and coroutine optimization techniques directly informed our design.

**Dietmar Kühl** for P2762 and P3552, which explore sender/receiver networking and coroutine task types. His clear articulation of design tradeoffs—including the late-binding problem and cancellation overhead concerns—helped crystallize our understanding of where the sender model introduces friction for networking.

The analysis in this paper is not a critique of these authors' contributions, but rather an exploration of whether networking's specific requirements are best served by adapting to general-purpose abstractions or by purpose-built designs.

---

## References

1. [N4242](https://wg21.link/n4242) — Executors and Asynchronous Operations, Revision 1 (2014)
2. [N4482](https://wg21.link/n4482) — Some notes on executors and the Networking Library Proposal (2015)
3. [P2300R10](https://wg21.link/p2300) — std::execution (Michał Dominiak, Georgy Evtushenko, Lewis Baker, Lucian Radu Teodorescu, Lee Howes, Kirk Shoop, Eric Niebler)
4. [P2762R2](https://wg21.link/p2762) — Sender/Receiver Interface for Networking (Dietmar Kühl)
5. [P3552R3](https://wg21.link/p3552) — Add a Coroutine Task Type (Dietmar Kühl, Maikel Nadolski)
6. [P3826R2](https://wg21.link/p3826) — Fix or Remove Sender Algorithm Customization (Lewis Baker, Eric Niebler)
7. [Boost.Asio](https://www.boost.org/doc/libs/release/doc/html/boost_asio.html) — Asynchronous I/O library (Chris Kohlhoff)
