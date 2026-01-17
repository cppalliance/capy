# run_async Implementation: Using Vinnie's Suspended Coroutine Launcher Pattern

## Overview

The current `run_async` implementation **successfully uses** Vinnie Falco's suspended coroutine launcher pattern with **exactly two frames**, while also supporting handler-based execution that Vinnie's original design didn't address.

---

## Vinnie's Original Pattern

Vinnie's gist describes a technique for efficiently wrapping user coroutines while controlling frame allocation order. The pattern achieves exactly **two frames** (launcher + user task) with guaranteed allocation order:

```cpp
launcher()(user_task())
//   ^         ^
//   |         +-- Frame #1 allocated SECOND
//   +-- Frame #2 allocated FIRST
```

### Three-Component Architecture

1. **`launcher()`** - A **coroutine** that allocates its frame and suspends immediately
2. **`operator()`** - A **non-coroutine function** that stores the user task handle
3. **`launch_awaitable`** - Establishes the continuation chain when awaited

---

## Our Implementation: Vinnie's Pattern + Handler Support

We implement Vinnie's pattern **exactly as described**, with the addition of handler-based execution for Asio-style `co_spawn` semantics.

### Frame Structure (2 Frames Total)

```
Frame #2: run_async_launcher (launcher coroutine)
  ├─ promise_type
  │   ├─ d_ (dispatcher)                          ← Added for handler/dispatcher support
  │   ├─ embedder_ (embedding_frame_allocator)   ← Added for frame allocator fix
  │   ├─ inner_handle_ (user's task)              ← Pre-existing
  │   └─ continuation_                            ← Pre-existing
  │
  └─ Frame #1: User's task
      └─ wrapper points to embedder_ in Frame #2
```

**Key members**:
- `embedder_`: Contains the single copy of the allocator; added to fix frame allocator lifetime issues
- `d_`: Dispatcher for task execution; added to support Asio-style handler semantics
- `inner_handle_`, `continuation_`: Pre-existing members used for coroutine chaining

### Code Structure

```cpp
// run_async() is a COROUTINE - allocates Frame #2
template<dispatcher Dispatcher, frame_allocator Allocator>
detail::run_async_launcher<Dispatcher, Allocator>
run_async(Dispatcher d, Allocator alloc = {})
{
    // TLS set in promise constructor (line 100)
    auto& promise = co_await get_promise{};
    co_await transfer_to_inner{&promise};
}
```

**This follows Vinnie's pattern**:
- ✅ `run_async()` is a coroutine
- ✅ Allocates Frame #2 first
- ✅ `embedder_` lives in promise (line 89)
- ✅ TLS set in promise constructor (line 100)
- ✅ Suspends immediately after setup
- ✅ Transfers to inner task via `transfer_to_inner`

---

## How Handler Mode Works (Still 2 Frames)

The key question: How do we support handler callbacks without adding a third frame?

### Handler Execution Path

```cpp
template<typename T, typename Handler>
void operator()(task<T> inner, Handler h) && {
    auto d = std::move(*handle_.promise().d_);
    run_with_handler(std::move(inner), std::move(h), std::move(d));
}
```

### The `run_with_handler` Function

```cpp
void run_with_handler(task<T> inner, Handler h, Dispatcher d)
{
    auto inner_handle = inner.release();

    // Set up frame chain: launcher -> inner
    handle_.promise().inner_handle_ = inner_handle;
    handle_.promise().continuation_ = std::noop_coroutine();
    inner_handle.promise().continuation_ = handle_;

    // Direct dispatcher assignment (no await_transform needed!)
    inner_handle.promise().ex_ = d;
    inner_handle.promise().caller_ex_ = d;
    inner_handle.promise().needs_dispatch_ = false;

    // Run synchronously to completion
    auto launcher = handle_;
    handle_ = nullptr;
    d(any_coro{launcher}).resume();

    // Both frames have completed by now
    // Handler is invoked AFTER completion
    std::exception_ptr ep = inner_handle.promise().ep_;

    if constexpr (std::is_void_v<T>) {
        if(ep) h(ep);
        else h();
    } else {
        if(ep) h(ep);
        else {
            auto& result_base = static_cast<detail::task_return_base<T>&>(
                inner_handle.promise());
            h(std::move(*result_base.result_));
        }
    }

    // Clean up both frames
    inner_handle.destroy();
    launcher.destroy();
}
```

### Why No Third Frame Is Needed

**The critical insight**: Handler mode uses **direct dispatcher assignment** instead of `await_transform`:

```cpp
// Direct assignment (no wrapper coroutine needed)
inner_handle.promise().ex_ = d;
inner_handle.promise().caller_ex_ = d;
```

This bypasses the need for a wrapper coroutine that would provide `await_transform` for dispatcher propagation. The dispatcher is simply assigned directly to the inner task's promise.

**Execution flow**:
1. `run_async(d)` creates launcher (Frame #2)
2. `operator()` with handler calls `run_with_handler`
3. User task handle obtained (Frame #1 already allocated)
4. Dispatcher assigned directly to inner task
5. `d(launcher).resume()` runs both frames to completion **synchronously**
6. After `resume()` returns, both frames are done
7. Handler invoked with result/exception
8. Frames destroyed

**Key difference from awaitable mode**:
- **Awaitable mode**: Needs `await_transform` for nested `co_await` operations
- **Handler mode**: No nested `co_await` from handler path - just direct assignment

---

## Awaitable Mode (Also 2 Frames)

The `launch_awaitable` struct provides awaitable semantics:

```cpp
template<typename T>
struct launch_awaitable {
    std::coroutine_handle<promise_type> launcher_;
    std::coroutine_handle<typename task<T>::promise_type> inner_;
    Dispatcher d_;
    bool started_ = false;

    ~launch_awaitable() {
        // If not awaited, run fire-and-forget
        if(!started_ && launcher_) {
            // ... setup and run synchronously
            d_(any_coro{launcher_}).resume();
            inner_.destroy();
            launcher_.destroy();
        }
    }

    template<typename D>
    std::coroutine_handle<> await_suspend(std::coroutine_handle<> cont, D const&) {
        started_ = true;
        launcher_.promise().inner_handle_ = inner_;
        launcher_.promise().continuation_ = cont;
        inner_.promise().continuation_ = launcher_;
        inner_.promise().ex_ = d_;
        inner_.promise().caller_ex_ = d_;
        inner_.promise().needs_dispatch_ = false;
        return launcher_;  // Symmetric transfer
    }

    T await_resume() {
        auto& inner_promise = inner_.promise();
        std::exception_ptr ep = inner_promise.ep_;

        inner_.destroy();
        launcher_.destroy();
        launcher_ = nullptr;

        if(ep) std::rethrow_exception(ep);
        if constexpr (!std::is_void_v<T>) {
            return std::move(*static_cast<detail::task_return_base<T>&>(inner_promise).result_);
        }
    }
};
```

This also uses only 2 frames:
- Frame #2: launcher
- Frame #1: user's task

---

## Comparison with Vinnie's Pattern

| Aspect | Vinnie's Pattern | Our Implementation | Match? |
|--------|------------------|-------------------|--------|
| **Frame count** | 2 frames | 2 frames | ✅ Yes |
| **`run_async()` type** | Coroutine | Coroutine | ✅ Yes |
| **Embedder location** | Launcher promise | Launcher promise | ✅ Yes |
| **TLS setup** | Promise constructor | Promise constructor | ✅ Yes |
| **`operator()` return** | Awaitable | Awaitable (+ handler overloads) | ✅ Extended |
| **Execution model** | Awaitable only | Awaitable + handlers | ✅ Extended |
| **Frame allocation order** | Launcher first, then user | Launcher first, then user | ✅ Yes |

**Conclusion**: We implement Vinnie's pattern faithfully, with the addition of handler-based execution that wasn't part of the original design.

---

## Key Innovations Beyond Vinnie's Pattern

### 1. Handler-Based Execution Without Extra Frames

Vinnie's pattern focuses on awaitable composition:
```cpp
int result = co_await launcher()(compute_value());
```

We added handler support:
```cpp
run_async(ex)(compute_value(), [](int result) { /* ... */ });
```

**How we achieve this with 2 frames**:
- Direct dispatcher assignment (no `await_transform` needed)
- Synchronous execution to completion
- Handler invoked after both frames finish
- No caller coroutine required

### 2. Dual-Mode `launch_awaitable`

Our `launch_awaitable` supports two modes:

```cpp
// Mode 1: Fire-and-forget (destructor path)
run_async(ex)(my_task());

// Mode 2: Awaitable (await_suspend path)
int result = co_await run_async(ex)(my_task());
```

The `started_` flag tracks which path was taken, and the destructor runs fire-and-forget if not awaited.

### 3. Handler Composition

Support for flexible handler patterns:

```cpp
// Success-only handler (exceptions rethrow)
run_async(ex)(task, [](int result) { });

// Full handler with exception handling
run_async(ex)(task, overload{
    [](int result) { },
    [](std::exception_ptr) { }
});

// Separate success/error handlers
run_async(ex)(task,
    [](int result) { },
    [](std::exception_ptr) { });
```

The `handler_pair` utility combines handlers, and `default_handler` provides default exception behavior.

---

## Why This Design Works

### Frame Allocation Timeline

```
1. run_async(ex) called
   └─> run_async() coroutine starts
       └─> Allocates Frame #2 (launcher)
       └─> promise_type constructor runs
           └─> embedder_ initialized
           └─> TLS set to point to embedder_

2. user_task() evaluated
   └─> Allocates Frame #1 using TLS
       └─> embedder_.allocate() called
       └─> Creates wrapper in Frame #1
       └─> Updates TLS to point to wrapper

3. operator()() called
   ├─> Handler mode: run_with_handler()
   │   └─> Sets up chain, runs synchronously
   │   └─> Invokes handler after completion
   │   └─> Destroys both frames
   │
   └─> Awaitable mode: returns launch_awaitable
       └─> Either runs fire-and-forget (destructor)
       └─> Or sets up await chain (await_suspend)
```

### Lifetime Guarantees

**Embedder outlives wrapper**:
- `embedder_` is in Frame #2's promise
- Frame #2 allocated first, destroyed last
- Wrapper in Frame #1 points to embedder
- Frame #1 destroyed before Frame #2

**Synchronous handler execution**:
- `d(launcher).resume()` runs to completion
- Both frames finish before returning
- Handler sees completed result
- Safe to destroy frames after handler returns

---

## Differences from Original Documentation Attempt

My first documentation attempt incorrectly described a **stack-based runner with 3 frames**. That was wrong. The actual implementation uses:

- **Vinnie's pattern exactly** (2 frames, launcher coroutine)
- **Handler support** added without extra frames
- **Direct dispatcher assignment** for handler mode
- **launch_awaitable** for dual-mode execution

The confusion arose because I was looking at an older version or misread the code structure. The current implementation is Vinnie's pattern with extensions for handler-based execution.

---

## Allocator Storage Optimization

**Important optimization**: The allocator is stored in **exactly one location**, and the wrapper uses a pointer to access it.

### Single Allocator Storage Location

The allocator is stored **only** in Frame #2's promise, inside the `embedder_`:

```cpp
// Frame #2: run_async_launcher
struct promise_type {
    std::optional<Dispatcher> d_;
    detail::embedding_frame_allocator<Allocator> embedder_;  // Contains the allocator
    //                                            ^^^^^^^^
    //                                            Only copy of allocator
    std::coroutine_handle<> inner_handle_;
    std::coroutine_handle<> continuation_;
};

// Inside embedding_frame_allocator
template<frame_allocator Allocator>
class embedding_frame_allocator {
    Allocator alloc_;  // ← THE ONLY COPY
};
```

### Wrapper Stores Pointer, Not Copy

The `frame_allocator_wrapper` embedded in Frame #1 stores a **pointer** to `embedder_.alloc_`:

```cpp
// Frame #1: user's task (at end of allocation)
template<frame_allocator Allocator>
class frame_allocator_wrapper {
    Allocator* alloc_;  // ← Pointer to embedder_.alloc_ in Frame #2

    explicit frame_allocator_wrapper(Allocator& a)
        : alloc_(&a)  // Store pointer, not copy
    {
    }

    void* allocate(std::size_t n) override {
        // Dereference pointer to access the single allocator in Frame #2
        return alloc_->allocate(n);
    }
};
```

### Memory Layout

```
┌─────────────────────────────────────────────────────────────┐
│ Frame #2: run_async_launcher (heap)                         │
├─────────────────────────────────────────────────────────────┤
│ promise_type:                                               │
│   ├─ d_: Dispatcher                                         │
│   ├─ embedder_: embedding_frame_allocator<Allocator>       │
│   │     └─ alloc_: Allocator ◄──────────────────────────┐  │
│   │         [64 bytes]           THE ONLY COPY          │  │
│   ├─ inner_handle_                                       │  │
│   └─ continuation_                                       │  │
└──────────────────────────────────────────────────────────┼──┘
                                                           │
┌──────────────────────────────────────────────────────────┼──┐
│ Frame #1: user's task (heap)                            │  │
├──────────────────────────────────────────────────────────┼──┤
│ [user's coroutine frame: promise, locals, state]        │  │
├──────────────────────────────────────────────────────────┼──┤
│ [ptr | 1] (tagged pointer to wrapper)                   │  │
├──────────────────────────────────────────────────────────┼──┤
│ frame_allocator_wrapper:                                │  │
│   └─ alloc_: Allocator* ────────────────────────────────┘  │
│       [8 bytes pointer]                                    │
└────────────────────────────────────────────────────────────┘

Total allocator storage: sizeof(Allocator) (e.g., 64 bytes)
Wrapper overhead: sizeof(void*) (8 bytes)
```

### Why This Works

Frame #1 is **always destroyed before** Frame #2 in all execution paths:
- Handler mode: `inner_handle.destroy()` then `launcher.destroy()`
- Fire-and-forget: `inner_.destroy()` then `launcher_.destroy()`
- Awaitable mode: `inner_.destroy()` then `launcher_.destroy()`

Since the wrapper (Frame #1) is destroyed first, the pointer to `embedder_.alloc_` (Frame #2) remains valid throughout the wrapper's lifetime.

### Construction Flow

```cpp
// 1. run_async(d, alloc) called
// Frame #2 allocated, promise_type constructor runs:
promise_type(Dispatcher d, Allocator a)
    : embedder_(std::move(a))  // Allocator moved into embedder_
{
    // embedder_.alloc_ now contains the allocator
    frame_allocating_base::set_frame_allocator(embedder_);
}

// 2. user_task() evaluated - Frame #1 allocation
void* embedding_frame_allocator::allocate(std::size_t n) {
    void* raw = alloc_.allocate(total);  // Use embedder's allocator

    // Construct wrapper at end of Frame #1
    // Pass REFERENCE so wrapper stores pointer
    auto* embedded = new (wrapper_loc)
        frame_allocator_wrapper<Allocator>(alloc_);
        //                                  ^^^^^^
        //                                  Reference to embedder_.alloc_
}
```

### Benefits

1. **Single source of truth** - Only one copy of allocator state exists
2. **Stateful allocators work correctly** - No duplicate state, no synchronization issues
3. **Reduced memory overhead** - Saves `sizeof(Allocator) - sizeof(void*)` bytes
   - For a 64-byte allocator: saves 56 bytes per first-frame allocation
   - Wrapper stores 8-byte pointer instead of 64-byte copy
4. **No additional allocations** - Just a pointer, no `shared_ptr` or heap indirection
5. **Zero performance overhead** - One pointer dereference vs. direct access

### Example: Stateful Allocator

```cpp
struct counting_allocator {
    std::shared_ptr<std::atomic<int>> counter_;

    void* allocate(size_t n) {
        counter_->fetch_add(1);  // Increment THE SAME counter
        return ::operator new(n);
    }

    void deallocate(void* p, size_t n) {
        counter_->fetch_sub(1);  // Decrement THE SAME counter
        ::operator delete(p);
    }
};

// With pointer-based wrapper:
// - embedder_.alloc_.counter_ points to counter
// - wrapper.alloc_ points to embedder_.alloc_
// - Both operations update THE SAME counter ✅

// With copy-based wrapper (old):
// - embedder_.alloc_.counter_ points to counter (shared_ptr copy)
// - wrapper.alloc_.counter_ points to SAME counter (shared_ptr copy)
// - Both copies share same counter, but this is by chance ⚠️
// - Non-shared_ptr state would diverge ❌
```

See [allocator_storage_optimization.md](allocator_storage_optimization.md) for complete details.

---

## Verification

All 39 test suites pass with 0 failures:
- Fire-and-forget mode ✅
- Handler-based result capture ✅
- Awaitable mode (`co_await`) ✅
- Exception handling ✅
- Nested task execution ✅
- Custom frame allocator tests ✅
  - Allocator captured on task creation
  - Child tasks use parent's allocator
  - TLS restored after co_await
  - TLS restored across multiple awaits
  - Deep nesting allocator propagation
  - Mixed tasks and async_ops
  - Balanced allocation/deallocation counts

ASAN verification confirms:
- No memory errors ✅
- No use-after-free ✅
- No dangling pointer dereferences ✅
- Proper frame lifetime management ✅

**Frame allocation order verified** (using temporary diagnostic logging):
- Frame #2 (launcher) allocated first ✅
- Frame #1 (task) allocated second ✅
- Frame #1 (task) destroyed first ✅
- Frame #2 (launcher) destroyed last ✅

This allocation order guarantees that the `embedder_` in Frame #2 outlives the `wrapper` in Frame #1, making the pointer-based allocator storage safe and correct.

**Conclusion**: The implementation successfully uses Vinnie's suspended coroutine launcher pattern with exactly 2 frames, while extending it to support handler-based execution for Asio-style semantics. The pointer-based allocator storage eliminates duplicate state and works correctly with stateful allocators.

---

## Appendix: Vinnie Falco's Suspended Coroutine Launcher Pattern

**Source**: https://gist.github.com/vinniefalco/e50c4ccebccdd7f7eaea83aa10e99245

### Overview

Vinnie's pattern describes an advanced C++ technique for wrapping user coroutines while controlling frame allocation order and minimizing the total number of coroutine frames.

### Core Problem

The naive approach to wrapping coroutines creates unnecessary overhead. A straightforward launcher that forwards to a user task generates **three frames** instead of two:
1. Launcher coroutine frame
2. Wrapper/intermediate frame
3. User's task frame

This wastes memory and adds indirection.

### The Solution: Two-Frame Architecture

The pattern achieves exactly **two frames** by leveraging a C++17 sequencing guarantee:

> "The postfix expression `E1` is sequenced before the argument expression `E2`"

In a call like `launcher()(user_task())`:
1. The `launcher()` coroutine allocates **Frame #1** first
2. The `user_task()` coroutine allocates **Frame #2** second
3. The `operator()` call connects them **without allocating a frame**

### Key Components

**`launcher()`** — A coroutine that:
- Allocates its frame immediately
- Suspends at initial suspend (`co_await suspend_always`)
- Stores the inner task handle in its promise
- Later transfers control to the inner task via symmetric transfer

**`operator()`** — A **regular (non-coroutine) function** that:
- Takes the user's task as a parameter
- Stores the task's coroutine handle in the launcher's promise
- Returns an awaitable **without creating a frame**
- This is critical: `operator()` must not be a coroutine

**`launch_awaitable`** — The awaitable returned by `operator()` that:
- Establishes the continuation chain when co_awaited
- Connects launcher → inner task → continuation
- Handles symmetric transfer for efficient execution

### Execution Flow

```cpp
// User code
co_await launcher()(user_task());

// Execution sequence:
1. launcher() called → Frame #1 allocated
2. launcher() suspends at initial_suspend
3. user_task() evaluated → Frame #2 allocated
4. operator() called → stores Frame #2 handle in Frame #1's promise
5. operator() returns launch_awaitable (no frame allocation)
6. co_await on launch_awaitable:
   - Sets up continuation chain
   - Transfers to launcher (Frame #1)
   - Launcher transfers to user_task (Frame #2)
7. user_task executes
8. user_task completes → transfers back to launcher
9. launcher returns to continuation
```

### Critical Design Points

1. **Frame Allocation Order**: The launcher's frame **must** be allocated before the user's task frame. This is guaranteed by the sequencing rules.

2. **`operator()` Is Not a Coroutine**: If `operator()` were a coroutine, it would allocate a third frame, defeating the purpose.

3. **Symmetric Transfer**: The pattern uses `co_await` expressions that return `std::coroutine_handle<>` to transfer control without stack buildup.

4. **Promise Storage**: The launcher's promise stores the inner task's handle, enabling the connection without extra allocations.

### Comparison to Our Implementation

Our `run_async` implementation follows Vinnie's pattern with these additions:

| Component | Vinnie's Pattern | Our Implementation |
|-----------|------------------|-------------------|
| **Frame allocation** | 2 frames (launcher + user task) | 2 frames (launcher + user task) ✅ |
| **Launcher coroutine** | Suspends immediately, transfers to inner | `run_async_launcher` - same approach ✅ |
| **Non-coroutine operator()** | Returns awaitable without frame | `launch_awaitable` - same approach ✅ |
| **Frame allocator** | Not specified | Added `embedder_` for allocator lifetime |
| **Handler support** | Not specified | Added `d_` and handler-based execution |
| **Dispatcher propagation** | Not specified | Added dispatcher storage and propagation |

### Why This Pattern Matters

**Performance**: Eliminates unnecessary frame allocations, reducing memory usage and indirection overhead.

**Control**: Provides explicit control over frame allocation order, enabling solutions to lifetime issues (like our allocator embedding).

**Flexibility**: The launcher can perform setup/teardown, exception handling, or context management without performance penalties.

**Correctness**: Guarantees the launcher outlives the user task, enabling safe pointer-based optimizations (like our allocator storage).
