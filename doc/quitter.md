# `quitter<T>` - Stop-Aware Task Type

## Summary

`quitter<T>` is a task type that satisfies `IoRunnable` and provides
transparent cancellation. When the stop token is triggered, the
coroutine body never sees the cancellation. The promise intercepts
it on resume and short-circuits to `final_suspend`. RAII cleanup
runs normally. The parent sees a "stopped" completion. The coroutine
author writes zero cancellation-handling code.

## Motivation

Ville Voutilainen (the emeritus chair of EWG in WG21) identified a gap in Capy's `task<T>`:
when an application needs to shut down (e.g. Qt "close all windows"),
every in-flight coroutine should exit cleanly without the programmer
writing `if (ec) co_return;` after every `co_await`. The sender model
achieves this via `set_stopped` - the coroutine never resumes. Capy's
`task<T>` resumes with `operation_aborted` and requires explicit
handling.

`quitter<T>` closes this gap. It is a policy-level task type: same
IoAwaitable protocol, same io_env propagation, same frame allocator,
same symmetric transfer - but with automatic stop-on-resume behavior.

## Behavior

### Normal operation (stop not requested)

Identical to `task<T>`. The coroutine runs, co_awaits I/O operations,
returns a value or throws. No observable difference.

### When stop is requested

1. The stop token in `io_env` is triggered (from a parent, a scope,
   or application shutdown).
2. The in-flight I/O operation observes the token and completes
   (typically with `operation_aborted`).
3. The I/O awaitable resumes the coroutine via symmetric transfer.
4. **Before the coroutine body sees the result**, the promise's
   `transform_awaiter::await_resume()` checks the stop token.
5. If `env_->stop_token.stop_requested()` is true, the awaiter
   throws a sentinel exception (`detail::stop_requested_exception`).
6. `unhandled_exception()` catches it and sets a `stopped_` flag
   instead of storing the exception.
7. `final_suspend` returns the continuation via symmetric transfer.
8. The parent's `await_resume()` observes the stopped flag.

### Why throw internally?

Throwing inside the coroutine is the only portable mechanism to
jump from an arbitrary suspension point to `final_suspend` while
running destructors for all in-scope locals. The exception never
escapes the coroutine - `unhandled_exception()` catches it. The
user never sees it. No `exception_ptr` is stored. The cost is one
throw + catch per cancellation, which happens once per coroutine
lifetime (not per operation).

Alternative: a compiler-level `coroutine_handle<>::destroy()` from
outside would also run destructors, but the parent loses the
completion signal. The internal-throw approach preserves structured
completion.

## Interface

### Header

```
<boost/capy/quitter.hpp>
```

### Synopsis

```cpp
namespace boost::capy {

template<typename T = void>
struct [[nodiscard]] quitter
{
    struct promise_type;

    // IoAwaitable
    bool await_ready() const noexcept;
    auto await_resume();
    std::coroutine_handle<> await_suspend(
        std::coroutine_handle<> cont,
        io_env const* env);

    // IoRunnable
    std::coroutine_handle<promise_type> handle() const noexcept;
    void release() noexcept;

    ~quitter();
    quitter(quitter&&) noexcept;
    quitter& operator=(quitter&&) noexcept;
    quitter(quitter const&) = delete;
    quitter& operator=(quitter const&) = delete;
};

} // namespace boost::capy
```

### `promise_type`

```cpp
struct quitter<T>::promise_type
    : io_awaitable_promise_base<promise_type>
    , detail::task_return_base<T>
{
    // Completion state
    enum class completion { running, value, exception, stopped };

    completion state_ = completion::running;
    union { std::exception_ptr ep_; };

    promise_type() noexcept;
    ~promise_type();

    quitter get_return_object();
    auto initial_suspend() noexcept;   // same as task<T>
    auto final_suspend() noexcept;     // same as task<T>

    void unhandled_exception();
    std::exception_ptr exception() const noexcept;
    bool stopped() const noexcept;

    template<class Awaitable>
    auto transform_awaitable(Awaitable&& a);
};
```

## Implementation Details

### `detail::stop_requested_exception`

```cpp
namespace boost::capy::detail {

struct stop_requested_exception {};

} // namespace boost::capy::detail
```

A lightweight sentinel type. Not derived from `std::exception`.
Never escapes the coroutine. Never stored in `exception_ptr`.

### `promise_type::unhandled_exception()`

```cpp
void unhandled_exception()
{
    try {
        throw;
    }
    catch (detail::stop_requested_exception const&) {
        state_ = completion::stopped;
    }
    catch (...) {
        new (&ep_) std::exception_ptr(std::current_exception());
        state_ = completion::exception;
    }
}
```

### `promise_type::exception()` and `stopped()`

```cpp
std::exception_ptr exception() const noexcept
{
    if (state_ == completion::exception)
        return ep_;
    return {};
}

bool stopped() const noexcept
{
    return state_ == completion::stopped;
}
```

### `transform_awaiter` (the key difference from `task<T>`)

```cpp
template<class Awaitable>
struct transform_awaiter
{
    std::decay_t<Awaitable> a_;
    promise_type* p_;

    bool await_ready() noexcept
    {
        return a_.await_ready();
    }

    template<class Promise>
    auto await_suspend(std::coroutine_handle<Promise> h) noexcept
    {
        using R = decltype(
            a_.await_suspend(h, p_->environment()));
        if constexpr (std::is_same_v<
            R, std::coroutine_handle<>>)
            return detail::symmetric_transfer(
                a_.await_suspend(h, p_->environment()));
        else
            return a_.await_suspend(
                h, p_->environment());
    }

    decltype(auto) await_resume()
    {
        set_current_frame_allocator(
            p_->environment()->frame_allocator);

        // THE KEY DIFFERENCE: check stop token before
        // returning to the coroutine body
        if (p_->environment()->stop_token.stop_requested())
            throw detail::stop_requested_exception{};

        return a_.await_resume();
    }
};
```

### `quitter::await_resume()`

```cpp
auto await_resume()
{
    if (h_.promise().stopped())
        throw detail::stop_requested_exception{};
    if (h_.promise().state_ == promise_type::completion::exception)
        std::rethrow_exception(h_.promise().ep_);
    if constexpr (!std::is_void_v<T>)
        return std::move(*h_.promise().result_);
}
```

When a `quitter` is awaited inside another `quitter`, the stopped
state propagates upward: the parent's `await_resume` throws
`stop_requested_exception`, which the parent's `unhandled_exception`
catches and sets `stopped_`. The chain unwinds automatically.

When a `quitter` is awaited inside a regular `task<T>`, the
`stop_requested_exception` becomes an unhandled exception in the
`task`. The `task`'s `await_resume` rethrows it. The caller must
handle it. This is by design: `task<T>` does not opt into automatic
cancellation.

### `quitter::await_suspend()`

Identical to `task<T>::await_suspend()`:

```cpp
std::coroutine_handle<> await_suspend(
    std::coroutine_handle<> cont,
    io_env const* env)
{
    h_.promise().set_continuation(cont);
    h_.promise().set_environment(env);
    return h_;
}
```

### Everything else

`initial_suspend`, `final_suspend`, `get_return_object`,
`return_value`/`return_void`, frame allocation (`operator
new`/`delete`), `handle()`, `release()`, move operations,
destructor - all identical to `task<T>`.

## Concept Satisfaction

`quitter<T>` satisfies:

- `IoAwaitable` - has `await_suspend(coroutine_handle<>, io_env const*)`
- `IoRunnable` - has `promise_type`, `handle()`, `release()`,
  `exception()`, `set_continuation()`, `set_environment()`,
  `result()` (non-void)

`quitter<T>` is usable with `run`, `run_async`, `when_all`,
`when_any`, and any launcher that accepts `IoRunnable`.

## Interaction with `when_all` / `when_any`

`when_all` and `when_any` create a child `stop_source` and register
a `stop_callback` on the parent token. When the parent requests
stop, the child token is triggered. Each child `quitter` observes
the token on its next resume and short-circuits. All children exit
cleanly. The parent combinator sees the stopped completions and
propagates accordingly.

No changes to `when_all` or `when_any` are needed. The stop
propagation already works through `io_env`. The only new behavior
is inside `quitter`'s `transform_awaiter::await_resume()`.

## Usage

### Basic

```cpp
quitter<> session(tcp_socket& sock)
{
    char buf[1024];
    auto [ec, n] = co_await sock.read_some(buf);
    // If stop was requested, we never reach here.
    // RAII cleanup for buf (trivial) and sock (if owned)
    // runs automatically.
    if (ec)
        co_return;
    co_await process(buf, n);
}
```

### Application shutdown

```cpp
quitter<> run_server(tcp_acceptor& acc)
{
    while (true)
    {
        auto [ec, sock] = co_await acc.accept();
        if (ec)
            co_return;
        co_await scope.spawn(session(sock));
    }
    // When the application calls stop_source.request_stop(),
    // the next co_await in this coroutine (or any child)
    // short-circuits. All sessions exit. All sockets close
    // via RAII. The server exits cleanly.
}
```

### Mixing with `task<T>`

```cpp
task<> outer()
{
    // If the quitter is stopped, its await_resume throws
    // stop_requested_exception. The task sees it as an
    // unhandled exception. The task's caller must handle it.
    co_await some_quitter_function();
}
```

This is intentional: `task<T>` does not opt into automatic
cancellation. The programmer who uses `task<T>` has chosen
explicit error handling.

## Test Plan

### Unit tests (in `test/unit/quitter.cpp`)

1. **Normal completion** - `quitter<int>` returns a value.
   Verify `await_resume()` returns the value. Verify
   `stopped()` is false.

2. **Void completion** - `quitter<>` completes normally.

3. **Exception propagation** - `quitter<>` throws. Verify
   `exception()` is non-null. Verify `stopped()` is false.

4. **Stop before first co_await** - Request stop before the
   quitter starts its first I/O operation. Verify the first
   `co_await` short-circuits. Verify `stopped()` is true.
   Verify RAII destructors ran (use a counting guard).

5. **Stop during I/O** - Start an I/O operation, request stop
   while it is in flight. Verify the coroutine does not resume
   into the body. Verify `stopped()` is true. Verify RAII
   destructors ran.

6. **Stop propagation through chain** - `quitter` awaits
   `quitter` awaits `quitter`. Request stop. Verify all three
   report `stopped()`. Verify all RAII destructors ran.

7. **Stop propagation with when_all** - `when_all` of two
   `quitter` tasks. Request stop. Verify both stop. Verify
   the `when_all` completes (does not hang).

8. **Stop propagation with when_any** - `when_any` of two
   `quitter` tasks. One completes normally. Verify the other
   is stopped.

9. **Mixing quitter and task** - `task<>` awaits `quitter<>`.
   Request stop. Verify the `quitter` stops. Verify the
   `task` sees `stop_requested_exception` as an unhandled
   exception.

10. **No stop requested** - Run a `quitter` to completion
    without ever requesting stop. Verify identical behavior
    to `task<T>`.

11. **RAII verification** - Use a struct with a destructor
    that increments a counter. Declare it in the coroutine
    body before a `co_await`. Request stop. Verify the
    counter was incremented (destructor ran).

12. **Multiple co_await** - `quitter` does three sequential
    I/O operations. Request stop after the second completes.
    Verify the third `co_await` short-circuits. Verify the
    first two results were processed normally.

### Integration tests

13. **TCP echo with shutdown** - Start a TCP echo server
    using `quitter`. Connect a client. Exchange data. Request
    stop. Verify the server exits cleanly and the socket is
    closed.

14. **Timer cancellation** - `quitter` awaits a long timer.
    Request stop. Verify the timer is cancelled and the
    coroutine exits promptly.

## Files to Create

| File | Contents |
|------|----------|
| `include/boost/capy/quitter.hpp` | `quitter<T>` class template |
| `include/boost/capy/detail/stop_requested_exception.hpp` | Sentinel exception type |
| `test/unit/quitter.cpp` | Unit tests |

## Files to Modify

| File | Change |
|------|--------|
| `include/boost/capy.hpp` | Add `#include <boost/capy/quitter.hpp>` |
| `test/unit/CMakeLists.txt` | Add `quitter.cpp` to test sources |

## Design Decisions

**Why a separate type instead of a flag on `task<T>`?**

The task type is a policy point. Users who want explicit error
handling use `task<T>`. Users who want transparent cancellation
use `quitter<T>`. The IoAwaitable protocol does not mandate a
task. This is the design P4003 enables.

**Why throw internally instead of `goto final_suspend`?**

C++ coroutines have no mechanism to jump from an arbitrary
suspension point to `final_suspend` other than throwing. The
throw runs destructors for all in-scope locals. The exception
is caught in `unhandled_exception()` and never escapes. The
cost is one throw per cancellation per coroutine lifetime.

**Why check in `await_resume` instead of `await_suspend`?**

`await_suspend` has already committed to suspending. The I/O
operation is in flight. Checking the stop token in `await_suspend`
would race with the I/O completion. Checking in `await_resume`
is safe: the operation has completed, the coroutine is about to
resume, and we intercept before the body sees the result.

**Why not check in `initial_suspend::await_resume`?**

We could, and should. If the stop token is already triggered
when the quitter starts, the first `initial_suspend::await_resume`
should throw `stop_requested_exception` immediately. Add this
check to `initial_suspend`'s awaiter:

```cpp
void await_resume() const noexcept(false)
{
    set_current_frame_allocator(
        p_->environment()->frame_allocator);
    if (p_->environment()->stop_token.stop_requested())
        throw detail::stop_requested_exception{};
}
```

Note: this changes `initial_suspend::await_resume` from `noexcept`
to potentially-throwing. This is safe because `unhandled_exception()`
will catch it.

## Execution Protocol

Save output after each complete semantic unit or to-do item (never
mid-paragraph). Always save output BEFORE marking plan items done -
never the reverse. On resumption: read the plan and last ~30 lines
of the output file. Repair any truncated tail. Continue from where
output ends, matching existing style. Never rewrite prior content.
