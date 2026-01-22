| Document | BOOST1002 |
|----------|-----------|
| Date:    | 2026-01-22 |
| Reply-to: | Vinnie Falco \<vinnie.falco@gmail.com\> |
| Audience: | WG21, SG1, LEWG |

---

# Comparative Analysis: IoAwaitable and Boost.Cobalt Execution Models

## Abstract

This paper provides a technical comparison between the IoAwaitable protocol (BOOST1000) and Boost.Cobalt's execution model. We focus on the execution model: how executors, allocators, and cancellation tokens are specified, propagated, and accessed within coroutine chains. The two libraries make fundamentally different architectural choices: Cobalt is tightly coupled to Boost.Asio and assumes single-threaded execution per context, while IoAwaitable is executor-agnostic and threading-configurable—single-threaded without locking when given an appropriate executor, or multi-threaded with safe executor hopping. Our analysis reveals that IoAwaitable's forward propagation model, launch functions, automatic allocator propagation, and concept-based task extensibility offer advantages for I/O workloads requiring flexibility, while Cobalt provides a mature solution for Asio-based single-threaded applications.

---

## 1. Introduction

Boost.Cobalt is a mature C++20 coroutine library built on Boost.Asio, providing coroutine primitives (`task`, `promise`, `generator`) along with composition utilities (`join`, `race`, `gather`) and I/O wrappers. It represents one approach to coroutine-based asynchronous I/O in C++.

The IoAwaitable protocol (BOOST1000) proposes a different execution model designed from the ground up for coroutine-only asynchronous I/O, emphasizing forward context propagation, allocator control (frame recycling is mandatory for performance), executor hopping, and concept-based extensibility. Unlike Cobalt, IoAwaitable is not tied to any specific I/O library.

### 1.1 Scope

This analysis compares **only the execution model** aspects:

- Threading model and executor hopping
- Launch functions and context bootstrapping
- Task type extensibility
- Executor specification and propagation
- Allocator discovery and propagation
- Cancellation/stop token mechanisms
- Context access from within coroutines

We do not compare:

- Composition primitives (Cobalt's `join`/`race`/`gather`)
- Communication primitives (Cobalt's `channel`)
- Generator support
- I/O object wrappers

### 1.2 Terminology

Both libraries use similar but distinct terminology:

| Concept | IoAwaitable | Cobalt |
|---------|-------------|--------|
| Library coupling | None (executor-agnostic) | Boost.Asio required |
| Execution primitive | Executor | Executor (via Asio) |
| Type-erased handle | `executor_ref` | `asio::any_io_executor` |
| Cancellation | `std::stop_token` | `asio::cancellation_signal/slot` |
| Thread-local context | `current_allocator` (temporary) | `this_thread::*` (persistent) |

---

## 2. Architectural Overview

### 2.1 IoAwaitable

IoAwaitable defines a protocol where context flows **forward** from caller to callee through explicit parameters:

```cpp
template<typename A>
concept IoAwaitable =
    requires(A a, coro h, executor_ref ex, std::stop_token token) {
        a.await_suspend(h, ex, token);
    };
```

Key characteristics:

- **Threading-agnostic**: Single-threaded without locking (given appropriate executor), or multi-threaded with safe executor hopping
- **Executor-agnostic**: Not tied to any I/O library; defines minimal `executor_ref` with `dispatch`/`post`
- **Three-argument `await_suspend`**: Executor and stop token are explicit parameters
- **Thread-local allocator**: Temporary transfer mechanism during a deterministic execution window
- **Concept-based extensibility**: Any type satisfying `IoAwaitable` participates
- **Standard cancellation**: Uses `std::stop_token` directly

### 2.2 Boost.Cobalt

Cobalt builds on Boost.Asio's executor model with coroutine-specific extensions:

```cpp
template<typename Return>
struct cobalt_promise
    : promise_memory_resource_base,
      promise_cancellation_base<asio::cancellation_slot, asio::enable_total_cancellation>,
      promise_throw_if_cancelled_base,
      enable_awaitables<cobalt_promise<Return>>,
      enable_await_allocator<cobalt_promise<Return>>,
      enable_await_executor<cobalt_promise<Return>>,
      // ...
```

Key characteristics:

- **Tightly coupled to Asio**: Requires Boost.Asio's `io_context`, executors, and cancellation model
- **Single-threaded by design**: Coroutines cannot be resumed on a different thread than created on
- **Tagged argument injection**: `asio::executor_arg_t` and `std::allocator_arg_t` in coroutine parameters
- **Thread-local defaults**: Persistent `this_thread::get_executor()` and `this_thread::get_default_resource()`
- **Asio cancellation model**: `cancellation_signal` with type filtering
- **Fixed task types**: `task`, `promise`, `generator`, `detached`

---

## 3. Threading Model

The most fundamental difference between the two libraries is their threading philosophy.

### 3.1 Cobalt: Single-Threaded by Design

The Cobalt documentation states explicitly:

> "This library is single-threaded by design, because this simplifies resumption and thus more performant handling of synchronizations like race."

And further:

> "You can't have any coroutines be resumed on a different thread than created on, except for a task (e.g. using spawn)."

This constraint enables important optimizations:

- **Symmetric transfer assumed safe**: No executor comparison needed before direct coroutine-to-coroutine transfer
- **Lock-free composition**: `race`, `join`, and `channel` use intrusive lists without cross-thread synchronization
- **Thread-local channels**: Communication primitives are explicitly not for cross-thread use

The `cobalt::thread` type creates a new OS thread with its own `io_context`, but this is an isolated single-threaded world—not a shared multi-threaded context.

### 3.2 IoAwaitable: Threading as Configuration

IoAwaitable's threading model is determined by the executor, not baked into the library:

**Single-threaded configuration**: When using an executor whose implementation does not use a mutex (e.g., a single-threaded `io_context`), IoAwaitable runs without any locking overhead. Algorithm authors who want lock-free composition primitives can simply require a single-threaded executor as a precondition.

**Multi-threaded configuration**: When using a thread-pool executor or multiple execution contexts, IoAwaitable handles cross-thread transitions safely via launch functions (`run_async`, `run_on`).

**Symmetric transfer in both modes**: The `dispatch` operation performs a single pointer comparison in the type-erased `executor_ref`:

```cpp
// In executor_ref::dispatch
if (this == &current_executor) {
    return h;  // Symmetric transfer - no queue overhead
}
// Otherwise queue for later execution
```

This means single-threaded deployments get identical performance to Cobalt's always-assume-safe approach, while multi-threaded deployments get safe executor hopping—using the same application code.

### 3.3 Comparison

| Aspect | IoAwaitable | Cobalt |
|--------|-------------|--------|
| Threading model | Configurable (executor choice) | Single-threaded per execution context |
| Single-threaded performance | Same as Cobalt (pointer comparison) | Symmetric transfer assumed safe |
| Multi-threaded support | Yes, via launch functions | No (except `spawn`) |
| Locking overhead | None with single-threaded executor | None (assumed single-threaded) |
| Lock-free composition | Opt-in via executor precondition | Always (baked in) |
| Channel communication | Depends on executor | Thread-local only |

---

## 4. Launch Functions and Executor Hopping

### 4.1 IoAwaitable Launchers

IoAwaitable defines launch functions as first-class protocol participants. They bridge non-coroutine code into the coroutine world and enable executor hopping.

**`run_async` — from callbacks, main(), event handlers:**

```cpp
// Basic: executor only
run_async(ex)(my_task());

// Full: executor, stop_token, allocator, success handler, error handler
run_async(ex, st, alloc, on_success, on_error)(my_task());
```

The two-call syntax leverages C++17's guaranteed evaluation order:

> "The postfix-expression is sequenced before each expression in the expression-list." — [expr.call]

This guarantees the trampoline coroutine is allocated BEFORE `my_task()` is invoked, enabling proper allocator injection.

**`run_on` — switching executors within coroutines:**

```cpp
task<void> io_bound_work() {
    // Running on I/O thread
    
    // Spawn compute work on thread pool, return here when done
    int result = co_await run_on(worker_pool, compute_heavy_task());
    
    // Back on I/O thread
    co_await send_response(result);
}
```

This enables a common pattern: I/O on dedicated threads, compute on worker pools, with clean executor transitions.

### 4.2 Cobalt Approach

Cobalt provides `spawn` for cross-executor execution:

```cpp
spawn(worker_executor, my_task(), asio::use_future);
```

However, there is no `run_on` equivalent for mid-chain executor switching. The single-threaded design means coroutines generally stay on their creation thread.

### 4.3 Comparison

| Aspect | IoAwaitable | Cobalt |
|--------|-------------|--------|
| Entry point | `run_async(ex)(task)` | `co_main`, `run()` |
| Cross-executor spawn | `run_async`, `spawn` | `spawn` |
| Mid-chain executor switch | `run_on(ex, task)` | Not provided |
| Allocator injection timing | Before task creation (C++17 ordering) | Via tagged argument |

---

## 5. Heterogeneous Task Types

### 5.1 IoAwaitable: Concept-Based Extensibility

IoAwaitable defines a protocol via concepts, not concrete types:

```cpp
template<typename A>
concept IoAwaitable =
    requires(A a, coro h, executor_ref ex, std::stop_token token) {
        a.await_suspend(h, ex, token);
    };

template<typename T>
concept IoAwaitableTask =
    IoAwaitable<T> &&
    requires { typename T::promise_type; } &&
    requires(typename T::promise_type& p, executor_ref ex, std::stop_token st) {
        { p.set_executor(ex) } noexcept;
        { p.set_stop_token(st) } noexcept;
        // ...
    };

template<typename T>
concept IoLaunchableTask =
    IoAwaitableTask<T> &&
    requires(T& t, T const& ct, typename T::promise_type const& cp) {
        { ct.handle() } noexcept -> std::same_as<std::coroutine_handle<typename T::promise_type>>;
        { t.release() } noexcept;
        { cp.exception() } noexcept -> std::same_as<std::exception_ptr>;
    } &&
    (std::is_void_v<decltype(std::declval<T&>().await_resume())> ||
     requires(typename T::promise_type& p) { p.result(); });
```

The concept hierarchy:

- **`IoAwaitable`**: Any awaitable with three-argument `await_suspend`
- **`IoAwaitableTask`**: Adds promise requirements for context injection
- **`IoLaunchableTask`**: Adds requirements for launch functions (`run_async`, `run_on`): handle access, ownership transfer, exception/result retrieval

Any type satisfying the concept participates in the protocol. This enables:

- **Third-party task types**: Libraries can define their own task types that interoperate
- **Specialized tasks**: Domain-specific task types (e.g., `gpu_task`, `file_task`) can participate
- **Gradual adoption**: Existing coroutine types can be adapted to satisfy `IoAwaitable`

### 5.2 Cobalt: Fixed Task Types

Cobalt provides a fixed set of task types:

- `task<T>` — Lazy coroutine
- `promise<T>` — Eager coroutine
- `generator<T>` — Async generator
- `detached` — Fire-and-forget

All share the same promise base classes. There is no concept-based extension point for third-party task types to participate in Cobalt's context propagation.

### 5.3 Comparison

| Aspect | IoAwaitable | Cobalt |
|--------|-------------|--------|
| Task definition | Concept-based protocol | Fixed concrete types |
| Third-party tasks | Can satisfy `IoAwaitable` | Must use Cobalt types |
| Heterogeneous composition | Supported | Not supported |
| Extensibility | Open (satisfy concept) | Closed (use provided types) |

---

## 6. Executor Specification

### 6.1 IoAwaitable Approach

IoAwaitable propagates the executor through `await_suspend` parameters:

```cpp
coro await_suspend(coro cont, executor_ref ex, std::stop_token token) {
    promise().set_executor(ex);
    promise().set_stop_token(token);
    promise().set_continuation(cont);
    return handle_;
}
```

The parent coroutine's `await_transform` injects these parameters:

```cpp
template<IoAwaitable A>
auto await_transform(A&& awaitable) {
    return awaitable_wrapper{
        std::forward<A>(awaitable),
        executor_,
        stop_token_
    };
}
```

**Resolution precedence**:
1. Explicit: If the awaitable carries its own executor (e.g., `run_on`)
2. I/O object: If awaiting an I/O operation, use the I/O object's executor
3. Inherited: Otherwise, inherit from the parent coroutine

### 6.2 Cobalt Approach

Cobalt uses a dual mechanism: tagged arguments and thread-local defaults.

**For `promise<T>` (eager)**:

```cpp
template<typename ... Args>
cobalt_promise(Args & ...args)
    : promise_memory_resource_base(detail::get_memory_resource_from_args(args...)),
      exec{detail::get_executor_from_args(args...)}
{
    this->reset_cancellation_source(signal.slot());
}
```

The executor is extracted from arguments if `asio::executor_arg_t` is present, otherwise from `this_thread::get_executor()`.

**For `task<T>` (lazy)**:

```cpp
template<typename Promise>
std::coroutine_handle<void> await_suspend(std::coroutine_handle<Promise> h) {
    if constexpr (requires {h.promise().get_executor();})
        self->promise->exec.emplace(h.promise().get_executor());
    else
        self->promise->exec.emplace(this_thread::get_executor());
    // ...
}
```

### 6.3 Comparison

| Aspect | IoAwaitable | Cobalt |
|--------|-------------|--------|
| Propagation mechanism | Explicit `await_suspend` parameter | Tagged arg + awaiter inheritance |
| Type erasure | `executor_ref` | `asio::any_io_executor` |
| Thread-local fallback | No | Yes (`this_thread::get_executor()`) |
| Lazy task behavior | Inherits via `await_suspend` | Inherits via awaiter's promise |
| Eager task behavior | Set at launch site | Captured from args/TLS at creation |

---

## 7. Allocator Discovery

### 7.1 The Timing Constraint

Both libraries face the same fundamental constraint: coroutine frame allocation occurs *before* the coroutine body executes.

```cpp
auto t = my_coro(args...);  // operator new called HERE
co_await t;                  // await_transform called HERE (too late)
```

Any mechanism that provides the allocator after invocation arrives too late for `operator new`.

### 7.2 IoAwaitable Approach

IoAwaitable uses thread-local propagation during a deterministic "window":

```cpp
inline thread_local std::pmr::memory_resource* current_allocator = 
    std::pmr::get_default_resource();

// In promise_type::operator new
static void* operator new(std::size_t size) {
    return current_allocator->allocate(size, alignof(std::max_align_t));
}
```

The window is maintained by `await_resume`:

```cpp
auto initial_suspend() noexcept {
    struct awaiter {
        promise_type* p_;
        void await_resume() { 
            tls::current_allocator = &p_->alloc_;
        }
    };
    return awaiter{this};
}
```

**Key property**: Function signatures remain clean. Allocator policy is set at launch sites and propagates transparently.

### 7.3 Cobalt Approach

Cobalt uses both tagged arguments and thread-local defaults:

```cpp
template<typename ... Args>
static void * operator new(const std::size_t size, Args & ... args) {
    auto res = detail::get_memory_resource_from_args(args...);
    // Allocate with prepended resource pointer for deallocation
}
```

The `get_memory_resource_from_args` function searches for `std::allocator_arg_t`:

```cpp
template<typename ... Args>
pmr::memory_resource * get_memory_resource_from_args(Args &&... args) {
    using args_type = mp11::mp_list<std::decay_t<Args>...>;
    constexpr static auto I = mp11::mp_find<args_type, std::allocator_arg_t>::value;
    if constexpr (sizeof...(Args) == I)
        return this_thread::get_default_resource();
    else
        return std::get<I + 1u>(std::tie(args...)).resource();
}
```

### 7.4 The Allocator Propagation Burden

The most significant practical difference emerges when propagating allocators through a coroutine chain.

**Cobalt: Manual Propagation Required**

To propagate an allocator through three levels of coroutines in Cobalt:

```cpp
// Level 3: Must accept allocator via variadic template
template<class... Args>
cobalt::task<void> level_three(Args&&...) {
    co_return;
}

// Level 2: Must query allocator AND forward it explicitly
template<class... Args>
cobalt::task<void> level_two(int x, Args&&...) {
    auto alloc = co_await cobalt::this_coro::allocator;
    co_await level_three(std::allocator_arg, alloc);
}

// Level 1: Must query allocator AND forward it explicitly
template<class... Args>
cobalt::task<int> level_one(int value, Args&&...) {
    auto alloc = co_await cobalt::this_coro::allocator;
    co_await level_two(42, std::allocator_arg, alloc);
    co_return value;
}

// Launch site
int main() {
    std::pmr::monotonic_buffer_resource resource;
    std::pmr::polymorphic_allocator<std::byte> alloc(&resource);
    cobalt::run(level_one(17, std::allocator_arg, alloc));
}
```

The burden at each level:
- Every coroutine needs `Args&&...` or explicit allocator parameter
- Every coroutine must call `co_await this_coro::allocator` to query it
- Every child invocation requires `std::allocator_arg, alloc`
- Forgetting to forward breaks the chain silently—child uses default allocator

**IoAwaitable: Automatic Propagation**

The same three-level chain in IoAwaitable:

```cpp
// Level 3: Clean signature
task<void> level_three() { 
    co_return; 
}

// Level 2: Clean signature, no allocator handling
task<void> level_two(int x) { 
    co_await level_three();  // Inherits allocator automatically
}

// Level 1: Clean signature, no allocator handling
task<int> level_one(int value) { 
    co_await level_two(42);  // Inherits allocator automatically
    co_return value; 
}

// Launch site - allocator specified once
int main() {
    std::pmr::monotonic_buffer_resource resource;
    std::pmr::polymorphic_allocator<std::byte> alloc(&resource);
    run_async(executor, stop_token, alloc)(level_one(17));
}
```

The allocator propagates via TLS through the entire chain without touching any coroutine signature.

### 7.5 Comparison

| Aspect | IoAwaitable | Cobalt |
|--------|-------------|--------|
| Thread-local propagation | Yes | Yes |
| Tagged argument support | No | Yes (`std::allocator_arg_t`) |
| Signature pollution | None | Required for propagation |
| Propagation mechanism | Automatic via TLS window | Manual query + forward |
| Per-coroutine override | At launch sites only | Via argument tag at any call |
| Error mode | N/A | Silent fallback to default |

---

## 8. Cancellation Mechanisms

### 8.1 IoAwaitable Approach

IoAwaitable uses `std::stop_token` directly, propagated as a parameter:

```cpp
coro await_suspend(coro cont, executor_ref ex, std::stop_token token) {
    promise().set_stop_token(token);
    // ...
}
```

The same token object flows through the entire chain without conversion:

```cpp
template<IoAwaitable A>
auto await_transform(A&& awaitable) {
    return awaitable_wrapper{
        std::forward<A>(awaitable),
        executor_,
        stop_token_  // Same token passed to child
    };
}
```

OS-level cancellation integrates directly:

```cpp
stop_callback cb(token, [&] {
    CancelIoEx(handle, &overlapped);  // Windows
});
```

### 8.2 Cobalt Approach

Cobalt uses Asio's cancellation model with per-coroutine signals:

```cpp
template<typename Return>
struct cobalt_promise {
    mutable asio::cancellation_signal signal;
    // ...
    
    template<typename ... Args>
    cobalt_promise(Args & ...args) {
        this->reset_cancellation_source(signal.slot());
    }
};
```

Cancellation propagates via slot linking in `await_suspend`:

```cpp
template<typename Promise>
bool await_suspend(std::coroutine_handle<Promise> h) {
    if constexpr (requires {h.promise().get_cancellation_slot();})
        if ((cl = h.promise().get_cancellation_slot()).is_connected())
            cl.emplace<forward_cancellation>(*self->cancel_signal);
    // ...
}
```

**Cancellation types** provide filtering:

```cpp
asio::cancellation_type::none
asio::cancellation_type::terminal   // Only on fatal errors
asio::cancellation_type::partial    // Safe partial cancellation  
asio::cancellation_type::total      // Full cancellation
asio::cancellation_type::all        // terminal | partial | total
```

### 8.3 Comparison

| Aspect | IoAwaitable | Cobalt |
|--------|-------------|--------|
| Token type | `std::stop_token` | `asio::cancellation_signal/slot` |
| Propagation | Direct parameter passing | Slot linking via `forward_cancellation` |
| Per-coroutine source | No (inherited) | Yes (each promise owns a signal) |
| Filtering | None | `cancellation_type` bitmask |
| Filter customization | N/A | `reset_cancellation_state(filter)` |
| Cross-coro overhead | None (same token) | Slot registration at each boundary |

---

## 9. Context Access from Coroutines

Both libraries allow coroutines to query their execution context from within the coroutine body.

### 9.1 IoAwaitable Approach

IoAwaitable uses tag types that `await_transform` intercepts:

```cpp
task<void> example() {
    executor_ref ex = co_await get_executor();         // Never suspends
    std::stop_token token = co_await get_stop_token(); // Never suspends
}
```

### 9.2 Cobalt Approach

Cobalt provides similar accessors via the `this_coro` namespace:

```cpp
task<void> example() {
    executor ex = co_await this_coro::executor;
    auto alloc = co_await this_coro::allocator;
    auto ct = co_await this_coro::cancelled;
    auto state = co_await this_coro::cancellation_state;
}
```

### 9.3 Comparison

| Accessor | IoAwaitable | Cobalt |
|----------|-------------|--------|
| Executor | `co_await get_executor()` | `co_await this_coro::executor` |
| Allocator | Not specified | `co_await this_coro::allocator` |
| Stop/cancel query | `co_await get_stop_token()` | `co_await this_coro::cancelled` |
| Full cancel state | N/A | `co_await this_coro::cancellation_state` |
| Reset cancel filter | N/A | `co_await this_coro::reset_cancellation_state(...)` |

---

## 10. Lazy vs Eager Semantics

Both libraries distinguish between lazy and eager coroutine types.

### 10.1 IoAwaitable

IoAwaitable `task` is lazy—context is injected when awaited:

```cpp
task<int> compute();

task<void> parent() {
    // compute() returns immediately; context injected at co_await
    co_await compute();  // await_suspend receives executor, stop_token
}
```

Launch functions (`run_async`, `run_on`) bootstrap context for the root coroutine.

### 10.2 Cobalt

Cobalt distinguishes `task` (lazy) and `promise` (eager):

**`task<T>`** — Lazy, inherits context from awaiter.

**`promise<T>`** — Eager, captures context at creation:

```cpp
promise<int> p = eager_compute();  // Already running
int result = co_await p;            // Wait for completion
```

### 10.3 Comparison

| Aspect | IoAwaitable | Cobalt |
|--------|-------------|--------|
| Lazy type | `task` | `task` |
| Eager type | Not specified | `promise` |
| Lazy context source | `await_suspend` parameters | Awaiter's promise |
| Eager context source | N/A | Constructor args + thread-local |

---

## 11. Thread-Local State

### 11.1 IoAwaitable

IoAwaitable uses one thread-local variable, but it is **not** persistent global state—it is a temporary transfer mechanism with a defined execution window:

```cpp
inline thread_local std::pmr::memory_resource* current_allocator;
```

The window of validity:

```cpp
// Window start: launcher sets current_allocator before task() is called
current_allocator = &my_resource;
auto t = my_task();  // operator new reads current_allocator
// Window end: initial_suspend::await_resume copies value to promise
// After this point, current_allocator is not used by this coroutine
```

Outside this window, the thread-local value is not relied upon. Each coroutine captures its allocator in the promise during `initial_suspend`, then uses that captured value. This is fundamentally different from Cobalt's persistent thread-local executor state.

The executor is **not** stored in thread-local state—it propagates explicitly through `await_suspend`.

### 11.2 Cobalt

Cobalt uses multiple thread-local variables:

```cpp
namespace this_thread {
    pmr::memory_resource* get_default_resource() noexcept;
    pmr::memory_resource* set_default_resource(pmr::memory_resource* r) noexcept;
    
    executor& get_executor(const boost::source_location& loc);
    bool has_executor();
    void set_executor(executor exec) noexcept;
}
```

### 11.3 Comparison

| Thread-Local | IoAwaitable | Cobalt |
|--------------|-------------|--------|
| Allocator | Yes (temporary transfer) | Yes (persistent) |
| Executor | No | Yes (persistent) |
| Semantics | Transfer mechanism with defined window | Persistent global state |
| Accessor functions | Direct variable | Getter/setter functions |

---

## 12. Summary

| Capability | IoAwaitable | Cobalt |
|------------|-------------|--------|
| **Library coupling** | None (executor-agnostic) | Boost.Asio required |
| **Threading model** | Configurable (executor choice) | Single-threaded per context |
| **Single-thread performance** | Same as Cobalt (pointer comparison) | Symmetric transfer assumed safe |
| **Multi-thread support** | Yes, via launch functions | No (except `spawn`) |
| **Lock-free composition** | Opt-in via executor precondition | Always (baked in) |
| **Executor hopping** | `run_on` for mid-chain switching | `spawn` only |
| **Task extensibility** | Concept-based (open) | Fixed types (closed) |
| **Executor propagation** | Forward via `await_suspend` params | Tagged args + awaiter inheritance + TLS |
| **Executor type** | `executor_ref` | `asio::any_io_executor` |
| **Allocator propagation** | Automatic via TLS | Manual via tagged args |
| **Signature pollution** | None | Required for allocator propagation |
| **TLS semantics** | Temporary transfer window | Persistent global state |
| **Cancellation type** | `std::stop_token` | `asio::cancellation_signal/slot` |
| **Cancellation filtering** | None | `cancellation_type` bitmask |
| **Per-coro cancel source** | No | Yes |

---

## 13. Assessment

### 13.1 IoAwaitable Strengths

1. **Threading flexibility**: Configurable via executor—single-threaded without locking, or multi-threaded with safe executor hopping, using identical application code.

2. **Executor-agnostic**: Not tied to any I/O library; works with any executor satisfying the minimal concept.

3. **Automatic allocator propagation**: Set once at launch site, propagates through entire chain without signature pollution. Frame recycling is straightforward.

4. **Concept-based extensibility**: Third-party task types can participate by satisfying `IoAwaitable`, `IoAwaitableTask`, or `IoLaunchableTask`.

5. **Standard cancellation**: Uses `std::stop_token` directly, avoiding library-specific types.

6. **Simpler propagation model**: Executor and stop token are explicit `await_suspend` parameters—no slot linking, no persistent thread-local state.

7. **Mid-chain executor switching**: `run_on` enables clean transitions between I/O threads and worker pools.

8. **Lock-free composition available**: Algorithm authors can require single-threaded executors as preconditions for lock-free primitives.

### 13.2 Cobalt Strengths

1. **Lock-free composition by default**: Single-threaded assumption enables `race`, `join`, and `channel` without cross-thread synchronization—no preconditions required.

2. **Cancellation filtering**: Per-coroutine control over which cancellation types to respond to.

3. **Asio ecosystem**: Seamless integration with existing Asio-based code and libraries.

4. **Richer introspection**: More accessors for cancellation state management.

5. **Eager coroutines**: `promise` type provides eager execution with early context capture.

6. **Mature implementation**: Battle-tested in production environments.

### 13.3 Tradeoffs

The fundamental distinction is **flexibility vs. simplicity of assumptions**:

- **IoAwaitable** offers both threading flexibility AND lock-free composition when needed. Single-threaded deployments get identical performance to Cobalt. Multi-threaded deployments get safe executor hopping. Algorithm authors who want lock-free primitives can require single-threaded executors as preconditions. The cost is that the library is newer and less battle-tested.

- **Cobalt** bakes single-threaded assumptions into every algorithm, providing lock-free composition without any preconditions. The cost is restricted threading—coroutines must stay on their creation thread, allocator propagation requires explicit forwarding at each call site, and the library is tied to Boost.Asio.

---

## 14. Conclusion

IoAwaitable and Boost.Cobalt represent different points in the design space for coroutine execution models:

- **IoAwaitable** is executor-agnostic and threading-configurable. Single-threaded deployments get lock-free performance; multi-threaded deployments get safe executor hopping—with identical application code. Automatic allocator propagation keeps signatures clean. Concept-based extensibility allows heterogeneous task types. The three-argument `await_suspend` makes context flow explicit. Standard `std::stop_token` avoids library lock-in.

- **Cobalt** is tightly coupled to Boost.Asio and optimized for single-threaded execution contexts. Lock-free composition primitives work without preconditions. Cancellation filtering provides fine-grained control. The cost is restricted threading flexibility, allocator signature pollution, and Asio dependency.

The choice depends on application requirements:

- For applications requiring flexibility—multi-threaded support, executor-agnostic design, automatic allocator propagation, or heterogeneous task types—IoAwaitable provides a more general solution.
- For single-threaded applications deeply integrated with Asio that need lock-free composition without explicit preconditions, Cobalt provides a mature, battle-tested solution.

This analysis demonstrates that IoAwaitable's design achieves threading flexibility without sacrificing single-threaded performance, while Cobalt's design trades flexibility for simplicity of assumptions.

---

## References

1. [BOOST1000] Vinnie Falco, Steve Gerbino, Amlal El Mahrouss. "IoAwaitables: A Coroutines-First Execution Model." 2026-01-21.
2. [Boost.Cobalt] Klemens Morgenstern. Boost.Cobalt Library Documentation.
3. [Boost.Asio] Chris Kohlhoff. Boost.Asio Library Documentation.
4. [P2300R10] Michał Dominiak, et al. "std::execution."

---

## Acknowledgements

This analysis benefits from Klemens Morgenstern's work on Boost.Cobalt, which provides a mature reference for coroutine-based I/O in C++. The comparison is offered in the spirit of technical discourse—both approaches contribute valuable insights to the evolution of asynchronous C++.
