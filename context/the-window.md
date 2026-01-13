Yes! There IS a window. Let me trace it:

---

## The Window

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

**The window is: "while the parent coroutine body is executing".**

If `parent` sets TLS when it resumes and `child()` is called during that execution, `child`'s `operator new` sees the TLS.

---

## Where to Set TLS

The cleanest hook is **`await_resume`** — called right before the coroutine body continues:

```cpp
auto initial_suspend() noexcept {
    struct awaiter {
        promise_type* p_;
        bool await_ready() { return false; }
        void await_suspend(coro) {}
        void await_resume() { 
            tls::current_allocator = &p_->alloc_;  // Set when body starts
        }
    };
    return awaiter{this};
}
```

Every time the coroutine resumes (after any `co_await`), it sets TLS to its allocator. When `child()` is called, TLS is already pointing to `parent`'s allocator.

---

## The Flow

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
child resumes → TLS = child.alloc (same value, inherited)
    ↓
child calls grandchild() → grandchild uses TLS
```

---

## Why It's Safe

- TLS is only read in `operator new`
- TLS is set by the currently-running coroutine
- Single-threaded: only one coroutine runs at a time per thread
- No dangling: the coroutine that set TLS is still on the stack when `operator new` reads it

**Very narrow, deterministic window.** Clean.