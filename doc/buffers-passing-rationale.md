# Design Rationale: Buffer Sequence Passing Convention

## Context

This document captures the design space and trade-offs around how buffer
sequences are passed to I/O operations in capy. The central question is
whether the `ReadStream` and `WriteStream` concepts should mandate that
implementations copy the buffer sequence, or whether they should accept
by reference and leave lifetime management to the caller.

A secondary question is whether the distinction between coroutine-based
and non-coroutine-based implementations (tasks returning `io_task` vs.
raw awaitables) changes the answer.

The discussion took place between Vinnie Falco and Peter Dimov. The
consensus is still being formed; this document records the arguments on
both sides.

## Current State

The capy documentation currently states:

> Buffer sequences should be accepted by value when the member function
> is a coroutine, to ensure the sequence lives in the coroutine frame
> across suspension points.

This statement is acknowledged to be backwards. The discussion produced
the following corrected understanding:

- A function that returns an `IoAwaitable` directly (a raw awaitable,
  not backed by a coroutine frame) must store the buffer sequence inside
  the awaitable, because there is no coroutine frame to hold it. Taking
  by value ensures the sequence lives in the returned object.

- A function that returns an `io_task` (a coroutine) type-erases the
  buffer sequence into its coroutine frame. In this case the caller's
  object is referenced across suspension points through the coroutine
  frame itself, and taking by `const&` is correct - the sequence is not
  a temporary relative to the suspension.

The guidance should read: raw awaitables take by value; coroutine-based
tasks take by `const&`.

## Background

### Two Layers of Lifetime

Buffer sequences have a two-layer lifetime structure:

1. **The sequence object** - the iterator pair or container that
   describes which memory regions to use. This is typically cheap to
   copy; it holds pointers and sizes, not bytes.

2. **The underlying memory** - the bytes the buffers point at. The
   sequence does not own this memory. Whoever created the buffers is
   responsible for keeping the memory alive until the operation
   completes.

The passing-convention debate concerns layer 1 only. Both sides agree
that layer 2 is a harder, separate problem, and that coroutines solve it
elegantly by anchoring the memory in the coroutine frame.

### The Asio Precedent

Asio's specification requires:

> If a read or write operation is also an asynchronous operation, the
> operation shall maintain one or more copies of the buffer sequence
> until the operation no longer requires access to the memory specified
> by the buffers in the sequence.

This is a mandatory copy of the sequence object (layer 1). Asio requires
`CopyConstructible` as a consequence. The copy keeps the sequence alive
across the multiple `async_read_some` / `async_write_some` calls that
compose the full operation.

The question is whether capy should follow this precedent or loosen it
for the coroutine-first context.

## The Case For Mandatory Copy (By-Value)

### Correctness by Default

Taking the buffer sequence by value when returning a raw awaitable is
the only way to guarantee correctness regardless of call pattern. The
concept does not know whether the caller will immediately `co_await` the
result or store the awaitable and `co_await` it later:

```cpp
unsigned char buffer[1024];
auto aw = stream.read_some(mutable_buffer(buffer, sizeof(buffer)));
// ... if read_some took by const&, aw now holds a dangling pointer ...
co_await aw;
```

If `read_some` takes by `const&` or `&&` without storing a copy, the
caller who defers the `co_await` has undefined behavior. Taking by value
eliminates this class of bug.

### Detached Awaitables and Senders

Type-erasing stream wrappers capture the awaitable rather than
`co_await`-ing it inline. The sender bridge in capy captures awaitables
and runs them as sender operations. Both patterns require the awaitable
to be self-contained. A by-value sequence in the awaitable makes this
safe. A reference does not.

### Owning Buffer Sequences

The buffer sequence concept does not preclude owning sequences - types
that hold a `shared_ptr` to their memory and expose `const_buffer`
iterators. The Asio documentation and example code (see
`reference_counted.cpp`) demonstrate this pattern explicitly: the
reference count is the mechanism by which the memory lifetime is tied to
the operation lifetime. A guaranteed copy of the sequence is what makes
this work - when the last copy is destroyed, the reference count drops
and the memory is released.

Without a guaranteed copy, the owning-sequence pattern requires the
caller to manage lifetime explicitly, defeating the purpose.

### Arrays Are Not Copyable

C language arrays (`const_buffer buf[N]`) are not copyable. If the
concept requires `CopyConstructible`, language-level arrays cannot be
passed directly. This is not a reason to drop the copy requirement - it
is a reason to use `std::array<const_buffer, N>` instead. The
distinction is intentional: `std::array` is a first-class range with
copy semantics; C arrays are not.

## The Case Against Mandatory Copy (By-Reference)

### The Copy Is Not Free for All Sequences

While copying a `mutable_buffer` or `std::array<const_buffer, 4>` is
cheap, the concept does not bound the number of buffers in the sequence.
A sequence representing one million scatter-gather regions is legitimate
and not uncommon in high-throughput networking. Mandating a copy of
every such sequence at every call site is a performance tax that
accumulates.

The `buffer_array` type in capy arose as a workaround: it avoids
initializing capacity on construction precisely because the copy is
non-trivial at scale. This is a self-inflicted problem if the copy is
mandatory.

### Wrapping Streams Are Penalized Twice

A stream wrapper that reads from an inner stream and post-processes the
data (e.g., XOR, compression, TLS) must pass the buffer sequence down to
the inner stream. If the concept mandates by-value, the wrapper copies
the sequence on entry, then the inner stream copies it again. Two
copies, neither necessary:

```cpp
template<class ReadStream> class xoring_stream
{
    ReadStream& s_;
public:
    template<class Buffers>
    io_task<size_t> read_some(Buffers buffers) // first copy here
    {
        auto [ec, n] = co_await s_.read_some(buffers); // second copy here
        xor_buffers(buffers, n);
        co_return { ec, n };
    }
};
```

This problem is compounded when the concept mandates by-value
unconditionally, because the wrapper cannot use the more appropriate
`Buffers const&` signature - the concept forces its hand.

### Coroutines Make the Copy Unnecessary

In a coroutine-based design, the caller's buffer sequence is in the
caller's coroutine frame. When the caller `co_await`s the operation, the
caller is suspended and the frame - including the buffer sequence - stays
alive until the operation completes. No copy is needed. Requiring a copy
anyway is a cost that buys nothing in the common case.

### Start Without, Add Later If Needed

If capy ships without mandatory copies, implementations that need them
(for detached awaitables, senders, owning sequences) can make them
explicitly. The converse is not true: if capy mandates copies, the
design calcifies around the copy and there is no path to removing it
later. Starting without the copy preserves optionality.

## Key Tension Points

### By-Value vs. By-`&&`

A forwarding reference (`Buffers&&`) is an alternative to by-value that
avoids copying lvalue sequences:

```cpp
template<class Buffers>
auto read_some(Buffers&& buf)
{
    struct aw {
        stream* s_;
        Buffers buf_; // deduced as reference type for lvalues
    };
    return aw{ this, std::forward<Buffers>(buf) };
}
```

The problem: when `Buffers` is deduced as an lvalue reference, `buf_`
is a reference member. The awaitable then holds a reference to the
caller's object, which may go out of scope before `co_await`. The
by-`&&` approach is only safe for rvalues; for lvalues it silently
introduces the same dangling-reference hazard that by-value avoids.

Correcting this requires `std::decay_t<Buffers>` for the stored type,
which makes by-`&&` equivalent to by-value for the storage decision.

### Tasks vs. Raw Awaitables

The passing convention differs by return type:

- `io_task<size_t> read_some(Buffers const& buf)` - the coroutine frame
  is the storage. The caller's object is kept alive by the `co_await`
  chain. By-`const&` is correct and no copy is made.

- `IoAwaitable auto read_some(Buffers buf)` - the returned object is the
  storage. The awaitable must be self-contained. By-value is required.

The concept as written applies to both cases with a single signature,
which creates the tension. A concept that admits both signatures (and
distinguishes them by return type) would resolve the ambiguity, at the
cost of a more complex concept.

### The Array Problem

The `io_task` path accepts `const_buffer buf[N]` today because it takes
by `const&` and does not make a copy. If the concept is tightened to
require a copy, raw arrays are excluded. The fix is `std::array`, but
this is a source-compatibility break for any code that passes C arrays
today.

## Areas of Agreement

1. **The underlying memory lifetime is a separate, harder problem.**
   Coroutines solve this by keeping the memory in the coroutine frame.
   Buffer sequence passing convention does not affect this.

2. **A `mutable_buffer` or `const_buffer` by itself is cheap to copy.**
   The dispute is about sequences of buffers at scale.

3. **Language-level arrays (`T[]`) are second-class.** They are not
   copyable. Code that needs to pass buffer sequences should use
   `std::array` or a container. The concept should not be weakened to
   accommodate `T[]`.

4. **Raw awaitables must store a copy.** A function returning an
   `IoAwaitable` directly, without a backing coroutine frame, must
   store the buffer sequence in the returned object. By-value is the
   only safe signature.

5. **Coroutine-based tasks can accept by `const&`.** When the function
   is a coroutine returning `io_task`, the sequence lives in the
   caller's frame across the `co_await`. No copy is needed by the
   callee.

## Areas of Disagreement

1. **Whether the concept should mandate the copy.** One view holds that
   mandating a copy is correct by default and the cost is acceptable
   because sequences are supposed to be cheap. The other holds that the
   concept should not constrain implementations unnecessarily and callers
   who need a copy should make one explicitly.

2. **Whether detached awaitables and senders are a primary concern.**
   One view holds that these patterns are real and the concept must
   accommodate them safely. The other holds that they are niche cases
   and should not drive the default API design.

3. **Whether owning sequences justify the copy.** One view holds that
   the owning-sequence pattern (using `shared_ptr` to tie memory
   lifetime to operation lifetime) is a legitimate and useful pattern
   that requires a guaranteed copy. The other holds that coroutines
   eliminate the need for this pattern and it should not drive the
   concept design.

4. **Whether to start permissive or restrictive.** One view holds that
   starting without the copy and adding it later is the right
   engineering approach - remove requirements you don't need. The other
   holds that correctness by default is worth the cost, and relaxing
   later is harder than tightening.

## Summary

| Property                          | By-Value (Copy)      | By-Reference (No Copy) |
| --------------------------------- | -------------------- | ---------------------- |
| Raw awaitable safety              | Guaranteed           | Requires discipline    |
| Detached awaitable / sender safety | Guaranteed          | Requires discipline    |
| Owning sequences                  | Supported            | Not supported          |
| Coroutine overhead                | Unnecessary copy     | None                   |
| Wrapping stream overhead          | Two copies           | None                   |
| Large scatter-gather sequences    | Costly               | Free                   |
| C array compatibility             | Excluded             | Works                  |
| Start permissive, tighten later   | No                   | Yes                    |

The core tension is between safety by default (by-value) and
implementation freedom (by-reference). The by-value convention
eliminates a class of lifetime bugs for detached awaitables and owning
sequences, at the cost of unnecessary copies in the coroutine case and
a penalty for large sequences and wrapping streams. The by-reference
convention is appropriate for coroutine-based tasks but unsafe for raw
awaitables without caller discipline.

A complete resolution likely requires distinguishing the two cases in
the concept itself: raw awaitables mandate by-value; coroutine tasks
accept by `const&`. Whether the standard should mandate that
implementations keep at least one copy alive for the duration of the
operation - regardless of how the parameter is passed - remains an open
question.
