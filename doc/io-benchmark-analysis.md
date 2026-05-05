
# I/O Read Stream Benchmark Analysis

## Overview

This benchmark compares three execution models for asynchronous I/O across three stream abstraction levels and two I/O return types. Each cell executes 20,000,000 `read_some` calls on a single thread using a no-op stream, isolating execution model overhead from I/O latency. Each configuration is measured over 5 independent runs preceded by a warmup pass; tables report mean +/- standard deviation. The benchmark source is available at [14].

## Trade-off summary

| Property                          | capy IoAwaitable                      | P2300 sender/receiver                        |
|-----------------------------------|---------------------------------------|----------------------------------------------|
| Native concrete performance       | ~31 ns/op, 0 al/op                   | ~32–34 ns/op, 0 al/op                        |
| Type erasure cost (with recycler) | +5 ns/op, 0 al/op                    | +21–23 ns/op, 1 al/op (conditional on SBO fit [18]) |
| Type erasure mechanism            | Preallocated awaitable                | Recycled op_state (factory + virtual dispatch)|
| Why the gap persists              | No allocator path, no allocation call | Allocator fast path + factory + unique_ptr [3]|
| Synchronous completion            | ~1 ns/op (symmetric transfer)         | ~2.6 ns/op (trampoline [19]); ~1 ns in coroutine via `as_awaitable` [15] |
| Inline completion (await_ready)   | I/O in `await_ready`, no suspend      | No equivalent; `start()` is void and post-suspend [16] |
| Looping                           | Native `for` loop                     | `repeat_until` with trampoline [19]           |
| Bridge to other model (native)    | ~10–11 ns/op, 1 al/op                | ~16 ns/op, 0 al/op                          |
| Bridge to other model (erased)    | Faster in bex::task, equal in pipeline| ~32 ns/op, 0 al/op                          |
| Sender → awaitable bridge         | Zero-alloc synthetic frame (`frame_cb`) [10] | `as_awaitable` customization point [2] |
| Awaitable → sender bridge         | No customization point; `connect-awaitable` uses coroutine (manual HALO in stdexec [17]) [3] | N/A (native) |
| `as_awaitable` bypass             | N/A (native protocol)                 | Only leaf senders with explicit member [7, 15] |
| Compile-time env safety           | Structural (in function signature)    | Opt-in (per-sender constraint) [11, 12]      |
| Composability                     | Coroutine chains (`when_all`, `when_any`, `timeout`) | Sender algorithm pipelines  |


## Results

All values are mean +/- stddev over 5 runs (warmup excluded). **Bold** = native execution model (Column A). al/op counts allocation calls per operation, including recycled allocations. The bridge column (B) in Tables 1 and 3 shows 1 al/op — the `scheduled_resume` operation state when IoAwaitables post through `schedule()` → `connect()` → `start()`.

### Table 1: sender/receiver pipeline

|                | A: sender (native)   |          | B: awaitable (bridge) |          |
|----------------|---------------------:|---------:|----------------------:|---------:|
|                | ns/op                | al/op    | ns/op                 | al/op    |
| Native         | **34.3 +/- 0.1**    | **0**    | 46.3 +/- 0.0         | 1        |
| Abstract       | **47.1 +/- 0.2**    | **1**    | 46.4 +/- 0.0         | 1        |
| Type-erased    | **57.5 +/- 0.0**    | **1**    | 54.1 +/- 0.1         | 1        |
| Synchronous    | **2.6 +/- 0.3**     | **0**    | 5.1 +/- 0.1          | 0        |

### Table 2: capy::task

|                | A: awaitable (native)  |          | B: sender (bridge) |          |
|----------------|-----------------------:|---------:|--------------------:|---------:|
|                | ns/op                  | al/op    | ns/op               | al/op    |
| Native         | **31.4 +/- 0.2**      | **0**    | 48.1 +/- 0.3       | 0        |
| Abstract       | **32.3 +/- 0.2**      | **0**    | 72.2 +/- 0.2       | 1        |
| Type-erased    | **36.4 +/- 0.1**      | **0**    | 72.1 +/- 0.0       | 1        |
| Synchronous    | **1.0 +/- 0.2**       | **0**    | 19.0 +/- 0.0       | 0        |

### Table 3: beman::execution::task

|                | A: sender (native)   |          | B: awaitable (bridge) |          |
|----------------|---------------------:|---------:|----------------------:|---------:|
|                | ns/op                | al/op    | ns/op                 | al/op    |
| Native         | **31.9 +/- 0.0**    | **0**    | 43.5 +/- 0.1         | 1        |
| Abstract       | **55.2 +/- 0.0**    | **1**    | 43.4 +/- 0.0         | 1        |
| Type-erased    | **55.2 +/- 0.0**    | **1**    | 48.7 +/- 0.1         | 1        |
| Synchronous    | **1.0 +/- 0.2**     | **0**    | 2.9 +/- 0.2          | 0        |

## Analysis

### Native performance is equivalent

Both execution models achieve ~31–34 ns/op with zero allocations when consuming their native I/O type on a concrete stream. The sender pipeline's native result (34.3 ns/op) is ~2–3 ns higher than the coroutine models (~31–32 ns/op) due to the `trampoline_scheduler` interposed by `repeat_until` on every iteration [19] — even when operations complete asynchronously, the trampoline checks recursion depth and stack consumption before inlining. This overhead is the cost of stack overflow protection in the pure sender path.

### Type erasure costs diverge

- **capy::any_read_stream** (type-erased awaitable): **36.4 ns/op, 0 al/op**. The awaitable is preallocated at stream construction and reused across every `read_some` call. No allocator path is invoked per operation — placement construct into existing storage.

- **sndr_any_read_stream** (type-erased sender): **55.2–57.5 ns/op, 1 al/op**. Each operation traverses the recycling allocator fast path (TLS lookup, size-class bucketing, free-list pop/push), the factory lambda, `concrete_op` construction/destruction, virtual `start()`/`execute()` dispatch, and `unique_ptr` management.

The ~19–21 ns gap and the 1 al/op difference are irreducible with the current sender/receiver architecture. The allocation call represents the minimum structural cost of the `connect`/`start` protocol under type erasure: the operation state's type is erased, so it must be dynamically allocated — even with a recycling allocator.

**stdexec note:** stdexec's `any_sender` uses a 64-byte small buffer optimization (SBO) for type-erased operation states [18]. If a concrete operation state fits within this buffer, no heap allocation occurs — the state is constructed in-place. Whether the 1 al/op manifests depends on the operation state size. This benchmark's type-erased senders produce operation states that exceed the SBO threshold, but simpler senders may avoid the allocation entirely. The structural cost is therefore conditional on operation state size, not inherent to the protocol.

**libunifex note:** libunifex's `any_sender_of` uses `any_unique_t`, which always heap-allocates the type-erased operation state with no SBO [22]. Every `connect` on a type-erased sender allocates regardless of operation state size, confirming that the structural allocation cost is inherent to the `connect`/`start` protocol when the operation state type is erased.

The allocation counts (from the native Column A):

| Stream type           | pipeline | capy::task | bex::task |
|-----------------------|---------:|-----------:|----------:|
| Native                | 0        | 0          | 0         |
| Abstract              | 1        | 0          | 1         |
| Type-erased           | 1        | 0          | 1         |

The IoAwaitable column (capy::task) shows 0 al/op at all abstraction levels. The sender columns show 1 al/op once the stream is abstracted — the type-erased `concrete_op` allocation that the recycler serves from its free list.

### Bridges are competitive

The non-bold column in each table measures the cost of consuming the opposite I/O type through a bridge. Both bridges use universally correct protocols — not optimized for this benchmark's specific senders.

- **await_sender** (sender → IoAwaitable, Table 2 Col B): Adds ~17 ns and **zero allocations** for native senders. The bridge connects the sender to a bridge receiver and uses an atomic exchange protocol to handle synchronous and asynchronous completion uniformly. The receiver resumes the coroutine directly — no posting through the executor. Abstract and type-erased senders show 1 al/op — the type-erased `concrete_op` allocation from the sender side, not the bridge.

- **as_sender** (IoAwaitable → sender, Tables 1 and 3 Col B): For `beman::execution::task` (Table 3), the `awaitable_sender`'s `as_awaitable` member lets beman's `await_transform` [2, §33.9.11.8] call the IoAwaitable directly, bypassing beman's `sender_awaitable` wrapping. The overhead is ~11 ns over the native sender path. For the sender pipeline (Table 1), the bridge constructs a synthetic coroutine frame (`frame_cb`). Both paths incur 1 al/op from the `scheduled_resume` operation state — the P2300-mandated `schedule()` → `connect()` → `start()` protocol to resume a coroutine on the scheduler.

### The bridged awaitable outperforms native senders under abstraction

In Table 3 (`beman::execution::task`), the bridged awaitable column (Col B) is **faster** than the native sender column (Col A) for abstract and type-erased streams:

- Table 3 abstract: awaitable bridge 43.4 ns (1 al/op) vs sender native **55.2 ns (1 al/op)**
- Table 3 type-erased: awaitable bridge 48.7 ns (1 al/op) vs sender native **55.2 ns (1 al/op)**

Both sides now show 1 al/op at the abstract/type-erased level, but the awaitable bridge is still 7–12 ns faster. The bridged awaitable's performance is remarkably flat across abstraction levels (43.5/43.4/48.7 ns), while the native sender jumps sharply from 31.9 ns (native) to 55.2 ns (abstract). This occurs because the bridge cost is constant — the IoAwaitable's `await_suspend` always follows the same path regardless of stream abstraction — while the sender model's virtual dispatch and type erasure machinery scale with abstraction level.

In Table 1 (sender pipeline), the bridge is slightly faster: bridge 54.1 ns vs native 57.5 ns at the type-erased level — both at 1 al/op.

### P2300 bridge asymmetry

P2300 provides asymmetric support for bridging between senders and awaitables [2, 3]:

**Sender → Awaitable:** The `as_awaitable` customization point [2, §33.9.11.8] is the first-priority dispatch when a sender is `co_await`'d. A sender can provide an optimized awaitable representation via a member function, completely bypassing the generic `sender_awaitable` wrapping (connect + start + result variant + atomic). The benchmark's sender streams use this to provide an awaitable that inherits `work_item` and enqueues itself directly — single round-trip, zero allocation. This is a legitimate and expected customization [7].

**Awaitable → Sender:** There is no equivalent customization point on the awaitable side. When `connect()` encounters an awaitable, it uses `connect-awaitable` [17], which creates a bridge coroutine. P2006R1 explicitly notes this frame is "not generally eligible for the heap-allocation elision optimization (HALO)" [3]. stdexec mitigates the heap allocation by pre-allocating 64 bytes of storage inline in the operation state and overriding the coroutine's `operator new` to placement-construct into this buffer [17] — a manual HALO that avoids heap allocation when the coroutine frame fits. libunifex's `connect_awaitable` also uses a bridge coroutine for the same purpose but without the inline-storage optimization [22]. Capy's `as_sender` bridge avoids the coroutine frame entirely by using a synthetic `frame_cb`. P4126R0 [10] proposes standardizing this technique.

### stdexec's symmetric transfer recovery in `as_awaitable`

When a sender without an `as_awaitable` member is `co_await`'d inside a coroutine, stdexec wraps it in `__sender_awaitable` [15]. This wrapper recovers symmetric transfer for the sender protocol using an atomic compare-and-swap race detection mechanism:

1. `await_suspend` sets an atomic `__ready_` flag to `false`, then calls `start()` on the operation state.
2. If the sender completes inline (during `start()`), the receiver's completion handler attempts a CAS on `__ready_` from `false` to `true`. If `await_suspend` hasn't checked yet, the CAS succeeds and the receiver defers resumption to `await_suspend`.
3. Back in `await_suspend`, a second CAS detects that `__ready_` is already `true` and returns the current coroutine handle — achieving symmetric transfer with a flat stack.
4. If the sender completed asynchronously, the CAS finds `__ready_` still `false`, sets it to `true`, and returns `noop_coroutine()` to suspend. The receiver resumes the continuation later.

An additional thread ID check short-circuits the atomic protocol: if completion occurs on a different thread, it is definitionally asynchronous and the receiver resumes directly.

When the sender is statically known to complete inline (via the `__completes_inline` concept [21]), stdexec uses a specialized code path that skips the atomic entirely — `await_suspend` calls `connect` and `start` synchronously and returns the coroutine handle directly [15].

This mechanism recovers symmetric transfer **only when a sender is `co_await`'d inside a coroutine**. It does not help the pure sender/receiver pipeline path (Table 1), where no coroutine exists to provide `await_suspend`.

**libunifex note:** libunifex's sender-to-awaitable bridge does not implement this atomic exchange protocol. Its `_as_awaitable::await_suspend` calls `start()` and returns `void` — the coroutine always suspends unconditionally [22]. If the sender completes synchronously during `start()`, the receiver's `complete()` method directly calls `continuation_.resume()`, resuming the coroutine from within the `start()` call stack. This risks stack buildup on repeated synchronous completions and demonstrates that the recovery mechanism is an implementation-specific optimization, not a structural property of the sender model.

### Table 3 and `as_awaitable`

Table 3's native sender column (Col A) benefits from `bex::task`'s `as_awaitable` dispatch. When a sender provides an `as_awaitable` member, `bex::task`'s `await_transform` calls it directly — the sender's `connect` and `start` methods are never invoked. stdexec's implementation confirms this dispatch priority: the `as_awaitable` CPO uses a `__first_callable` chain that checks the sender's member function first, before falling back to generic wrapping [15]. The benchmark's `sndr_read_stream::read_sender` provides exactly this: an `as_awaitable` that returns a lightweight `work_item` awaitable, identical in cost to the IoAwaitable path.

This explains why Table 3 native sender (31.9 ns/op) matches Table 2 native awaitable (31.4 ns/op) — both are measuring the awaitable path, not the sender protocol.

The existing P2300 networking implementation in beman::net [13] does not use `bex::task`. Its examples use a custom `demo::task` whose `await_transform` always creates a `sender_awaiter` that calls `connect` + `start` — with no `as_awaitable` check. Every `co_await net::async_receive(...)` in beman::net pays the full sender protocol cost. For beman::net users, Table 1 (sender pipeline) is more representative of actual per-operation overhead than Table 3.

Senders that do not provide `as_awaitable` — which includes most senders produced by P2300 algorithms like `let_value`, `then`, `when_all`, etc. — also go through the full `connect`/`start` path in `bex::task` via its generic `sender_awaitable` bridge. In stdexec, only `STDEXEC::task` and `exec::basic_task` define `as_awaitable` members [15]; no algorithm sender does. The `as_awaitable` optimization is only available to leaf senders that implement it explicitly.

### Compile-time safety

The IoAwaitable protocol's 2-argument `await_suspend(coroutine_handle<>, io_env const*)` structurally enforces that the execution environment is provided at suspension time. The dependency is in the function signature — the compiler rejects any call site that does not provide it.

In the sender/receiver model, environment availability is checked when a sender queries the receiver's environment inside `start()`. This check IS compile-time (it fails template instantiation if the query is unsupported), but it is opt-in: each sender must explicitly constrain its `connect` method. If the sender author forgets the constraint, the error appears as a deep template instantiation failure rather than a clear signature mismatch. P3164R4 [11] and P3557R2 [12] are addressing diagnostic quality for these errors but are not yet part of the C++26 standard.

### Sender looping and the trampoline

The sender/receiver pipeline (Table 1) uses stdexec's `repeat_until` [19] composed with `let_value` and `just` to implement a loop. The `repeat_until` algorithm wraps each iteration with a `trampoline_scheduler` that tracks recursion depth (default 16) and stack consumption (default 4096 bytes). When either limit is exceeded, execution is deferred to a queue and drained iteratively [19]. This prevents stack overflow from repeated inline completions — enabling the Synchronous row in Table 1.

The trampoline adds a small but measurable overhead to every iteration: the native pipeline (34.3 ns/op) is ~2–3 ns slower than the coroutine models (~31–32 ns/op) even for asynchronous operations, because the trampoline checks are executed unconditionally. This is the baseline cost of stack overflow protection in the pure sender path.

At the native level, the pipeline (34.3 ns/op) remains comparable to the coroutine models. The gap widens under type erasure because the pipeline's `connect` on each iteration traverses the factory + allocator path (1 al/op), whereas a coroutine reuses its frame (0 al/op).

**libunifex note:** libunifex provides both `repeat_effect_until` (with the same direct-recursion design and no built-in trampoline) and a separate `trampoline_scheduler` that tracks recursion depth (default 16) and defers to an iterative drain queue when the limit is exceeded [22]. The trampoline is not integrated into the repeat algorithm — users must compose them explicitly. This confirms the pattern: the trampoline is a general-purpose mitigation, not a solved problem within sender loop algorithms.

### Synchronous completions

In real networking I/O, many operations complete without waiting for the kernel: reads from a socket with data already in the receive buffer, writes to a non-full send buffer, DNS cache hits, TLS session resumptions, io_uring completions already batched in the completion queue. In a high-throughput server, this is the common case — a busy connection often has data waiting before the application reads it.

The Synchronous row measures this scenario. The I/O operation completes immediately — no executor posting, no thread pool round-trip.

|                | capy::task (awaitable) | beman::task (sender via as_awaitable) | sender pipeline (trampoline) |
|----------------|:----------------------:|:-------------------------------------:|:----------------------------:|
| Synchronous    | 1.0 ns/op              | 1.0 ns/op                             | 2.6 ns/op                    |

Both coroutine models achieve ~1 ns/op through symmetric transfer — `await_suspend` returns the coroutine handle and the compiler performs a tail call. The stack stays flat regardless of how many operations complete synchronously in sequence.

The sender/receiver pipeline achieves 2.6 ns/op using stdexec's `repeat_until` with `trampoline_scheduler` [19]. The trampoline detects inline completions and defers to an iterative queue when recursion limits are reached, keeping the stack bounded. This is 2.6x slower than the coroutine path — the overhead comes from the trampoline's recursion depth and stack consumption checks on every iteration, plus the occasional queue drain when limits are exceeded. Without the trampoline, repeated inline completions would cause stack overflow because `start()` is void [16] — the only way to deliver a result is through the receiver (`set_value`), which recurses into the next iteration's `connect`/`start`.

Coroutines handle synchronous completions more efficiently through two mechanisms, neither of which has a sender equivalent:

- **`await_ready`** — The awaitable can perform the I/O (e.g., `recvmsg`) in `await_ready` and return `true` if data is available. The coroutine never suspends — no handle manipulation, no symmetric transfer, no atomic exchange. This is the fastest possible path for inline completions. A sender cannot do this because `start()` is called inside `await_suspend`, after the coroutine has already suspended. The work cannot be moved earlier. (Both stdexec's `__sender_awaitable` [15] and libunifex's `_as_awaitable` [22] unconditionally return `false` from `await_ready`, confirming across implementations that senders cannot use this optimization even when wrapped.)

- **`await_suspend` return value** — If `await_ready` returns `false`, `await_suspend` can still complete the I/O and return the coroutine handle for symmetric transfer. The compiler performs a tail call — the stack stays flat regardless of how many operations complete synchronously in sequence. (stdexec recovers this mechanism for senders via `__sender_awaitable`'s atomic CAS protocol [15] — see *stdexec's symmetric transfer recovery* above — but only when the sender is consumed inside a coroutine.)

The sender model would need an equivalent mechanism (a way for `start()` to indicate "completed synchronously, here's the result"), which does not exist in P2300 and would be a fundamental change to the operation state protocol. stdexec's `__completion_behavior` system [21] can statically determine whether a sender completes inline, enabling optimized code paths at compile time, but this is a static property used by wrappers — it does not change the `start()` return type.

### What the bridge columns demonstrate

The bridged columns represent the real cost that arises when a library returns one I/O type but the application uses the other execution model. A networking library built on IoAwaitables will pay the `as_sender` tax when consumed from a sender pipeline. Conversely, a sender-based I/O library will pay the `await_sender` tax when consumed from `capy::task`.

Both bridges are designed for universal correctness:

- **await_sender** uses an atomic exchange protocol that safely handles senders completing synchronously during `start()`, asynchronously on the same thread, or asynchronously on a different thread.

- **as_sender** uses the P2300 environment query mechanism [2, §33.9.4] to obtain its executor, provides an `as_awaitable` member for coroutine integration [2, §33.9.11.8], and provides a `connect` path for sender pipelines — each using the most efficient mechanism available for that context.

The bridge overhead is modest — both directions add 11–17 ns for native streams. The `await_sender` bridge (Table 2 Col B) incurs zero allocation calls for native senders; the `as_sender` bridge (Tables 1 and 3 Col B) incurs 1 al/op from the `scheduled_resume` operation state required by P2300's `schedule()` → `connect()` → `start()` protocol.

### Scope and limitations

This benchmark measures per-operation overhead for sequential I/O in a tight loop. It does not measure:

- **Concurrent composition** — `when_all` over N streams, fan-out patterns.
- **Real I/O latency** — io_uring submit/complete cycles, network round-trips.
- **Multi-threaded work distribution** — cross-thread scheduling, work stealing, NUMA-aware dispatch.
- **Compile time and diagnostic quality** — template instantiation depth, error message clarity.


## Methodology

**Execution models** (one per table):

- **sender/receiver pipeline** — Pure sender pipeline using stdexec's `repeat_until` [19] + `let_value`. No coroutines. Driven by `sender_thread_pool` via `sync_wait`. The `repeat_until` algorithm wraps each iteration with a `trampoline_scheduler` [19] that bounds recursion depth and stack consumption, preventing stack overflow from repeated inline completions.
- **capy::task** — Capy's coroutine task, driven by `capy::thread_pool`. Natively consumes IoAwaitables.
- **beman::execution::task** — Beman's P2300 coroutine task [1], driven by `sender_thread_pool`. Natively consumes senders. **Note:** `bex::task`'s `await_transform` checks `as_awaitable` on the sender (first-priority dispatch per [exec.as.awaitable]). When the sender provides an `as_awaitable` member — as the benchmark's `sndr_read_stream` does — the task calls it directly, bypassing `connect`/`start` entirely. Table 3's native sender column (Col A) therefore measures the `as_awaitable` path, not the full sender protocol. This is the best-case scenario for senders in coroutines. See *Table 3 and `as_awaitable`* in the Analysis section for implications.

**Stream abstraction levels** (one per row):

- **Native** — Concrete stream type, fully visible to the compiler. No virtual dispatch or type erasure.
- **Abstract** — Virtual base class. The caller sees an interface; the implementation is hidden behind virtual dispatch.
- **Type-erased** — Value-type erasure. `capy::any_read_stream` for awaitables (zero steady-state allocation via cached awaitable storage); `sndr_any_read_stream` for senders (heap-allocated stream, sender type erasure via SBO).

**I/O return types** (one per column):

- **Column A** — Native I/O type for the execution model.
- **Column B** — Bridged I/O type (opposite protocol).

The native column (A) is shown in **bold**.

**Thread pools:**

Both thread pools inherit from `boost::capy::execution_context`, providing the same recycling memory resource for coroutine frame allocation. Both use intrusive work queues, mutex + condition variable synchronization, and identical outstanding-work tracking with `std::atomic<std::size_t>` and `memory_order_acq_rel`.

- **capy::thread_pool** — Used in Table 2 Col A. Posts `continuation&` objects via intrusive linked list (zero allocation per post).
- **sender_thread_pool** — Used in all other cells. Posts `work_item*` intrusively when the sender's operation state inherits `work_item` (zero allocation). Has no `post(coroutine_handle<>)` — P2300 execution contexts only expose `schedule()` [20], which returns a sender. To resume a coroutine on the scheduler, the caller must go through `schedule()` → `connect()` → `start()`, heap-allocating the operation state (one allocation per post).

The `schedule`/`connect`/`start` allocation path is used when IoAwaitables post through the executor adapter (Tables 1 and 3 Col B). This is a cross-protocol adaptation cost: the IoAwaitable produces a `coroutine_handle<>`, but P2300 has no way to accept a bare handle. The adapter must create a `scheduled_resume` operation state — `connect(schedule(sched), resume_receiver)` — and heap-allocate it because the coroutine is suspended and cannot host it. The operation state IS the queue node (inherits `work_item`), so no additional wrapping is needed, but the allocation is unavoidable. Real P2300 execution contexts (stdexec's `run_loop`, `static_thread_pool`) use the same intrusive queue pattern [6].

**Operation state recycling:**

Type-erased senders allocate their operation state (`concrete_op`) via `op_base::operator new`, which is overridden to use the same recycling memory resource used for coroutine frames. After warmup, these allocations are served from a thread-local free list in O(1) without calling global `operator new`. Both the coroutine frame recycler and the op_state recycler use the same `boost::capy::get_recycling_memory_resource()`, providing equivalent amortized allocation cost. The recycling allocator is functionally equivalent to what P3433R1 [9] proposes for allocator support in operation states.

This means the benchmark shows both models at their best: coroutine frames are recycled (standard practice for coroutine-based systems), and sender operation states are recycled (the strongest available mitigation for the structural allocation). The remaining performance differences reflect irreducible overhead — allocator fast-path cost, factory dispatch, virtual calls — not allocation policy. The al/op counts in the tables reflect allocation *calls* (including recycled), not global heap hits, so the structural allocation demand is visible even when the recycler eliminates the malloc cost.

**Allocation tracking:**

All allocation paths go through a single counter. Global `operator new` increments `g_alloc_count` before calling `malloc`. The recycling memory resource is wrapped in a `counting_memory_resource` proxy that increments the same counter before delegating — both for type-erased sender operation states (`op_base::operator new`) and for coroutine frame allocations (`polymorphic_allocator` passed to `bex::task`). This means al/op reflects *allocation calls per operation* regardless of whether they hit the global heap or the recycler's free list. The counter measures structural allocation demand, not allocation policy.

**Warmup:**

The first complete pass through all cells is a warmup (results discarded). This eliminates instruction cache, branch predictor, and CPU frequency scaling effects from the first execution model measured. The 5 measured runs begin from a thermally stable state.

**Compiler optimization:**

Each `co_await` suspends the coroutine and posts to the thread pool's work queue, acquiring a mutex, pushing to the intrusive queue, and signaling a condition variable. These are observable side effects that prevent the compiler from eliminating the benchmark loops.

## Bridge Implementations

### await_sender (sender → IoAwaitable)

Used in Table 2 Column B. Wraps a P2300 sender so it can be `co_await`'d inside a `capy::task`.

**Mechanism:** The bridge creates a `sender_awaitable` that placement-constructs the sender's operation state into a stack-allocated buffer. A `bridge_receiver` stores the sender's completion result in a `std::variant` discriminated by completion channel (value, error_code, exception_ptr, stopped).

**Synchronous completion safety:** The bridge uses an `std::atomic<bool>` exchange protocol. Both `await_suspend` (after calling `start()`) and the receiver's completion function call `done_.exchange(true, memory_order_acq_rel)`. Whichever side arrives second (sees `true` from the exchange) is responsible for resuming the coroutine. If the sender completes synchronously during `start()`, `await_suspend` detects this and returns the coroutine handle for symmetric transfer — the coroutine never actually suspends, avoiding stack corruption [5]. This is the same pattern used by stdexec's `__sender_awaitable` [15], which uses `std::atomic<bool>` with a compare-and-swap protocol and a `std::thread::id` check for the same purpose, and by beman::execution's `sender_awaitable` [1], which uses `atomic<thread::id>`.

**Result routing:** The bridge inspects the sender's error completion signatures at compile time. If the sender can complete with `set_error(std::error_code)`, `await_resume` returns `io_result<T>` so the error code is a value, not an exception. Otherwise, `await_resume` returns the value directly and rethrows exceptions.

**Zero bridge allocations:** The operation state lives on the coroutine frame (via placement new into a sized buffer). The receiver resumes the coroutine directly — no posting through the executor. The 0 al/op for native senders confirms this.

### as_sender (IoAwaitable → sender)

Used in Tables 1 and 3 Column B. Wraps an IoAwaitable so it can be consumed by the P2300 sender/receiver model.

**Mechanism:** The bridge constructs a synthetic coroutine frame (`frame_cb`) — a 24-byte struct whose first two members (resume/destroy function pointers) match the coroutine frame ABI layout used by MSVC, GCC, and Clang. `coroutine_handle<>::from_address(&cb_)` produces a valid handle whose `.resume()` calls the bridge's completion callback. This avoids allocating an actual coroutine frame, unlike P2300's `connect-awaitable` which creates a bridge coroutine with a heap-allocated frame that is "not generally eligible for the heap-allocation elision optimization (HALO)" [3]. stdexec mitigates this in its `__connect_awaitable` implementation by pre-allocating the coroutine frame inline in the operation state's storage buffer (64 bytes on 64-bit systems), providing a manual HALO that avoids heap allocation when the frame fits [17]. P4126R0 [10] proposes standardizing the synthetic frame technique as a "universal continuation model."

**Executor query:** The bridge obtains a Capy-compatible executor from the P2300 environment using the standard query forwarding mechanism [2, §33.9.4]. It defines a `get_io_executor` query CPO marked as a forwarding query (`forwarding_query(get_io_executor_t{})` returns `true`), ensuring it propagates through sender adapter chains via `FWD-ENV` [2, §33.9.3.5]. Since `starts_on` injects `sched_env<Scheduler>` (which only answers `get_scheduler` and `get_domain`), the bridge queries `get_scheduler(env)` — which IS forwarded — then queries the scheduler itself: `scheduler.query(get_io_executor_t{})`. The scheduler returns a Capy executor by value, which the bridge stores in the operation state. No benchmark-specific types appear in the bridge code.

**`as_awaitable` customization:** The `awaitable_sender` provides an `as_awaitable(Promise&)` member, which is the first-priority dispatch in `[exec.as.awaitable]` [2, §33.9.11.8]. stdexec's implementation confirms this priority: the `as_awaitable` CPO dispatches via `__first_callable` with the member function check (`__with_member`) as the highest priority, followed by transformed sender member, simple awaitable, and finally generic sender wrapping [15]. When `co_await`'d inside a `bex::task`, beman's `await_transform` calls this member instead of wrapping the sender in `sender_awaitable`. The member creates a standard awaitable that calls the IoAwaitable's 2-argument `await_suspend(handle, io_env const*)` directly, adapting it to the standard 1-argument protocol. This eliminates a double bridge (IoAwaitable → sender → `sender_awaitable` → awaitable) that would otherwise add connect/start/variant/atomic overhead.

**Completion routing:** The `frame_cb` callback calls `await_resume()` on the IoAwaitable and routes the result through P2300 completion channels based on the return type: `void` → `set_value()`, `error_code` → `set_value()`/`set_error(ec)`, other types → `set_value(T)`.

## References

[1] Beman Project. *execution26: Beman.Execution*. https://github.com/bemanproject/execution

[2] P2300R10. *std::execution*. Niebler, Baker, Hollman, et al. https://wg21.link/P2300

[3] P2006R1. *Eliminating heap-allocations in sender/receiver with connect()/start() as basis operations*. Baker, Niebler, et al. https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2020/p2006r1.pdf

[4] P3187R1. *Remove ensure_started and start_detached from P2300*. https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2024/p3187r1.pdf

[5] P3552R3. *Add a Coroutine Task Type*. https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2025/p3552r3.html

[6] NVIDIA. *stdexec: NVIDIA's reference implementation of P2300*. https://github.com/NVIDIA/stdexec

[7] C++ Working Draft. *[exec.as.awaitable]*. https://eel.is/c++draft/exec.as.awaitable

[8] P2079R6. *System execution context*. https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2025/p2079r6.html

[9] P3433R1. *Allocator Support for Operation States*. https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2025/p3433r1.pdf

[10] P4126R0. *A Universal Continuation Model*. https://isocpp.org/files/papers/P4126R0.pdf

[11] P3164R4. *Early Diagnostics for Sender Expressions*. https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2025/p3164r4.html

[12] P3557R2. *High-Quality Sender Diagnostics with Constexpr Exceptions*. https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2025/p3557r2.html

[13] Beman Project. *net: Beman.Net — P2300-based networking*. https://github.com/bemanproject/net

[14] Gerbino, S. *I/O Read Stream Benchmark source*. https://github.com/cppalliance/capy/tree/develop/bench/beman

[15] NVIDIA/stdexec. `include/stdexec/__detail/__as_awaitable.hpp` — `as_awaitable` CPO implementation: `__first_callable` dispatch priority (line 473), `__sender_awaitable` with atomic CAS symmetric transfer recovery (lines 309–342), `__sender_awaitable` for inline-completing senders (lines 349–396), `await_ready` always returns `false` (line 109), `__sender_awaitable_base` atomic state (lines 134–141). Only `STDEXEC::task` (line 256 of `__task.hpp`) and `exec::basic_task` (line 462 of `exec/task.hpp`) define `as_awaitable` members; no algorithm sender does. https://github.com/NVIDIA/stdexec

[16] NVIDIA/stdexec. `include/stdexec/__detail/__operation_states.hpp` — `start_t` CPO enforces `start()` returns `void` via `static_assert` (line 45). https://github.com/NVIDIA/stdexec

[17] NVIDIA/stdexec. `include/stdexec/__detail/__connect_awaitable.hpp` — Awaitable-to-sender bridge using a bridge coroutine with manual HALO: `operator new` placement-constructs into pre-allocated 64-byte storage in the operation state (lines 169–181), avoiding heap allocation when the coroutine frame fits. https://github.com/NVIDIA/stdexec

[18] NVIDIA/stdexec. `include/exec/any_sender_of.hpp` and `include/stdexec/__detail/__any.hpp` — Type-erased sender operation state uses 64-byte SBO buffer (`_iopstate_base_t` at line 397 of `any_sender_of.hpp`). `__emplace_into` (line 549 of `__any.hpp`) constructs in-place when the model fits, falling back to allocator-based heap allocation otherwise. https://github.com/NVIDIA/stdexec

[19] NVIDIA/stdexec. `include/exec/repeat_until.hpp` and `include/exec/trampoline_scheduler.hpp` — `repeat_until` wraps child senders with `trampoline_scheduler` (line 154 of `repeat_until.hpp`). The trampoline tracks recursion depth (default 16) and stack consumption (default 4096 bytes), deferring to an iterative queue when limits are exceeded (lines 147–174 of `trampoline_scheduler.hpp`). https://github.com/NVIDIA/stdexec

[20] NVIDIA/stdexec. `include/stdexec/__detail/__schedulers.hpp` — `schedule_t` CPO (lines 43–70): the only entry point for P2300 schedulers. No `post(coroutine_handle<>)` exists in the interface. https://github.com/NVIDIA/stdexec

[21] NVIDIA/stdexec. `include/stdexec/__detail/__completion_behavior.hpp` — Completion behavior tracking system: `__inline_completion` (line 58), `__completes_inline` concept (line 201), `__completes_where_it_starts` concept (line 205). Used by `as_awaitable` to select atomic-free code paths for statically-known inline senders. https://github.com/NVIDIA/stdexec

[22] Meta/libunifex. `include/unifex/await_transform.hpp`, `include/unifex/connect_awaitable.hpp`, `include/unifex/any_sender_of.hpp`, `include/unifex/repeat_effect_until.hpp`, `include/unifex/trampoline_scheduler.hpp` — Meta's prototype sender/receiver implementation (predates P2300 standardization). `_as_awaitable::await_ready()` unconditionally returns `false` (line 176 of `await_transform.hpp`). `await_suspend` calls `start()` and returns `void` with no atomic exchange protocol for synchronous completion detection (line 213). `connect_awaitable` uses a bridge coroutine without inline-storage manual HALO (line 188 of `connect_awaitable.hpp`). `any_sender_of` heap-allocates all type-erased operation states via `any_unique_t` with no SBO (line 52 of `any_sender_of.hpp`). `repeat_effect_until` uses direct recursion with no built-in trampoline (line 63 of `repeat_effect_until.hpp`); separate `trampoline_scheduler` exists but is not integrated (line 31 of `trampoline_scheduler.hpp`). https://github.com/facebookexperimental/libunifex
