# Execution Model: post, dispatch, and defer

Research notes on executor semantics for capy, incorporating material from
N4242 (Executors and Asynchronous Operations) and N4482 (Executor Semantics).

## The Minimal Executor Concept

After discussion with Peter Dimov, we concluded the minimal foundational executor API is:

```cpp
struct executor
{
    template<class NullaryCallable>
    void post(NullaryCallable&&);

    template<class NullaryCallable>
    void dispatch(NullaryCallable&&);
};
```

This is the smallest API upon which everything—including senders—can be built.

## Progress Guarantees

From N4482, executors provide a **parallel progress guarantee**: submitted work
will eventually execute, but only after its first step begins. This is weaker
than a concurrent guarantee (where work makes progress independently).

| Guarantee | Meaning | Example |
|-----------|---------|---------|
| **Parallel** | Work runs eventually, shares threads | Thread pool, io_context |
| **Concurrent** | Work makes independent progress | new_thread_executor |
| **Weakly parallel** | May need external nudging | Resumable functions on executors |

Coroutines built on parallel executors get weakly parallel progress—the
executor provides the parallel guarantee, but the coroutine itself may suspend
indefinitely waiting for events.

## Semantic Definitions

### post

**Never blocks the caller pending completion of f.**

From N4242 §15.9.1:
> The executor shall not invoke f in the current thread of execution prior to
> returning from post.

- Guaranteed the callable does NOT run before `post()` returns
- Never runs inline, always goes through the queue
- Use for: new independent work

### dispatch

**Permitted to block the caller until f finishes.**

From N4242 §15.9.1:
> The executor may invoke f in the current thread of execution prior to
> returning from dispatch.

- If called from within the executor's context → execute immediately
- If called from outside → enqueue like `post`
- May block until completion if running inline
- Use for: continuations (resuming suspended work)

### defer

**Like post, but hints continuation relationship.**

From N4242 §15.9.1:
> defer is used to convey the intention of the caller that the submitted
> function is a continuation of the current call context. The executor may
> use this information to optimize or otherwise adjust the way in which f
> is invoked.

- Always enqueues (never inline)
- Implementation may defer beyond current handler invocation
- Enables thread-local queue optimization (see below)
- Use for: yielding to other queued work, fairness

## Why Both post and dispatch?

### Correctness, Not Just Optimization

From N4482 §3.1.1:
> A consequence of dispatch()'s specification is that its use can introduce
> deadlock. This occurs when the caller holds a mutex and f attempts to
> acquire the same mutex. Thus the choice between dispatch() and post()
> impacts **program correctness**. A hint, which by definition need not be
> respected, is an inappropriate way for the caller to express its intention.

The distinction is **not a hint**—it affects whether your program deadlocks.

### post alone is insufficient

With only `post`, every continuation bounces through a queue even when already on the correct executor:

```cpp
// Already running on executor X
async_op.then([](auto result) {
    // With post-only: queued, context switch, runs later
    // With dispatch: runs inline, zero overhead
});
```

### dispatch expresses continuation semantics

When an async operation completes, the handler is a *continuation* of the current logical operation. The completing operation knows:

- "I'm finishing on context X"
- "The handler wants to run on context Y"  
- If X == Y → run it now (dispatch)
- If X ≠ Y → post to Y

This is the essential optimization that makes coroutines competitive with hand-written state machines.

## The defer Optimization

From N4242 §9, consider a chain of async reads where each completes immediately
(data already in kernel buffers):

**With post (naïve):**
```
#6  — lock mutex
#7  — dequeue read_loop
#8  — unlock mutex
#9  — call read_loop
#1  — call post
#2  — lock mutex
#3  — enqueue read_loop
#4  — notify condition
#5  — unlock mutex
(repeat)
```

Each cycle requires **two lock/unlock pairs** plus a condition variable notification.

**With defer (optimized):**
```
#3 — lock mutex
#4 — flush thread-local queue to main queue
#5 — dequeue read_loop
#6 — unlock mutex
#7 — call read_loop
#1 — call defer
#2 — enqueue to thread-local queue (no lock!)
(repeat)
```

By using a thread-local queue, defer eliminates one lock/unlock pair and avoids
unnecessary thread wakeups. On modern hardware: ~15ns for uncontended lock vs
~2ns for thread-local access.

## Formal Executor Requirements

From N4242 §15.9.1, an executor type X must provide:

| Expression | Semantics |
|------------|-----------|
| `x.dispatch(f, a)` | May invoke f in current thread prior to returning |
| `x.post(f, a)` | Shall not invoke f in current thread prior to returning |
| `x.defer(f, a)` | Same as post, but hints f is a continuation |
| `x.on_work_started()` | Increment outstanding work count |
| `x.on_work_finished()` | Decrement outstanding work count |
| `x.context()` | Return reference to execution_context |

The allocator parameter `a` allows the executor to use caller-provided memory
for storing the queued function object.

## Coroutine Affinity Example

Consider a coroutine chain with affinity:

```cpp
task<void> c() { co_return; }
task<void> b() { co_await c(); }
task<void> a() { co_await b(); }

spawn(ex, a().on(ex), handler);  // a has affinity, b and c inherit
```

### Transition Analysis

| Transition | Type | Recommended | Rationale |
|------------|------|-------------|-----------|
| a → b (starting b) | Starting child | Symmetric transfer | Initiating new work, already on `ex` |
| b → c (starting c) | Starting child | Symmetric transfer | Same—continuing the chain |
| c → b (c completes) | Continuation | **dispatch** | Resuming suspended parent |
| b → a (b completes) | Continuation | **dispatch** | Resuming suspended parent |

### Execution Trace with Async I/O

```
1. Thread A (ex): a runs, awaits b → symmetric transfer
2. Thread A (ex): b runs, awaits c → symmetric transfer
3. Thread A (ex): c runs, starts async I/O, suspends
4. Thread B (I/O): I/O completes, resumes c
5. Thread B (I/O): c's final_suspend — NOT on ex!
   → dispatch: posts continuation to ex
6. Thread A (ex): b resumes from queue
7. Thread A (ex): b's final_suspend — already on ex
   → dispatch: runs inline (no queue bounce)
8. Thread A (ex): a resumes immediately
```

With **dispatch**: Steps 5 posts (wrong context), step 7 runs inline (right context).

With **post everywhere**: Step 7 would unnecessarily queue.

With **symmetric transfer everywhere**: Step 5 would violate affinity.

## capy::task Should Always Dispatch

For `capy::task`, all uses of the dispatcher are resuming suspended coroutines:

| Site | What's Happening | Nature |
|------|------------------|--------|
| `final_suspend` | Child done, resume parent | Continuation |
| Async op completes | Resume awaiting task | Continuation |
| `spawn` completion | Task done, call handler | Continuation |

There's no case where task submits new independent work through the dispatcher. Therefore, the `executor_dispatcher` should use dispatch:

```cpp
struct executor_dispatcher
{
    executor ex_;

    template<class F>
    void operator()(F&& f) const
    {
        if (ex_)
            ex_.dispatch(std::forward<F>(f));  // continuation semantics
        else
            std::forward<F>(f)();
    }
};
```

## When to Use post from await_suspend

From the perspective of an async operation's completion path:

### Use dispatch (default)

- Normal async completion
- Efficient inline execution when on correct context
- The common case

### Use post when:

1. **Synchronous completion safety**: Operation might complete before initiation returns
   ```cpp
   start_async_read([h, &d](Data result) {
       // Might be called BEFORE start_async_read returns!
       d.post(h);  // Safe: never inline
   });
   ```

2. **Reentrancy**: Completing while holding locks
   ```cpp
   void complete_all_waiters() {
       std::lock_guard lock(mutex_);
       for (auto& w : waiters_) {
           w.dispatcher.post(w.handle);  // User code runs after lock released
       }
   }
   ```

3. **Stack depth**: Batch-completing many operations
   ```cpp
   for (auto& op : completed_ops) {
       op.dispatcher.post(op.handle);  // Constant stack depth
   }
   ```

4. **Fairness**: Yielding to other queued work after heavy computation

### Decision Framework

| Situation | Use |
|-----------|-----|
| Normal async completion | dispatch |
| Might complete synchronously | post |
| Completing while holding locks | post |
| Batch-completing many ops | post |
| Want to yield/be fair | post or defer |

## Relationship to Senders (P2300)

The sender model's `schedule()` + `start()` decomposes into these primitives:

```cpp
auto sender = schedule(ex) | then([]{ /* work */ });
```

When started, the scheduler internally uses:
- `dispatch` if already on correct context (inline optimization)
- `post` if not (deferred execution)

The post/dispatch model is foundational—senders are built on top of it.

## References

- N4242: Executors and Asynchronous Operations, Revision 1 (2014)
- N4482: Some notes on executors and the Networking Library Proposal (2015)
- Boost.Asio executor model: https://www.boost.org/doc/libs/release/doc/html/boost_asio/overview/model/executors.html
- P2300: std::execution: https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2024/p2300r10.html

## Summary

- **post** = "here's new independent work" (always queued, never blocks caller)
- **dispatch** = "here's the next step of what I'm doing" (inline if same context, may block)
- **defer** = "queue this continuation" (enables thread-local optimization)

The choice between dispatch and post is **correctness**, not optimization.
For coroutine continuations, dispatch is correct. Post is the escape hatch
when you need guaranteed deferred execution.
