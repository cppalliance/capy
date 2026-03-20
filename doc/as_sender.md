# Zero-Allocation Sender-to-Awaitable Bridge

Every IoAwaitable ever written - timers, mutexes, channels, semaphores,
file I/O, sockets, database queries, HTTP clients - is now consumable
by a sender pipeline. No coroutine frame. No heap allocation. Zero cost.

## What This Is

`as_sender` wraps any IoAwaitable in a P2300-compliant sender. A
receiver attaches to the sender through `connect`. When `start` is
called, the operation state drives the awaitable protocol directly -
`await_ready`, `await_suspend`, `await_resume` - without ever creating
a coroutine.

The awaitable does not know it is talking to a sender. It sees a
`coroutine_handle<>` and an `io_env*`, exactly as it would from a
coroutine. The awaitable's code does not change. Not one line.

```cpp
// Wrap any IoAwaitable as a sender
auto sndr = as_sender(stream.read_some(buf));

// Attach a receiver and start the operation
auto op = connect(std::move(sndr), my_receiver);
start(op);
```

## How It Works

The bridge rests on a single observation: all three major compilers
(MSVC, GCC, Clang) lay out a coroutine frame with two function
pointers at the front:

```cpp
struct coroutine_frame {
    void (*resume)(coroutine_frame*);
    void (*destroy)(coroutine_frame*);
    // ... promise, locals, state ...
};
```

When you call `handle.resume()`, the compiler calls the function
pointer at offset zero. That is all it does.

The bridge defines a lightweight struct that matches this layout:

```cpp
struct frame_cb {
    void (*resume)(frame_cb*);
    void (*destroy)(frame_cb*);
    void* data;
};
```

Three pointers. Twenty-four bytes on a 64-bit platform. The `resume`
pointer holds the sender's completion callback. The `destroy` pointer
is a no-op - the sender owns its own lifetime. The `data` pointer
points back to the operation state.

`std::coroutine_handle<>::from_address(&cb)` produces a handle that,
when `.resume()` is called, invokes the function pointer at offset
zero - which is our callback. The awaitable receives this handle. It
cannot tell the difference. It does not need to.

## The Flow

Here is what happens, step by step:

- **`as_sender(awaitable)`** stores the awaitable inside a sender.
  Nothing runs yet. Senders are lazy.

- **`connect(sender, receiver)`** produces an operation state. The
  operation state holds the awaitable, the receiver, an `io_env`, and
  a `frame_cb`. Everything lives on the operation state. No allocation.

- **`start(op_state)`** begins the operation:

  1. The executor and stop token are pulled from the receiver's
     environment and stored in the `io_env`.

  2. `await_ready()` is checked. If the awaitable is immediately
     ready, the result is harvested and the receiver is signaled
     inline.

  3. Otherwise, the `frame_cb` is filled in: `resume` points to the
     completion callback, `destroy` is a no-op, `data` points to the
     operation state. A `coroutine_handle<>` is manufactured from the
     `frame_cb`'s address. `await_suspend(handle, &env)` is called on
     the awaitable.

- **The awaitable runs.** It submits work to the reactor - a timer
  fires, bytes arrive on a socket, a mutex unlocks. When the operation
  completes, the reactor calls `executor.post(handle)` or
  `executor.dispatch(handle)`.

- **The executor calls `handle.resume()`.** Because the handle points
  at the `frame_cb`, this calls the `resume` function pointer. The
  callback recovers the operation state from `data`, calls
  `await_resume()` to harvest the result, and signals the receiver
  through `set_value`, `set_error`, or `set_stopped`.

The awaitable went through its entire lifecycle - ready check,
suspension, reactor submission, executor resumption, result harvest -
without a coroutine ever existing.

## What This Means

The awaitable ecosystem and the sender ecosystem are no longer
separate worlds. They are one world.

Every IoAwaitable anyone has written becomes a sender with a single
function call. Awaitable authors gain a new consumer base without
modifying a single line of their code. Sender authors gain access to
every I/O primitive the awaitable ecosystem has produced - and will
produce - at zero allocation cost.

- **One I/O implementation.** The library implements each operation
  once as an IoAwaitable. Coroutines `co_await` it. Sender pipelines
  consume it through `as_sender`. Both go through the same reactor,
  the same executor, the same platform code.

- **Zero allocation.** The `frame_cb` lives on the operation state.
  No coroutine frame. No heap allocation. No bridge coroutine. The
  previous implementation allocated a coroutine frame per I/O
  operation just to obtain a `coroutine_handle<>`. That tax is gone.

- **Full protocol fidelity.** The bridge respects `await_ready` for
  synchronous fast-paths. It normalizes `await_suspend` return types
  (`void`, `bool`, `coroutine_handle<>`). It propagates the executor
  and stop token through `io_env`. It routes results to `set_value`,
  errors to `set_error`, and cancellation to `set_stopped`.

- **Transparent to the awaitable.** The awaitable sees a
  `coroutine_handle<>` and an `io_env const*`. It does not know
  whether the handle points at a coroutine frame or a `frame_cb`. It
  does not need to know. The handle is the abstraction boundary, and
  the abstraction holds.

- **Works today.** This is not a proposal. It is shipping code. It
  compiles and passes tests on MSVC, GCC, and Clang. The ABI
  compatibility that makes it work is the same ABI reality documented
  in P3203R0 and relied upon by Boost.Cobalt in production.

## Example

```cpp
namespace capy = boost::capy;
namespace ex = beman::execution;

// A Capy IoAwaitable - a 500ms timer
auto sndr = capy::as_sender(capy::delay(500ms));

// Connect a receiver whose environment carries a Capy executor
auto op = ex::connect(
    std::move(sndr),
    my_receiver{
        {pool.get_executor(), stop_source.get_token()},
        &done});

// Start the operation - no coroutine frame allocated
ex::start(op);
```

The receiver's environment provides the executor and stop token. The
bridge threads them into the `io_env` that the awaitable expects. The
timer fires, the executor resumes the handle, the receiver gets
`set_value()`. Twenty-four bytes of `frame_cb` on the operation state.
That is the entire cost.

Welcome to the awaitable universe. The door is open.
