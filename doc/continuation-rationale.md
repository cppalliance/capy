# Design Rationale: Continuation Type in the Executor Interface

## Context

This document captures the design space and trade-offs around replacing
`std::coroutine_handle<>` with a first-class `continuation` type in
capy's executor interface. The central question is whether the executor
concept should traffic in raw coroutine handles or in a richer type that
carries intrusive queue metadata. Secondary questions address where the
`continuation` object lives, how it is passed, and what this means for
the promise base, the `IoAwaitable` protocol, and downstream consumers
like corosio.

The consensus was reached through discussion and prototyping. The
implementation ships as a breaking change to the `Executor` concept.

## Current Consensus

The executor concept adopts `continuation&` as the parameter type for
`dispatch` and `post`:

```cpp
struct continuation
{
    std::coroutine_handle<> h;
    continuation* next = nullptr;
};

concept Executor = requires(E& e, continuation c) {
    { e.dispatch(c) } -> std::same_as<std::coroutine_handle<>>;
    { e.post(c) };
    // ...
};
```

Both fields are public. The `continuation` lives in the I/O awaitable
for caller-handle posting, and in combinator/trampoline state for
parent-dispatch and child-launch patterns. The `IoAwaitable` concept
is unchanged. The promise base (`io_awaitable_promise_base`) is
unchanged. The `dispatch` return type remains `std::coroutine_handle<>`
for symmetric transfer.

The rationale for these choices follows.

## Background

### The Executor Bottleneck

Every coroutine resumption in capy funnels through the executor's
`dispatch` or `post`. I/O completions, combinator child launches,
cancel callbacks, and cross-executor trampolines all converge on
these two operations. The executor interface is the narrowest
bottleneck in the library.

### The Allocation Problem

With `std::coroutine_handle<>` as the parameter, executors that queue
work must allocate a node to hold the handle. The thread pool wraps
every posted handle in a heap-allocated `work` struct:

```cpp
struct work : intrusive_queue<work>::node
{
    std::coroutine_handle<> h_;
    // ...
};

void post(std::coroutine_handle<> h) {
    auto* w = new work(h);   // per-post allocation
    q_.push(w);
}
```

Corosio's reactor scheduler has the same pattern: a `post_handler`
that inherits from `scheduler_op` and is heap-allocated for every
`post(coroutine_handle<>)` call. Corosio solved this for I/O
operations by using `scheduler_op*` (an intrusive node embedded in
the awaitable), but the executor-level `post(coroutine_handle<>)`
path remained allocating.

Frame allocation is already recycled via `recycling_memory_resource`.
Queue-node allocation is the last steady-state allocation in the hot
path.

### The Safety Problem

Users can obtain a `std::coroutine_handle<>` and call
`executor.post(h)` directly. Misuse of raw coroutine handles
(double resume, use-after-destroy, resuming on the wrong thread)
causes silent UB. The type system does nothing to prevent it.

### Concepts vs. Concrete Types

A concept specifies the least set of requirements that generic code
may rely on. The executor concept determines what `dispatch` and
`post` accept. Changing this parameter type is a breaking change to
the concept and all conforming executor implementations.

## The Parameter Type Question

Three options exist for how `dispatch` and `post` receive the
continuation:

### Option P1: By Value

```cpp
void post(continuation c) const;
```

**Arguments for:**

1. Simplest signature. Matches the original sketch.
2. No aliasing concerns — the executor gets its own copy.

**Arguments against:**

1. Breaks zero-allocation queuing. The executor links the
   continuation into an intrusive queue via `next`. If `c` is a
   stack-local copy, the copy is destroyed when `post` returns and
   the queue has a dangling pointer. The whole point of the intrusive
   `next` is that the executor queues the *original object*, not a
   copy.
2. For `dispatch`, the inline case (return `c.h` for symmetric
   transfer) works, but the fallback to `post` has the same problem.

### Option P2: By Reference (chosen)

```cpp
void post(continuation& c) const;
std::coroutine_handle<> dispatch(continuation& c) const;
```

**Arguments for:**

1. The executor links the original object into the queue. No copy,
   no dangling pointer.
2. The caller guarantees address stability — the `continuation`
   must outlive the queue residency. This is the same guarantee
   already required for coroutine frames and awaitable objects.
3. Cleaner than pointer — no null state to handle.

**Arguments against:**

1. Requires the caller to ensure the `continuation` is an lvalue
   with sufficient lifetime. A `continuation` constructed as a
   temporary cannot be passed.

### Option P3: By Pointer

```cpp
void post(continuation* c) const;
```

**Arguments for:**

1. Traditional for intrusive data structures. Nullable.

**Arguments against:**

1. Nullable without reason — a null continuation is meaningless for
   `post` and `dispatch`.
2. Pointer syntax at every call site (`&c` vs. `c`).

**Recommendation:** Option P2. By-reference is the only option that
supports zero-allocation intrusive queuing without introducing null
states. The address-stability requirement is inherent to intrusive
data structures and is already a property of the objects that embed
continuations (awaitables, combinator state).

## The Placement Question

The `continuation` needs a stable address while it sits in an
executor's queue. Two locations were considered:

### Option L1: In the Promise

`io_awaitable_promise_base` gains a `continuation` member. One
`continuation` per coroutine, reused across all suspension points.

**Arguments for:**

1. One canonical location per coroutine. No question about where
   it lives.
2. The promise outlives every suspension point, so the address is
   always stable.
3. `final_suspend` can dispatch the parent's continuation directly
   without any additional state.

**Arguments against:**

1. Changes the `IoAwaitable` concept. `await_suspend` must receive
   `continuation&` instead of `coroutine_handle<>`, or the awaitable
   must reach into the caller's promise to get the continuation. Both
   are protocol changes.
2. Burdens task authors. Every promise type that inherits from
   `io_awaitable_promise_base` grows by a pointer (the `next`
   field) even though most suspension points never queue the
   continuation (they use symmetric transfer inline).
3. Conflates two concerns. The promise stores "who resumes me when
   I'm done" — a parent-child relationship. The `continuation` with
   `next` means "I'm a queueable unit of work." These are different
   concepts. The parent's continuation is only queued when the child
   finishes and the parent must be posted to a different executor.
   In the common case (same executor, symmetric transfer), it is
   never queued.

### Option L2: In the Awaitable (chosen)

Each I/O awaitable embeds its own `continuation`. The awaitable
receives `coroutine_handle<>` in `await_suspend` as it does today,
wraps it in the embedded `continuation`, and passes that to
`post()`/`dispatch()`.

**Arguments for:**

1. No change to the `IoAwaitable` concept. The `continuation` is
   an implementation detail of the awaitable, not a protocol concern.
2. The awaitable has a stable address for the duration of the
   suspension (the compiler guarantees this for the operand of
   `co_await`).
3. Aligns with corosio's pattern, where I/O services already embed
   their operation state (`scheduler_op`) in the awaitable.
4. Zero burden on task authors. `task<T>`, `quitter<T>`, and future
   task types are unchanged.
5. Cancel callbacks store `continuation*` pointing into the
   awaitable, which outlives the suspension.

**Arguments against:**

1. A new `continuation` is initialized at every `co_await`. Not an
   allocation (it is embedded), but `next` and `h` are set each
   time.
2. Combinator and trampoline patterns (parent dispatch, child
   launch) do not have an I/O awaitable in scope. These sites need
   their own `continuation` storage in the combinator state or
   trampoline promise.

### Comparison

| Property | Promise (L1) | Awaitable (L2) |
|---|---|---|
| Changes `IoAwaitable` concept? | Yes | No |
| Continuations per coroutine | One, reused | One per `co_await` |
| Init cost per suspension | None (already set) | Set `h` and `next` |
| Alignment with corosio `scheduler_op` | Separate patterns | Same pattern |
| Burden on task authors | Yes — inherits extra pointer | None |
| Combinator / trampoline sites | Free (in promise) | Need explicit storage |
| `io_awaitable_promise_base` size | +8 bytes per coroutine | Unchanged |

**Recommendation:** Option L2. The `continuation` is about how an
I/O operation interacts with the executor's queue — that is the
awaitable's concern. The handful of combinator and trampoline sites
that need their own `continuation` storage are internal to the library
and explicitly annotated. The promise base stays lean, the IoAwaitable
protocol is untouched, and task authors see no change.

## The Dispatch Return Type Question

`dispatch` returns `std::coroutine_handle<>` for symmetric transfer.
Two options exist for what `dispatch` returns now that it accepts
`continuation&`:

### Option D1: Return `std::coroutine_handle<>` (chosen)

```cpp
std::coroutine_handle<> dispatch(continuation& c) const;
```

**Arguments for:**

1. Symmetric transfer is a language-level mechanism. `await_suspend`
   must return `std::coroutine_handle<>`. The return type of
   `dispatch` feeds directly into `await_suspend`'s return value.
2. The inline case returns `c.h` (the wrapped handle). The posted
   case returns `std::noop_coroutine()`. Both are already
   `coroutine_handle<>`.
3. No new type needed in the return position.

**Arguments against:**

None identified.

### Option D2: Return `continuation&` or `continuation*`

**Arguments for:**

1. Symmetry with the parameter type.

**Arguments against:**

1. `await_suspend` cannot return `continuation&`. The language
   requires `coroutine_handle<>`, `bool`, or `void`.
2. The caller would have to unwrap `.h` at every return site.
3. Returning a reference to the input parameter is semantically
   confusing — the executor may have queued the continuation and
   returned `noop_coroutine()`, in which case the reference points
   to a queued object.

**Recommendation:** Option D1. The return type stays
`std::coroutine_handle<>`. Symmetric transfer is a language
mechanism that operates on handles, not continuations.

## The Address Stability Invariant

A `continuation` must not move or be destroyed while it is linked
into an executor's queue. When `post(c)` is called, the executor
stores `&c` in an intrusive list via `c.next_`. If `c` moves or is
destroyed before the executor dequeues it, the list has a dangling
pointer.

This is not a new class of obligation. A `coroutine_handle<>` posted
to an executor has the same requirement: the coroutine frame it
points to must remain alive until the handle is resumed. The
difference is that the old executor interface hid this behind a
per-post heap allocation — `new work(h)` copied the handle into
owned storage, so the caller never had to think about it. With
`continuation&`, the queue node is the caller's object, making the
lifetime discipline explicit rather than hidden behind an allocation.

In coroutine code, the invariant is satisfied automatically:

- **I/O awaitables** are alive for the duration of the suspension
  (guaranteed by the compiler for the operand of `co_await`).
- **Combinator state** outlives all child runners by construction.
- **Trampoline promises** live inside heap-allocated coroutine frames.

The invariant is only visible in non-coroutine code (tests, manual
executor interaction), where the caller must ensure the `continuation`
is declared before the executor or otherwise outlives the queue
residency. This is the same care required when holding a raw
`coroutine_handle<>` — the handle must not dangle. The continuation
merely surfaces an obligation that was always present.

Practical guidelines:

- **Do not store continuations in containers that reallocate.**
  `std::vector<continuation>` is unsafe if the vector grows after
  any continuation has been posted. Use
  `std::unique_ptr<continuation[]>` (allocated once, never
  reallocated) or `std::array<continuation, N>`.

- **Declaration order matters in non-coroutine code.** A
  stack-local `continuation` posted to a `thread_pool` must be
  declared before the pool, so that C++ LIFO destruction destroys
  the pool (joining its threads) before destroying the continuation.

ASAN builds catch most violations.

## The Strand Question

The strand wraps an inner executor and provides serialized execution.
Its internal mechanism uses `strand_op` wrapper coroutines with frame
recycling. Two options exist for how the strand interacts with
`continuation`:

### Option S1: Strand Queues Continuations Directly

Replace `strand_op` with direct `continuation` queueing via `next`.

**Arguments for:**

1. Eliminates the wrapper coroutine and frame recycling machinery.
2. Consistent with the thread pool's approach.

**Arguments against:**

1. The strand_op wrapper exists for dispatch-loop control, not just
   queuing. When the strand resumes a coroutine, the coroutine may
   complete and its `final_suspend` may do symmetric transfer. The
   wrapper coroutine catches this: it calls `target.resume()`, and
   when the target suspends or the wrapper's own `final_suspend`
   fires, control returns to the dispatch loop. Without the wrapper,
   symmetric transfer from the target's `final_suspend` would escape
   the strand's dispatch loop entirely.
2. Frame recycling amortizes allocation to once per strand lifetime.
   Removing it does not save allocations — it moves them.

### Option S2: Strand Keeps Its Wrapper, Changes Input Signature (chosen)

The strand's `post(continuation& c)` extracts `c.h` and wraps it in
a `strand_op` as before. Only the public signature changes.

**Arguments for:**

1. Minimal change. The strand's proven serialization mechanism is
   untouched.
2. The `strand_op` wrapper and frame recycling continue to work
   exactly as before.

**Arguments against:**

1. The strand does not benefit from zero-allocation posting. Each
   `post` still creates a wrapper coroutine. (But the wrapper frames
   are recycled, so the steady-state allocation count is zero.)

**Recommendation:** Option S2. The strand's wrapper mechanism solves
a problem (`continuation` does not: dispatch-loop control). Changing
only the input signature is the minimal, safe approach.

## The Promise Base Question

`io_awaitable_promise_base` stores the parent's coroutine handle via
`set_continuation(coroutine_handle<>)` / `continuation()`. Should
this internal storage change from `coroutine_handle<>` to the
`continuation` struct?

### Option B1: Change Internal Storage

`cont_` becomes `continuation`. `set_continuation` still accepts
`coroutine_handle<>` and constructs the struct internally. Task
authors see no change.

**Arguments for:**

1. `final_suspend` can dispatch the parent's continuation directly
   to the executor without extra state.
2. Invisible to task authors — the conversion is internal.

**Arguments against:**

1. Every coroutine frame grows by 8 bytes (the `next` pointer),
   even though the parent's continuation is rarely queued. The common
   case (same executor, symmetric transfer) returns `c.h` inline —
   `next` is dead weight.
2. Conflates "who resumes me" with "I'm a queueable unit."

### Option B2: Keep Promise Base Unchanged (chosen)

`cont_` stays as `coroutine_handle<>`. Only the specific internal
types that dispatch through an executor at `final_suspend` store
their own `continuation`:

- `when_all_core::continuation_` (parent handle for combinator)
- `when_any_core::continuation_` (same)
- `dispatch_trampoline::parent_` (cross-executor trampoline)
- `run_awaitable_ex::task_cont_` (initial task dispatch)
- `run_async_trampoline::task_cont_` (same)

**Arguments for:**

1. Zero size increase for all coroutine frames.
2. Clean separation: the promise stores a handle for symmetric
   transfer; the `continuation` struct is only used where queuing
   actually occurs.
3. The affected sites are all library-internal, not user-facing.

**Arguments against:**

1. More explicit storage declarations in combinator and trampoline
   code. (But these are few and clearly annotated.)

**Recommendation:** Option B2. The 8-byte-per-frame cost is
unnecessary. The handful of internal sites that need a `continuation`
for executor dispatch are explicit about it.

## Impact on Corosio

Corosio is a separate library that consumes capy's executor interface.
The `continuation` change requires updates in corosio:

1. **`io_context::executor_type`** — `dispatch` and `post` signatures
   change. The fast-path logic (return `c.h` if on scheduler thread,
   else post) is structurally identical.

2. **`dispatch_coro`** — The single dispatch point for all
   reactor-based I/O completions. Currently takes `coroutine_handle<>`
   from the reactor_op; will take `continuation&`. The fast-path
   (`target<io_context::executor_type>()` check) extracts `c.h` for
   symmetric transfer.

3. **`scheduler::post(coroutine_handle<>)`** — Currently
   heap-allocates a `post_handler`. With `continuation`, the scheduler
   can queue the continuation directly via `next`, eliminating the
   allocation. Whether `continuation::next_` and `scheduler_op`'s
   intrusive queue unify or coexist is a corosio-internal design
   question.

4. **I/O operation types** (`reactor_op`, `overlapped_op`,
   `waiter_node`) — These store `coroutine_handle<>` and
   `executor_ref`. They would embed a `continuation` instead.

5. **IOCP constraint** — `overlapped_op` must remain an `OVERLAPPED`
   for the Windows API. `continuation` must coexist with `OVERLAPPED`
   inheritance, not replace it.

## Areas of Agreement

1. **The executor interface should not traffic in raw
   `coroutine_handle<>`.** The allocation cost and safety risk are
   both real.

2. **The `IoAwaitable` concept should not change.** Awaitables
   receive `coroutine_handle<>` in `await_suspend` and manage the
   `continuation` internally.

3. **The promise base should not carry a `continuation`.** The
   per-frame overhead is unjustified for a field that is rarely
   used for queuing.

4. **`dispatch` returns `std::coroutine_handle<>`.** Symmetric
   transfer is a language mechanism.

5. **Address stability is the caller's responsibility.** The
   `continuation` must outlive the queue residency. This is
   documented and enforced by ASAN.

## Summary

| Property | `coroutine_handle<>` (old) | `continuation&` (new) |
|---|---|---|
| Per-post allocation (thread_pool) | `new work(h)` every call | None (intrusive queue) |
| Per-post allocation (strand) | `strand_op` wrapper (recycled) | Same (wrapper retained) |
| Type safety | Raw handle, easy to misuse | Struct, harder to fabricate |
| `IoAwaitable` concept | `await_suspend(handle, env)` | Unchanged |
| Promise base | `coroutine_handle<>` | Unchanged |
| Combinator state | `coroutine_handle<>` fields | `continuation` fields |
| Symmetric transfer | `dispatch` returns handle | Same |
| Lifetime invariant | Frame must outlive handle (hidden by allocation) | Same obligation, explicit (no allocation) |
| Breaking change | — | Yes (executor concept) |

The core trade-off is between the simplicity of raw handles (freely
copyable, lifetime hidden behind per-post allocation) and the
performance and safety benefits of intrusive continuations
(zero-allocation posting, type system barrier against misuse). The
lifetime discipline is not new — a `coroutine_handle<>` always
required the frame to outlive the handle — but it becomes the
caller's explicit responsibility instead of being absorbed by a
heap allocation. In coroutine code, the existing lifetime guarantees
of awaitables and combinator state satisfy this automatically.
