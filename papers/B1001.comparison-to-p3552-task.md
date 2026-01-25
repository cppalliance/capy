| Document | BOOST1001 |
|----------|-----------|
| Date:    | 2026-01-22 |
| Reply-to: | Vinnie Falco \<vinnie.falco@gmail.com\> |
| Audience: | WG21, SG1, LEWG |

---

# Comparative Analysis: P3552 `task` and IoAwaitable for Asynchronous I/O

## Abstract

This paper provides a technical comparison between P3552's `std::execution::task` and the IoAwaitable protocol proposed in BOOST1000. We analyze the architectural differences in context propagation, allocator discovery, execution primitives, and cancellation semantics. Our analysis concludes that while P3552 provides a capable general-purpose coroutine type suitable for heterogeneous computing, its backward-query context model and scheduler-centric design introduce friction for I/O-first workloads. The IoAwaitable protocol's forward propagation model and executor-centric design emerge as better suited to the specific requirements of asynchronous networking.

---

## 1. Introduction

C++26 is poised to standardize `std::execution::task` (P3552R3) as the coroutine type for use with the sender/receiver model. Concurrently, alternative execution models have emerged that question whether sender/receiver's abstractions serve all asynchronous workloads equally well.

This paper examines two execution models:

1. **P3552 `task`** — A general-purpose coroutine type designed to integrate with `std::execution` (P2300), supporting scheduler affinity, allocator customization, and structured cancellation.

2. **IoAwaitable** — A coroutines-only execution model (BOOST1000) designed specifically for asynchronous I/O, featuring forward context propagation, executor-based dispatch, and platform reactor integration.

Our goal is not to declare one approach superior in all contexts, but to analyze their respective strengths and weaknesses for the specific domain of asynchronous I/O—networking, file operations, and related workloads.

### 1.1 Scope

This analysis focuses on:

- Context propagation mechanisms (scheduler/executor, allocator, stop token)
- Timing constraints for coroutine frame allocation
- Execution primitive semantics
- Platform I/O integration requirements
- Type erasure and ABI considerations

We do not address GPU computing, parallel algorithms, or heterogeneous dispatch—domains where P3552's sender/receiver integration provides clear benefits.

---

## 2. Architectural Overview

### 2.1 P3552 `task`

P3552 defines `task<T, Environment>` as a coroutine type that satisfies the sender concept. Key architectural decisions:

- **Sender/Receiver Integration**: `task` is a sender; it can be `connect()`ed to a receiver and composed with sender algorithms.
- **Scheduler Affinity**: Coroutines resume on the same scheduler by default, using `affine_on` semantics.
- **Backward Context Flow**: Context (scheduler, allocator, stop token) flows from receiver to sender via environment queries.
- **Type-Erased Scheduler**: `task_scheduler` provides runtime polymorphism for scheduler types.

The completion signatures are:

```cpp
completion_signatures<
    set_value_t(T),
    set_error_t(exception_ptr),
    set_stopped_t()
>;
```

### 2.2 IoAwaitable

IoAwaitable defines a protocol for coroutine-based I/O with three context elements:

- **Executor**: Controls where functions run (`dispatch` for inline-safe, `post` for always-queued)
- **Allocator**: Propagated via thread-local storage during a deterministic window
- **Stop Token**: Forwarded explicitly through awaitable chains

Key architectural decisions:

- **Coroutines-Only**: No sender/receiver; all composition through `co_await`
- **Forward Context Flow**: Context flows from caller to callee via `await_transform` injection
- **Three-Argument `await_suspend`**: `await_suspend(coro, executor_ref, stop_token)`
- **Type-Erased Executor**: `executor_ref` provides runtime polymorphism

---

## 3. Context Propagation

The fundamental architectural difference between P3552 and IoAwaitable lies in how context flows through operation chains.

### 3.1 Backward Flow in P3552

In the sender/receiver model, context is discovered through receiver environment queries *after* the sender and receiver are connected:

```cpp
// P3552 pattern (simplified)
template <receiver R>
void state<R>::start() & noexcept {
    auto sched = get_scheduler(get_env(rcvr));  // Query receiver
    auto token = get_stop_token(get_env(rcvr)); // Query receiver
    // ...
}
```

P2300R10 states explicitly:

> "In the sender/receiver model, as with coroutines, contextual information about the current execution is most naturally propagated from the consumer to the producer."

This backward flow serves heterogeneous computing well: the work graph is constructed, connected to a receiver that knows the target execution context, and then started. The receiver's environment tells senders where to dispatch work.

### 3.2 Forward Flow in IoAwaitable

IoAwaitable propagates context in the opposite direction—from caller to callee at each suspension point:

```cpp
// IoAwaitable pattern
template<IoAwaitable A>
auto await_transform(A&& awaitable) {
    return awaitable_wrapper{
        std::forward<A>(awaitable),
        executor_,      // From this coroutine
        stop_token_     // From this coroutine
    };
}
```

The child's `await_suspend` receives context as parameters:

```cpp
coro await_suspend(coro cont, executor_ref ex, std::stop_token token) {
    promise().set_executor(ex);
    promise().set_stop_token(token);
    promise().set_continuation(cont);
    return handle_;
}
```

Context is known at the launch site and propagates forward through the chain.

### 3.3 Implications for I/O

The direction of context flow has practical consequences:

| Aspect | Forward (IoAwaitable) | Backward (P3552) |
|--------|----------------------|------------------|
| Context availability | At invocation | After `connect()` |
| Allocator timing | Before `operator new` | After frame exists |
| Type visibility | No receiver type needed | Receiver type required |
| Composition | Through `co_await` | Through sender algorithms |

For I/O workloads where the caller knows the execution context at the point of initiating an operation, forward flow is natural. The application decides policy; operations inherit it.

---

## 4. Allocator Discovery

Allocator propagation presents the sharpest divergence between the two models due to a fundamental timing constraint.

### 4.1 The Timing Constraint

Coroutine frame allocation occurs *before* the coroutine body executes:

```cpp
auto t = my_coro(args...);  // operator new called HERE
co_await t;                  // await_transform called HERE
```

Any mechanism that provides context after invocation—receiver connection, `await_transform`, explicit method calls—arrives too late for `operator new`.

### 4.2 P3552 Approach

P3552 uses the `allocator_arg_t` parameter pattern:

```cpp
struct allocator_aware_context {
    using allocator_type = std::pmr::polymorphic_allocator<std::byte>;
};

template <class... A>
task<int, allocator_aware_context> fun(int value, A&&...) {
    co_return value;
}

// Usage requires allocator in argument list
sync_wait(fun(17, std::allocator_arg, my_allocator));
```

The promise type's `operator new` inspects arguments:

```cpp
template <class... Args>
void* operator new(size_t size, const Args&... args) {
    // Extract allocator from args if allocator_arg_t present
}
```

**Limitation**: Every coroutine in a call chain must include the allocator in its parameter list, even if it never uses it directly. The allocator becomes "viral," polluting interfaces throughout the codebase.

### 4.3 IoAwaitable Approach

IoAwaitable uses thread-local propagation during a deterministic execution window:

```cpp
inline thread_local std::pmr::memory_resource* current_allocator = 
    std::pmr::get_default_resource();

// In promise_type::operator new
static void* operator new(std::size_t size) {
    return current_allocator->allocate(size, alignof(std::max_align_t));
}
```

The "window" is the period while the parent coroutine body executes:

1. Parent coroutine resumes
2. Parent sets TLS to its allocator (in `await_resume` of initial/intermediate suspend)
3. Parent calls `child()`
4. Child's `operator new` reads TLS
5. Child returns task
6. Parent suspends

**Advantage**: Function signatures remain clean. Allocator policy is set at launch sites (`run_async`, `run_on`) and propagates transparently.

**Tradeoff**: Relies on thread-local storage, which has its own costs and constraints.

### 4.4 Analysis

| Aspect | IoAwaitable | P3552 |
|--------|-------------|-------|
| Signature pollution | No | Yes |
| TLS dependency | Yes | No |
| Type preservation | Type-erased (`memory_resource*`) | Full (template parameter) |
| Per-operation customization | At launch sites only | Yes |

For applications requiring different allocators at different points in a call chain, P3552's explicit approach provides more control. For applications with uniform allocation policy (the common case for I/O servers), IoAwaitable's transparent propagation is cleaner.

### 4.5 The Allocator Propagation Burden

While P3552 provides `get_allocator` to query the allocator from the environment, this query arrives *after* frame allocation. To propagate an allocator through a coroutine chain, every coroutine must:

1. Accept variadic template arguments (or explicit allocator parameter)
2. Query the allocator via `co_await read_env(get_allocator)`
3. Explicitly pass `std::allocator_arg, alloc` to every child coroutine

**P3552 Example** — Three coroutines using the same allocator:

```cpp
struct alloc_ctx {
    using allocator_type = std::pmr::polymorphic_allocator<std::byte>;
};

// Level 3: Must accept allocator via parameter
template<class... Args>
ex::task<void, alloc_ctx> level_three(Args&&...) {
    co_return;
}

// Level 2: Must query allocator AND forward it
template<class... Args>
ex::task<void, alloc_ctx> level_two(int x, Args&&...) {
    auto alloc = co_await ex::read_env(ex::get_allocator);
    co_await level_three(std::allocator_arg, alloc);
}

// Level 1: Must query allocator AND forward it
template<class... Args>
ex::task<int, alloc_ctx> level_one(int value, Args&&...) {
    auto alloc = co_await ex::read_env(ex::get_allocator);
    co_await level_two(42, std::allocator_arg, alloc);
    co_return value;
}

// Launch site
int main() {
    std::pmr::monotonic_buffer_resource resource;
    std::pmr::polymorphic_allocator<std::byte> alloc(&resource);
    ex::sync_wait(level_one(17, std::allocator_arg, alloc));
}
```

**IoAwaitable Contrast** — The same three coroutines:

```cpp
task<void> level_three() { co_return; }

task<void> level_two(int x) { 
    co_await level_three(); 
}

task<int> level_one(int value) { 
    co_await level_two(42); 
    co_return value; 
}

// Launch site - allocator specified once, propagates automatically
int main() {
    std::pmr::monotonic_buffer_resource resource;
    std::pmr::polymorphic_allocator<std::byte> alloc(&resource);
    run_async(executor, stop_token, alloc)(level_one(17));
}
```

The burden imposed by P3552:

- Every coroutine needs `Args&&...` or explicit allocator parameter
- Every coroutine must call `co_await ex::read_env(ex::get_allocator)`
- Every child invocation requires `std::allocator_arg, alloc`
- Function signatures no longer reflect their algorithmic interface
- The pattern is error-prone: forgetting to forward breaks the chain silently

### 4.6 Environment Type Fragmentation

Different `Environment` template parameters produce different task types, creating additional constraints for allocator propagation.

```cpp
struct PmrEnv { 
    using allocator_type = std::pmr::polymorphic_allocator<std::byte>; 
};
struct DefaultEnv { };  // Uses std::allocator<std::byte>

// These are DIFFERENT types
ex::task<void, PmrEnv> pmr_coro();
ex::task<void, DefaultEnv> default_coro();
```

Composition works—both are senders—but allocator propagation breaks:

```cpp
template<class... Args>
ex::task<void, PmrEnv> parent(Args&&...) {
    auto alloc = co_await ex::read_env(ex::get_allocator);  // pmr allocator
    
    // Child uses DefaultEnv - its frame uses std::allocator, not pmr!
    co_await default_coro();  
    
    // Cannot pass the allocator - type mismatch
    // co_await default_coro(std::allocator_arg, alloc);  // ERROR
}
```

Each `Environment` determines its own:

- `allocator_type` — how the frame is allocated
- `scheduler_type` — scheduler affinity behavior
- `stop_source_type` — cancellation mechanism
- `error_types` — error completion signatures

**Practical effect**: To ensure uniform allocator behavior across a chain, all coroutines must use the same `Environment` (or at least compatible `allocator_type`). This compounds the signature pollution—not only must you thread `allocator_arg` through parameters, but all task types in the chain must agree on the allocator type.

**IoAwaitable contrast**: A single `task` type with type-erased allocator (`memory_resource*`) propagated via TLS. All coroutines automatically use the same allocator regardless of where they are defined or how they are composed.

---

## 5. Execution Primitives

### 5.1 P3552: Scheduler with `schedule()`

P3552 operates in terms of schedulers. The `task_scheduler` type-erases any scheduler:

```cpp
class task_scheduler {
    shared_ptr<void> sch_;
public:
    sender schedule();
    // ...
};
```

Scheduler affinity ensures coroutines resume on the same scheduler:

```cpp
template <sender Sender>
auto await_transform(Sender&& sndr) noexcept {
    if constexpr (same_as<inline_scheduler, scheduler_type>)
        return as_awaitable(std::forward<Sender>(sndr), *this);
    else
        return as_awaitable(
            affine_on(std::forward<Sender>(sndr), SCHED(*this)), *this);
}
```

The `affine_on` algorithm adapts a sender to complete on the specified scheduler.

### 5.2 IoAwaitable: Executor with `dispatch()` and `post()`

IoAwaitable operates in terms of executors with two distinct operations:

```cpp
template<class E>
concept Executor = requires(E const& ce, std::coroutine_handle<> h) {
    { ce.dispatch(h) } -> std::convertible_to<std::coroutine_handle<>>;
    { ce.post(h) };
};
```

- **`dispatch(h)`**: Runs `h` inline if safe within the executor's rules; otherwise queues it. Returns a handle for symmetric transfer.
- **`post(h)`**: Always queues `h` for later execution. Never runs inline.

The distinction matters for correctness:

```cpp
void example() {
    std::mutex mtx;
    std::lock_guard lock(mtx);
    
    // DANGEROUS with dispatch: might run inline while lock held
    executor.dispatch(continuation);
    
    // SAFE: guaranteed to run after this function returns
    executor.post(continuation);
}
```

### 5.3 Comparative Analysis

| Aspect | IoAwaitable Executor | P3552 Scheduler |
|--------|---------------------|-----------------|
| Core operation | `dispatch()`, `post()` | `schedule()` → sender |
| Inline execution | Explicit (`dispatch` vs `post`) | Implementation-defined |
| Return type | `coroutine_handle<>` | Sender |
| Symmetric transfer | Direct handle return | Through sender machinery |
| Correctness control | Via operation choice | Via scheduler choice |

P3552's scheduler model is more abstract—it returns a sender that can be composed. IoAwaitable's executor model is more concrete—it directly resumes coroutine handles with explicit inline/queued semantics.

For I/O completion paths where latency matters, the ability to choose `dispatch` (run inline when safe, avoiding queue overhead) versus `post` (guarantee deferred execution) provides important control.

---

## 6. Scheduler Affinity vs Executor Inheritance

### 6.1 P3552 Scheduler Affinity

P3552 implements scheduler affinity: after each `co_await`, execution returns to the original scheduler:

```cpp
task<void> example() {
    // Running on scheduler A
    co_await some_operation();  // Might complete on scheduler B
    // Automatically transferred back to scheduler A
}
```

This is implemented via `affine_on`:

> "When an operation which is `co_await`ed completes the execution is transferred to the held scheduler using `continues_on`."

**Benefit**: Prevents a class of bugs where code assumes it's running on a specific context.

**Cost**: Every `co_await` potentially incurs scheduling overhead, even when the operation completed on the correct context.

### 6.2 IoAwaitable Executor Inheritance

IoAwaitable does not automatically return to the original executor. The executor is *inherited* forward:

```cpp
task<void> example() {
    // Running with executor A
    co_await child_operation();  // Inherits executor A
    // Still on whatever context child completed on
}
```

To explicitly change context:

```cpp
co_await run_on(different_executor, child_operation());
```

**Benefit**: No overhead when staying on the same context.

**Cost**: Developer must be aware of context changes.

### 6.3 I/O Implications

For I/O operations, the completion context is typically determined by the I/O object:

- A socket read completes on the I/O context's thread (epoll, IOCP, io_uring callback)
- The continuation should run on an application-specified executor

P3552's scheduler affinity handles this automatically but adds overhead. IoAwaitable requires explicit `run_on` when context changes are needed but avoids unnecessary scheduling when they're not.

---

## 7. Stop Token and Cancellation

### 7.1 IoAwaitable Stop Token

IoAwaitable propagates the stop token explicitly through `await_suspend`:

```cpp
coro await_suspend(coro cont, executor_ref ex, std::stop_token token) {
    promise().set_stop_token(token);
    // ...
}
```

The stop token is passed as a direct parameter—the same `std::stop_token` object flows through the entire chain without conversion or linking overhead. Each coroutine receives the token when it suspends:

```cpp
template<IoAwaitable A>
auto await_transform(A&& awaitable) {
    return awaitable_wrapper{
        std::forward<A>(awaitable),
        executor_,
        stop_token_     // Same token passed to child
    };
}
```

OS-level cancellation integrates directly:

```cpp
// In I/O operation
stop_callback cb(token, [&] {
    CancelIoEx(handle, &overlapped);  // Windows
    // or
    io_uring_prep_cancel(...);        // Linux
});
```

### 7.2 P3552 Stop Token

P3552 obtains the stop token from the receiver's environment using a backward query:

```cpp
// In state::start()
void start() & noexcept {
    auto st = get_stop_token(get_env(rcvr));
    // Initialize prom.token and prom.source linked to st
}
```

The task maintains its own stop source and token that are *linked* to the receiver's token:

```cpp
stop_source_type  source;  // Task's own stop source
stop_token_type   token;   // Task's own token, linked to parent
```

The linking ensures semantic forwarding:

- `prom.token.stop_requested()` returns `st.stop_requested()`
- `prom.token.stop_possible()` returns `st.stop_possible()`
- Callbacks registered on `prom.token` fire when the parent token requests stop

The task's environment then exposes this token to child operations:

```cpp
unspecified get_env() const noexcept {
    // env.query(get_stop_token) returns token
}
```

Cancellation is signaled via `just_stopped()` or when an awaited operation completes with `set_stopped`:

```cpp
co_await just_stopped();  // Triggers unhandled_stopped()
```

### 7.3 Cross-Environment Stop Token Propagation

When a `task<T, E1>` co_awaits a `task<T, E2>` with a different environment, stop tokens must bridge the gap:

1. The inner task is treated as a sender
2. `await_transform` wraps it: `as_awaitable(affine_on(inner_task, scheduler))`
3. The inner task connects to a receiver whose environment comes from the outer task
4. When the inner task's `state::start()` runs, it queries `get_stop_token(get_env(rcvr))`
5. The inner task links its own stop source to this token

Different `Environment` types can specify different `stop_source_type`:

```cpp
struct CustomEnv {
    using stop_source_type = custom_stop_source;  // Not inplace_stop_source
};
```

The linking mechanism forwards *semantics* rather than requiring type identity—each task may use a different stop token type internally while still responding to cancellation requests from its parent.

**Practical effect**: Stop token propagation works across environment boundaries, but requires stop source linking at each task boundary rather than direct token forwarding.

### 7.4 Comparison

| Aspect | IoAwaitable | P3552 |
|--------|-------------|-------|
| Discovery | Forward propagation | Backward receiver query |
| Timing | At `await_suspend` | After `connect()` |
| Token type | `std::stop_token` throughout | Per-environment `stop_token_type` |
| Propagation mechanism | Direct parameter passing | Stop source linking |
| Cross-task overhead | None (same token) | Linking at each boundary |
| OS integration | Explicit (`CancelIoEx`, etc.) | Not specified |

Both models support cooperative cancellation. IoAwaitable's direct parameter passing avoids linking overhead and uses a uniform token type. P3552's environment-based approach allows per-task stop source customization but requires linking at each task boundary.

---

## 8. Platform I/O Integration

### 8.1 P3552 Gap

P3552 does not address how I/O objects integrate with platform reactors. The scheduler abstraction is decoupled from I/O context:

- No `context()` accessor to reach the reactor
- No model for socket registration with epoll/IOCP/io_uring
- No strand abstraction for serialized access

An I/O library built on P3552 must provide these separately.

### 8.2 IoAwaitable Integration

IoAwaitable's executor concept includes context access:

```cpp
{ ce.context() } noexcept -> std::same_as<decltype(ce.context())&>;
```

I/O objects hold a reference to their execution context:

> "A socket needs the context to register with the reactor; the executor alone cannot provide this."

Strands are built on the executor model:

> "The `strand` we provide, for example, wraps an I/O context's executor to add serialization guarantees."

### 8.3 Analysis

P3552's generality comes at the cost of I/O-specific affordances. IoAwaitable's narrower focus allows tighter platform integration.

---

## 9. Type Erasure and ABI Considerations

### 9.1 P3552 Type Visibility

The sender/receiver model exposes operation state types through `connect_result_t`:

```cpp
template<class S, class R>
using connect_result_t = decltype(connect(declval<S>(), declval<R>()));
```

Composed operations accumulate types:

```cpp
template<class S, class R>
struct retry_op {
    using child_op_t = connect_result_t<S&, retry_receiver<S, R>>;
    optional<child_op_t> o_;  // Fully typed nested state
};
```

This enables compile-time optimization but creates ABI sensitivity.

### 9.2 IoAwaitable Type Hiding

IoAwaitable achieves type erasure through coroutine handles:

> "C++20 coroutines provide type erasure *by construction*... The frame is opaque: Callers see only a handle, not the promise's layout."

Composed operations expose only `task`:

```cpp
task<int> composed_operation() {
    co_return co_await inner_operation();  // Inner type hidden
}
```

The `executor_ref` type-erases executors at zero cost. The launcher coroutine (e.g., `run_async`, `run_on`) holds the fully-typed concrete executor, and `executor_ref` stores a non-owning reference to it:

```cpp
template<Executor Ex>
void run_async(Ex&& ex, std::stop_token token) {
    // ex lives in launcher's frame
    executor_ref ref{ex};  // Non-owning reference, no allocation
    // ref propagates through child coroutines
}
```

The launcher's lifetime extends beyond all child coroutines, so the concrete executor remains valid for the entire operation chain. No heap allocation is required for the type erasure—it's simply a pointer to the concrete executor plus a vtable pointer for dispatch.

### 9.3 Tradeoffs

| Aspect | IoAwaitable | P3552 |
|--------|-------------|-------|
| Compile-time optimization | Limited (type-erased) | Full type visibility |
| ABI stability | Stable (only `task` exposed) | Sensitive to implementation |
| Compile time | Single instantiation | N×M instantiations |
| Runtime cost | Zero (non-owning reference) | Minimal |

For I/O workloads where operations take 10,000+ nanoseconds, the 1-2ns type erasure cost is negligible. The ABI stability benefit may be significant for library authors.

---

## 10. Summary of Differences

| Capability | IoAwaitable | P3552 `task` |
|------------|-------------|-------------|
| **Context flow** | Forward (`await_suspend` params) | Backward (receiver queries) |
| **Allocator timing** | TLS window before `operator new` | `allocator_arg_t` in signature |
| **Execution primitive** | `executor` with `dispatch()`/`post()` | `scheduler` with `schedule()` |
| **Scheduler/executor model** | Inheritance (explicit change) | Affinity (auto-return) |
| **Stop token** | Via `await_suspend` parameter | Via receiver environment |
| **I/O context** | `context()` accessor | Not addressed |
| **Strand support** | Built on executor | Not addressed |
| **Type erasure** | Via `executor_ref` + coroutine handles | Via `task_scheduler` |
| **Sender integration** | None | Full |
| **OS cancellation** | Explicit integration | Not specified |

---

## 11. Suitability for Asynchronous I/O

### 11.1 P3552 Strengths for I/O

1. **Scheduler Affinity**: Automatically prevents context-switching bugs
2. **Structured Cancellation**: Clean `set_stopped` channel
3. **Sender Composition**: Works with `when_all`, `let_value`, etc.
4. **Custom Error Types**: `with_error<E>` avoids exception overhead

### 11.2 P3552 Weaknesses for I/O

1. **Allocator Signature Pollution**: Every coroutine needs `allocator_arg_t` parameter
2. **No `dispatch`/`post` Distinction**: Cannot optimize inline-safe vs always-queued
3. **Missing I/O Context Model**: Reactor integration not addressed
4. **Missing Strand Abstraction**: Serialization must be built externally
5. **Scheduling Overhead**: Affinity adds cost even when unnecessary
6. **Type Leakage**: `connect_result_t` creates ABI sensitivity

### 11.3 Assessment

P3552 provides a capable general-purpose coroutine type that can work with I/O. However, its design priorities—sender/receiver integration, heterogeneous computing support, backward context flow—introduce friction for I/O-first workloads.

The IoAwaitable model demonstrates what emerges when I/O requirements drive the design: simpler primitives, forward context flow, and explicit platform integration.

---

## 12. Conclusion

P3552 `task` and IoAwaitable represent different points in the design space:

- **P3552** optimizes for generality and sender/receiver integration, accepting complexity in exchange for composability across heterogeneous workloads.

- **IoAwaitable** optimizes for I/O-specific requirements, accepting reduced generality in exchange for simpler primitives and better platform integration.

For applications that:
- Already use `std::execution` for GPU/parallel workloads
- Don't need fine-grained `dispatch`/`post` control
- Can tolerate allocator signature pollution
- Don't need built-in strand serialization

P3552 `task` is a reasonable choice.

For applications that:
- Are I/O-first (networking, file operations)
- Need explicit control over inline vs queued execution
- Want clean function signatures without allocator threading
- Require tight platform reactor integration

The IoAwaitable model may be more appropriate.

This analysis suggests that **networking may benefit from purpose-built abstractions** rather than adaptation to frameworks optimized for heterogeneous computing. The future of asynchronous C++ need not be a single universal abstraction—purpose-built frameworks that excel at their primary use cases may better serve the community.

---

## References

1. [P3552R3] Dietmar Kühl, Maikel Nadolski. "Add a Coroutine Task Type." 2025-06-20.
2. [P2300R10] Michał Dominiak, et al. "std::execution."
3. [P2762R2] Dietmar Kühl. "Sender/Receiver Interface for Networking."
4. [P3826R2] Lewis Baker, Eric Niebler. "Fix or Remove Sender Algorithm Customization."
5. [BOOST1000] Vinnie Falco, et al. "IoAwaitables: A Coroutines-First Execution Model." 2026-01-21.

---

## Acknowledgements

This analysis builds on the work of Dietmar Kühl (P3552, P2762), whose clear articulation of design tradeoffs informed our understanding. The comparison is offered in the spirit of technical discourse, not criticism of the substantial effort behind `std::execution` and its related proposals.
