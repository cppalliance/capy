# Adding `transfer_to` to the Executor Concept

## The Problem

When a coroutine crosses an executor boundary via `run`, the strand on either side of the boundary can get trapped. The bug appears in two directions:

**Case 1: caller on strand, target is a different executor.**

1. A coroutine is running on a strand
2. It does `co_await run(ex)(f())` where `ex` is not the strand
3. During `f()`, the strand still thinks it is running - no other queued coroutine can make progress

**Case 2: caller on io_context, target is a strand.**

1. A coroutine is running on an io_context
2. It does `co_await run(strand)(f())`
3. `f()` runs inside the strand's invoker. When `f()` completes, the trampoline dispatches the parent back to the io_context. The io_context inlines (same thread), so the parent runs inside the strand's `safe_resume` call. The strand is held until the parent suspends.

Both cases have the same root cause: `dispatch` can inline across an executor boundary, and the symmetric transfer chain runs the entire sequence without returning to the strand's invoker loop.

## Root Cause

The call site is `safe_resume(h)` at `strand_queue.hpp:269`, inside `dispatch_batch`. Each queued item is wrapped in a `strand_op` coroutine whose body calls `safe_resume(target)` at line 132. `safe_resume` calls `h.resume()`, which runs the coroutine until something in the chain returns `void` or `noop_coroutine()` from `await_suspend`. Symmetric transfer does not unwind - it tail-jumps through coroutine handles without returning to the caller.

**Case 1 trace** (caller on strand, target is io_context):

```
dispatch_batch: safe_resume(wrapper_h)              <- line 269
  -> wrapper: safe_resume(caller_coro)              <- line 132
    -> caller does co_await run(io_ctx)(f())
    -> await_suspend: io_ctx.dispatch(task_cont)
    -> io_ctx INLINES, returns task_cont.h
    -> symmetric-transfer to f()
    -> f() runs to completion
    -> f()'s final_suspend symmetric-transfers to trampoline
    -> trampoline: caller_ex.dispatch(parent)
    -> caller_ex is strand, running_in_this_thread() is true, INLINES
    -> symmetric-transfer to parent
    -> parent runs                                  <- still inside safe_resume
    -> parent suspends
  <- safe_resume returns
```

**Case 2 trace** (caller on io_context, target is strand):

```
dispatch_batch: safe_resume(wrapper_h)              <- line 269
  -> wrapper: safe_resume(inner_task)               <- line 132
    -> f() runs on the strand, completes
    -> f()'s final_suspend symmetric-transfers to trampoline
    -> trampoline: caller_ex.dispatch(parent)
    -> caller_ex is io_context, INLINES, returns parent.h
    -> symmetric-transfer to parent
    -> parent runs                                  <- still inside safe_resume
    -> parent suspends
  <- safe_resume returns
```

In both cases, the strand's invoker loop does not get control back until the parent suspends. The strand is held for the duration of the inner task, the trampoline, and the parent's resumed execution.

## The Fix: `transfer_to`

The problem is that `dispatch` does not give the source executor a chance to clean up before handing off to the target. We need a third verb on the Executor concept.

The three executor verbs become:

- `std::coroutine_handle<> dispatch(continuation& c)` - Run `c` on this executor. If already on the right thread, return `c.h` for symmetric transfer. Otherwise queue `c` and return `noop_coroutine()`.

- `void post(continuation& c)` - Queue `c` on this executor. Never run inline.

- `std::coroutine_handle<> transfer_to(executor_ref target, continuation& c)` - This executor is releasing `c`. Do whatever is needed to let go, then get `c` running on `target`. Non-serializing executors forward to `target.dispatch(c)`. Serializing executors (strands) post to `target` so the current dispatch batch can finish and the serialization frame can close normally.

`transfer_to` is called on the source executor - the one being left - because the source is the one that knows whether it needs to break the symmetric transfer chain. The target is always a valid (non-null) `executor_ref`.

## Concept Change

`executor.hpp` requires clause gains:

```cpp
{ ce.transfer_to(executor_ref{}, c) } -> std::same_as<std::coroutine_handle<>>;
```

## Type Erasure

The vtable in `executor_ref.hpp` gains a slot:

```cpp
std::coroutine_handle<> (*transfer_to)(void const*, executor_ref, continuation&);
```

`executor_ref` gains a forwarding method:

```cpp
std::coroutine_handle<> transfer_to(executor_ref target, continuation& c) const
{
    return vt_->transfer_to(ex_, target, c);
}
```

The `vtable_for<Ex>` template gains a corresponding lambda:

```cpp
[](void const* p, executor_ref target, continuation& c) -> std::coroutine_handle<> {
    return static_cast<Ex const*>(p)->transfer_to(target, c);
},
```

## Per-Executor Implementation

### thread_pool::executor_type

The thread pool has no serialization state. `transfer_to` just forwards to the target:

```cpp
std::coroutine_handle<>
transfer_to(executor_ref target, continuation& c) const
{
    return target.dispatch(c);
}
```

The thread pool was never affected by the strand escape bug. Its `dispatch` already calls `post` and returns `noop_coroutine()`, so the symmetric transfer chain always breaks at the thread pool boundary.

### strand

The strand is why `transfer_to` exists. Its implementation posts to the target instead of dispatching:

```cpp
// strand.hpp
std::coroutine_handle<>
transfer_to(executor_ref target, continuation& c) const
{
    return detail::strand_service::transfer_to(
        *impl_, executor_ref(ex_), target, c);
}
```

```cpp
// strand_service.cpp
std::coroutine_handle<>
strand_service::transfer_to(
    strand_impl& impl, executor_ref inner_ex,
    executor_ref target, continuation& c)
{
    target.post(c);
    return std::noop_coroutine();
}
```

This is deliberately minimal. The strand does not touch `dispatch_thread_` or `locked_`. It does not need to. Here is why:

When `transfer_to` is called, we are inside the invoker's `dispatch_pending` call, deep in a `.resume()` on a batch item. The invoker loop looks like this:

```cpp
for(;;)
{
    set_dispatch_thread(*p);
    dispatch_pending(*p);      // we are here
    if(try_unlock(*p))
    {
        clear_dispatch_thread(*p);
        co_return;
    }
}
```

`target.post(c)` queues the inner task on the target executor. Returning `noop_coroutine()` causes the coroutine to suspend, so `.resume()` returns and `dispatch_batch` moves to the next item. The batch finishes. The invoker loop reaches `try_unlock`, which either unlocks the strand (if the queue is empty) or loops to drain more work. Either way, the strand releases through its normal path.

The inner task `f()` runs concurrently on the target executor. When it finishes, the trampoline dispatches the parent back to the strand through the normal enqueue path.

An earlier version of this document proposed calling `clear_dispatch_thread` and `try_unlock` from inside `transfer_to`. That is wrong. Calling `try_unlock` mid-batch can set `locked_ = false` while the invoker is still processing items. If new work arrives and triggers a second invoker, two invokers run concurrently and the strand's serialization invariant breaks. The invoker loop is the only safe place to manipulate `locked_` and `dispatch_thread_`.

### Asio-style io_context bridges

User-written executor adapters (like `asio_executor` in the examples) have no serialization state. They get the same trivial implementation as thread_pool:

```cpp
std::coroutine_handle<>
transfer_to(executor_ref target, continuation& c) const
{
    return target.dispatch(c);
}
```

Any user-defined executor that implements its own serialization (an actor, an ordered queue, a custom strand-like primitive) should follow the strand pattern: post to the target instead of dispatching, so the current serialization frame can close normally.

## Trampoline Change

Both the forward trip and the return trip need `transfer_to`. The trampoline must store both executors:

```cpp
struct promise_type
{
    executor_ref caller_ex_;
    executor_ref target_ex_;   // NEW
    continuation parent_;
};
```

**Forward trip** (`run_awaitable_ex::await_suspend`):

Currently:

```cpp
task_cont_.h = h;
return ex_.dispatch(task_cont_);
```

Becomes:

```cpp
task_cont_.h = h;
return caller_env->executor.transfer_to(ex_, task_cont_);
```

This fixes Case 1. If the caller is on a strand, the strand posts to the target and returns `noop_coroutine()`. The coroutine suspends, the strand's batch finishes, and the invoker loop releases the strand normally.

**Return trip** (trampoline `final_suspend`):

Currently:

```cpp
return detail::symmetric_transfer(
    p_->caller_ex_.dispatch(p_->parent_));
```

Becomes:

```cpp
return detail::symmetric_transfer(
    p_->target_ex_.transfer_to(p_->caller_ex_, p_->parent_));
```

This fixes Case 2. If the target is a strand, the strand posts the parent to the caller's executor and returns `noop_coroutine()`. The trampoline suspends, control returns to the strand's `safe_resume` call, and the invoker loop proceeds to drain and unlock.

If neither the caller nor the target is a strand, both `transfer_to` calls forward to `target.dispatch(c)`, preserving the inline fast path.

## Alternative: Guard Object Instead of `transfer_to`

The `transfer_to` design requires every launch function author to call `transfer_to` on both trips. If someone writes a custom `run`-like function and forgets the return trip, they reintroduce the bug. An RAII guard in the coroutine frame could make the fix automatic. Three approaches:

### Option A: Flag in io_env

The strand sets a flag in the `io_env` when it dispatches a coroutine. The trampoline checks the flag on both trips and posts instead of dispatching when it is set.

`io_env` gains a bool:

```cpp
struct io_env
{
    executor_ref executor;
    std::stop_token stop_token;
    std::pmr::memory_resource* frame_allocator = nullptr;
    bool serialized = false;    // NEW
};
```

The strand's `dispatch` sets `serialized = true` in the env before returning the handle. The trampoline reads it:

```cpp
// forward trip (await_suspend)
if(caller_env->serialized)
{
    ex_.post(task_cont_);
    return std::noop_coroutine();
}
return ex_.dispatch(task_cont_);

// return trip (final_suspend)
if(/* target env was serialized */)
{
    caller_ex_.post(parent_);
    return std::noop_coroutine();
}
return caller_ex_.dispatch(parent_);
```

**For:** Zero-cost for non-strand cases (one branch on a bool). No changes to the Executor concept. The strand marks it, the trampoline reads it, the user never touches it.

**Against:** The env is `const` from the awaitable's perspective - the strand would need to set the flag before the env reaches the awaitable, which means the flag lives in the env owned by the `run` awaitable, not the caller's env. On the return trip, the trampoline needs to know whether the *target's* env was serialized, but the trampoline only stores the caller's executor, not the target's env. Plumbing the target's serialization flag to the trampoline adds complexity similar to storing `target_ex_`. The env also flows through the entire coroutine chain, so the flag would affect nested `run` calls - a coroutine on a strand that calls `run(strand2)(f())` inside `run(pool)(g())` would see `serialized = true` from the outer strand even though the inner context is a pool.

### Option B: TLS set by the strand invoker

The strand's invoker loop sets a TLS variable before calling `dispatch_pending` and clears it after. Any executor's `dispatch` checks this TLS variable and posts instead of inlining when it is set.

```cpp
// strand invoker loop
inline thread_local strand_impl* current_strand = nullptr;

static strand_invoker make_invoker(strand_impl& impl)
{
    strand_impl* p = &impl;
    for(;;)
    {
        set_dispatch_thread(*p);
        current_strand = p;
        dispatch_pending(*p);
        current_strand = nullptr;
        if(try_unlock(*p))
        {
            clear_dispatch_thread(*p);
            co_return;
        }
    }
}
```

Every executor's `dispatch` checks:

```cpp
std::coroutine_handle<> dispatch(continuation& c) const
{
    if(detail::current_strand)
    {
        post(c);
        return std::noop_coroutine();
    }
    // normal dispatch logic
}
```

**For:** Fully automatic. No changes to `run`, trampolines, or any launch function. Every executor boundary crossing inside a strand batch posts instead of inlining. Covers both trips, all launch functions, and user-defined launch functions that don't know about `transfer_to`.

**Against:** Every executor's `dispatch` pays a TLS read on every call, even when no strand is involved. Invasive - every concrete executor and `executor_ref::dispatch` must add the check. Nested strands need save/restore (strand B's invoker would clear the TLS set by strand A's invoker). The TLS approach also prevents legitimate inlining within a strand - if a coroutine on a strand dispatches more work to the same strand, it should inline (that is what `running_in_this_thread()` enables), but the TLS check would force it to post. Distinguishing "dispatching to a foreign executor" from "dispatching to the same strand" requires comparing the current strand pointer, adding more logic to every dispatch call.

### Option C: Continuation wrapper

The strand wraps every dispatched continuation in a guard that intercepts the symmetric transfer chain. When the coroutine suspends and `await_suspend` returns a handle that would leave the strand, the guard detects this and posts instead.

**For:** In theory, fully automatic and encapsulated in the strand.

**Against:** Not implementable with the current coroutine model. Symmetric transfer happens inside the C++ runtime - `await_suspend` returns a `std::coroutine_handle<>` and the runtime tail-calls it. There is no interception point between the return from `await_suspend` and the resumption of the target handle. The strand cannot inspect or redirect the handle after `await_suspend` returns it. The `strand_op` wrapper already wraps the target in a coroutine, but `safe_resume(target)` follows the entire symmetric transfer chain before returning - the wrapper only gets control back after the chain ends, which is too late.

### Recommendation

Option B (TLS) is the most automatic but too invasive and has the wrong default for same-strand dispatch. Option C is not implementable. Option A (env flag) has the right shape but the plumbing to get the flag to both trips is roughly as complex as storing `target_ex_` in the trampoline.

`transfer_to` on the Executor concept remains the cleanest design. The cost is that launch function authors must call it on both trips. Since launch functions are library machinery (not user code), and capy ships the primary one (`run`), this is an acceptable constraint. The alternative is to provide `transfer_to` on the concept AND use it automatically inside `run`'s trampoline, so users who write `co_await run(ex)(f())` never think about it. Custom launch functions that want the same correctness call `transfer_to`; those that don't care about strands can use plain `dispatch`.

## Test Impact

`testRunExStrandFirstInstruction` verifies that `running_in_this_thread()` is true inside an inner task passed to `run(strand)`. With `transfer_to` on the forward trip, the caller's executor (pool) calls `transfer_to` which forwards to `strand.dispatch(c)`. The strand is not running on this thread, so it enqueues and posts an invoker. The inner task runs inside the invoker where `running_in_this_thread()` is true. The test should still pass.

Two new tests are needed:

- **Case 1 regression test:** Two coroutines on the same strand. One does `co_await run(pool_ex)(slow_task())`. The second coroutine should make progress while `slow_task` runs on the pool.

- **Case 2 regression test:** A coroutine on an io_context does `co_await run(strand)(f())`. After `f()` completes and the parent resumes, verify the strand is free (not held by the parent's execution).
