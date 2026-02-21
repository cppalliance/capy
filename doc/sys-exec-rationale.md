# System Executor Design Rationale

This document captures the design space and trade-offs around providing
a "system executor" (a global thread pool for offloading CPU-bound work
from I/O threads). It is intended for LEWG consumption as background
for whatever direction is ultimately chosen.

## The Problem

A coroutine running inside an `io_context` (the I/O event loop) sometimes
needs to perform a CPU-intensive operation - a 100ms call to bcrypt, an
image resize, a compression pass. Blocking the I/O thread is unacceptable.
The coroutine needs *somewhere else* to run that work:

```cpp
co_await run( /* what goes here? */ )( long_task() );
```

The question is how the coroutine obtains an executor for that "somewhere
else."

## Positions

### Position A: Provide a Global System Executor

A library-provided global thread pool, accessible from any context:

```cpp
co_await run( get_system_executor() )( long_task() );
```

**Arguments for:**

1. Every major OS ships a system thread pool (GCD, Windows thread pool,
   Linux work queues). Wrapping one is meeting users where they are.
2. Ergonomics matter. Users who "just want to not block the I/O thread"
   should not face a design gauntlet.
3. The analogy to `operator new` holds: a global allocator exists even
   though serious programs override it. A global thread pool can serve
   the same role - a sensible default that advanced users replace.
4. Without it, every function signature in a deeply nested call stack
   must thread a thread-pool executor parameter, even when no
   downstream coroutine actually uses it.

**Arguments against:**

1. Historical experience: every global executor deployment has led to
   problems. Libraries start posting work to the global pool, and the
   user loses control of scheduling policy, thread count, and priority.
2. Once a global exists in the standard, it cannot be removed. If it
   proves harmful (as precedent suggests), the damage is permanent.
3. The global pool's properties (thread count, affinity, priority) are
   unknowable to library code. Posting to "some pool" is posting to a
   random pool regardless of its name.
4. In any serious program, the thread pool for compute-intensive work
   must be under the control of the program author, not the library.
   Tuning can only be performed by whoever owns `main()`.

### Position B: Require Explicit User-Declared Thread Pools

The user declares their own `thread_pool` the same way they declare
their `io_context`, and passes both to launch sites:

```cpp
io_context ioc;
thread_pool tp;
run_async( ioc.get_executor(), tp.get_executor() )( my_task() );
```

**Arguments for:**

1. Symmetric with `io_context`: users already declare and manage their
   execution context for I/O; doing the same for compute is consistent.
2. The program author controls thread count, priority, and lifetime.
3. No hidden global state. Every resource is visible and traceable.
4. Practically, there are not that many `run_async` call sites in user
   code. And in many of those, the thread pool is genuinely needed
   anyway.

**Arguments against:**

1. Forces every call site to be aware of and responsible for a resource
   it may never use. The thread pool becomes "propagated by all, used
   by none."
2. Library code that internally calls `run_async` must accept and
   forward a thread-pool executor, even when none of its own logic
   needs one - purely because a downstream dependency might.
3. Users must make a configuration choice (thread count, type of pool)
   at a point where they may lack the contextual information to choose
   well.

### Position C: Propagate via `io_env` (the Coroutine Environment)

Add a second executor slot to the coroutine environment that is
propagated implicitly, like the I/O executor already is:

```cpp
struct io_env {
    executor_ref executor;
    executor_ref compute_executor;   // new
    std::stop_token stop_token;
    std::pmr::memory_resource* frame_allocator = nullptr;
};
```

The launch site sets it once; all downstream coroutines inherit it
without parameter-list pollution:

```cpp
io_context ioc;
thread_pool tp;
run_async( ioc.get_executor(), tp.get_executor() )( my_task() );

// deep inside:
auto ex = co_await this_coro::compute_executor;
co_await run( ex )( bcrypt() );
```

**Arguments for:**

1. Solves propagation without polluting every intermediate function
   signature.
2. The user still declares and controls the thread pool - no global.
3. More structured than a global: the executor's lifetime is scoped
   to the coroutine tree rooted at the launch site.

**Arguments against:**

1. Sets a precedent for adding cross-cutting concerns to `io_env`.
   If we add a compute executor, why not a logger? An allocator?
   A TLS context? Each addition increases the overhead of every
   `run` and `run_async` call.
2. The `IoAwaitable` concept must grow to accommodate each new
   `io_env` member.
3. It is unclear what the default should be when the user does not
   set the compute executor (see below).

### Position D: Extensible `io_env` via Type-Erased Storage

Add a general-purpose extensibility mechanism rather than named fields:

```cpp
struct io_env {
    executor_ref executor;
    std::stop_token stop_token;
    std::pmr::memory_resource* frame_allocator = nullptr;
    polystore dynamic_objects;   // user-defined cross-cutting concerns
};
```

**Arguments for:**

1. Avoids privileging any single cross-cutting concern over another.
2. Users can propagate whatever they need (thread pool, logger, TLS
   context) without modifying the standard type.

**Arguments against:**

1. Untyped bags of state are difficult to reason about and document.
2. The standard should not need this level of generality for the
   narrow set of things that would otherwise be globals.
3. Only things that would be *harmful* globals deserve propagation
   support; that set is small enough to enumerate.

## The Default Value Problem

If the compute executor lives in `io_env`, what happens when the user
does not set it?

| Option              | Behavior                                              | Trade-off                                       |
| ------------------- | ----------------------------------------------------- | ----------------------------------------------- |
| Lazy global pool    | A library-managed thread pool is created on first use | Convenient, but reintroduces the global problem  |
| Terminate / abort   | Program terminates if the executor is used unset      | Safe but hostile to beginners                    |
| Throw               | Throws an exception if the executor is used unset     | Discoverable, but exceptions in executors are awkward |
| Compile-time error  | Not feasible with type-erased executors               | -                                                |

There is no consensus on the right default.

## Areas of Agreement

1. **User control is paramount.** In any serious program, the program
   author - not library code - must control the thread pool's
   properties.
2. **Libraries must not silently post to globals.** If a global
   executor exists, library authors will use it, and users will lose
   control of scheduling policy.
3. **Propagation is a real problem.** Threading a compute executor
   through every function signature is burdensome and does not scale
   to library code.
4. **The `io_context` is different from a thread pool.** The
   `io_context` holds the reactor and has configuration-sensitive
   properties (single-threaded, `io_uring`-backed, etc.) that
   justify per-user declaration. A generic compute pool has more
   relaxed requirements.
5. **The set of things needing propagation is small.** Beyond the
   compute executor, perhaps only a logger would qualify, and that
   is a separate standardization effort.

## Areas of Partial Agreement

1. **A global is bad for the standard but acceptable for a library.**
   One position holds that a global system executor can live in a
   non-standard library (where it can be removed after experience
   proves it harmful) but must not enter the standard (where removal
   is effectively impossible).
2. **`io_env` propagation is the least-bad option** *if* we accept
   that explicit parameter threading does not scale. Both sides
   acknowledge this is more principled than a global, though one
   side worries about `io_env` scope creep.
3. **Practical servers almost always need a compute pool.** This
   weakens the "propagated by all, used by none" objection but
   does not eliminate it.

## Areas of Disagreement

1. **Whether a global system executor has ever been a net positive.**
   One position holds it is empirically always wrong. The other
   holds that, like `operator new`, it serves as a useful default
   that serious programs override.
2. **Whether the standard should ship both a global and a
   propagation mechanism simultaneously.** One position says ship
   both but document the global as the convenience fallback. The
   other says the global's existence will undermine the propagation
   mechanism because developers will always take the easier path.
3. **Whether adding a second executor to `io_env` is acceptable
   scope.** One position views it as the principled solution. The
   other views it as the first step on a slippery slope of
   ever-growing coroutine environment state.

## Summary of Alternatives

| #   | Approach                        | Global? | Propagation  | `io_env` growth | User burden          |
| --- | ------------------------------- | ------- | ------------ | --------------- | -------------------- |
| A   | Global system executor          | Yes     | N/A          | None            | Minimal              |
| B   | Explicit parameter threading    | No      | Manual       | None            | High (every call)    |
| C   | Named slot in `io_env`          | No      | Automatic    | +1 field        | Low (at launch site) |
| D   | Extensible `io_env` (polystore) | No      | Automatic    | Unbounded       | Low (at launch site) |
| A+C | Global + `io_env` slot          | Yes     | Automatic    | +1 field        | Minimal              |

The design decision hinges on which cost the committee finds least
acceptable: the existence of a global singleton that cannot be removed,
the ergonomic burden of manual propagation, or the precedent of
expanding `io_env` with cross-cutting concerns.
