# Design Rationale: Executor Work-Counting Interface

## Context

The `Executor` concept requires `on_work_started()` and `on_work_finished()` as
public noexcept member functions. These functions allow the associated
`execution_context` to track outstanding work and determine when it is safe to
stop polling. This document records the design positions considered, their
trade-offs, and the rationale for the chosen interface, suitable for LEWG review.

## Background

An execution context (such as `io_context`) runs until there is no more work. The
work count is the mechanism by which it knows whether anything is still in flight.
Every asynchronous operation, composed operation, or detached coroutine must
contribute to this count, or the context may stop prematurely.

Boost.Asio has shipped `on_work_started()` / `on_work_finished()` as public
executor members since its inception. It also provides `executor_work_guard`, an
RAII wrapper that calls these functions in its constructor and destructor. Both
interfaces are in wide use.

## Positions

### Position 1: Public Raw Functions (Status Quo)

Expose `on_work_started()` and `on_work_finished()` as public members of every
executor, exactly as Asio does today. Provide `executor_work_guard` as a
convenience wrapper.

**Arguments for:**

- RAII always works, but for a measurable subset of cases it imposes a cost that
  manual adjustment avoids.
- The relationship is identical to `mutex::lock` / `mutex::unlock` versus
  `lock_guard`. The standard provides both; removing the primitives would be
  unprecedented.
- Library and algorithm authors have a legitimate need for fine-grained control.
  Real-world examples from Asio's own internals include:
  - **Batching**: a `when_all` or `when_any` that launches N child operations can
    bump the count once rather than N times.
  - **Conditional tracking**: skipping redundant work-count calls when the
    handler's executor and the I/O executor are the same instance.
  - **Split lifetimes**: composed operations where the "start" and "finish" of
    logical work do not nest lexically.
  - **Per-thread private counters**: accumulators that flush to the real count in
    bulk, reducing contention on a shared atomic.
  - **Avoiding redundant executor copies**: `executor_work_guard` stores a copy
    of the executor; in hot paths the copy itself is a cost.
- Restricting the interface does not eliminate bugs; it merely moves them behind
  a different abstraction boundary where they are equally possible but harder to
  diagnose.

**Arguments against:**

- The functions are error-prone. A missed `on_work_finished()` causes a hang; a
  spurious one causes premature shutdown. Neither failure is caught at compile
  time.
- Exposing them invites misuse by users who do not understand the invariants.
- WG21 review culture increasingly favors "safe by construction" interfaces and
  may reject a proposal that includes unguarded manual resource management.

### Position 2: RAII-Only via Work Token

Remove `on_work_started()` / `on_work_finished()` from the public interface
entirely. Replace them with a single function that returns an opaque RAII token:

```cpp
auto wt = ex.get_work_token();
// on_work_started fires at construction
// on_work_finished fires at destruction
```

**Arguments for:**

- Correct by construction. No path forgets to release.
- Aligns with the structured-concurrency philosophy of bounded lifetimes.
- Easier to teach and review.

**Arguments against:**

- **Type-erasure obstacle.** A type-erasing executor wrapper (analogous to
  `std::function` for executors) cannot name the concrete token type. The token
  either becomes a second type-erased object (adding overhead and complexity) or
  the wrapper must fall back to raw calls internally, reintroducing the very
  functions this position removes.
- **User-defined executors.** If the token type is fixed by the library (e.g.
  `work_guard<Executor>`), then every user-defined executor must grant friendship
  to it. This is a closed extension point - new guard types cannot be added
  without modifying every executor.
- **Performance.** The token carries a copy of the executor and a flag or count.
  In the batching, conditional-tracking, and per-thread-counter scenarios
  described in Position 1, the RAII wrapper imposes overhead that the manual
  interface avoids.
- **Expressiveness.** Some operations legitimately have non-nested, non-lexical
  work lifetimes. Forcing them into RAII requires auxiliary data structures (move
  the token into a container, optional, etc.) that add complexity without adding
  safety.

### Position 3: Counting Work Guard

Keep the raw functions but make them private. Provide only a `work_guard` that
holds a reference-counted work claim:

```cpp
class work_guard {
    Executor ex_;
    unsigned owns_;
public:
    work_guard(bool do_start = true);
    ~work_guard(); // calls on_work_finished if owns_ > 0
    void start();  // if (owns_++ == 0) calls on_work_started
    void finish(); // if (--owns_ == 0) calls on_work_finished
};
```

**Arguments for:**

- Retains fine-grained control (`start` / `finish`) without exposing the
  executor's raw interface.
- Multiple logical work items can share a single guard, avoiding N separate
  on_work_started/on_work_finished round-trips.
- Destructor acts as a safety net for unbalanced calls.

**Arguments against:**

- The counting destructor masks bugs. If a caller forgets a `finish()`, the
  destructor silently compensates. The bug is not eliminated; it is hidden. In
  the analogous mutex world, `lock_guard` deliberately does not provide
  `lock()`/`unlock()` members precisely to avoid this class of silent
  misbehavior.
- Loses the ability to reason about exactly when `on_work_started` fires. With
  the raw interface, a call to `on_work_started` has a single, observable effect.
  With a counting guard, the first `start()` fires the underlying call; the
  second does not. This makes debugging harder, not easier.
- Still requires friendship for user-defined executors if the raw functions are
  private.
- Does not address the per-thread batching or conditional-tracking use cases,
  which require direct access to the underlying count.

## Areas of Agreement

1. **RAII should be the default for users.** All positions agree that an
   RAII wrapper (guard or token) is the right everyday interface. Users who do
   not write composed operations or custom algorithms should never call raw
   work-count functions.

2. **Raw functions are error-prone for casual use.** No position disputes this.
   The disagreement is over whether the remedy is removal, encapsulation, or
   documentation.

3. **Algorithm authors need fine-grained control.** Even positions that favor
   restricting the interface acknowledge that library internals and advanced
   algorithm authors require the ability to manipulate work counts outside of
   strict RAII nesting.

4. **The analogy to `mutex` is structurally sound.** `lock` / `unlock` exist as
   public members; `lock_guard` and `unique_lock` wrap them for the common case.
   The executor work-count interface presents an identical layering question.

## Areas of Disagreement

1. **Should raw functions be public?** Position 1 says yes (precedent, necessity,
   performance). Positions 2 and 3 say no (error-prone, WG21 review risk).

2. **Is the type-erasure problem fatal to Position 2?** Proponents of Position 2
   consider it solvable (e.g. a type-erased token). Proponents of Position 1
   consider it a fundamental obstacle that reintroduces raw calls under a
   different name.

3. **Does a counting guard mask bugs or fix them?** Position 3 argues that
   silent cleanup on destruction is a feature (safety net). Position 1 argues it
   is a liability (silent misbehavior).

4. **Is "safe by construction" achievable here without unacceptable cost?**
   This is the core philosophical divide. One view holds that safety constraints
   should be imposed everywhere the language permits. The other holds that safety
   is a trade-off with a cost, and that a design which forecloses necessary
   optimization is not safer - it merely relocates the unsafety to a less
   visible place.

## Recommendation

The library exposes `on_work_started()` and `on_work_finished()` as public
members of the Executor concept (Position 1), accompanied by an `executor_work_guard`
RAII wrapper for the common case. This follows established practice in Asio and
mirrors the standard library's treatment of mutexes. The raw functions exist
because real-world composed operations and algorithm implementations demonstrably
require them, and the alternatives either reintroduce the same primitives behind
a type-erasure boundary or mask bugs through silent compensating logic.

The RAII wrapper remains the recommended interface for all user-facing code. The
raw functions are intended for library authors and advanced users who accept the
responsibility of maintaining the work-count invariant manually.
