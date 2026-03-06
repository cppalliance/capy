# Design Rationale: read_some Error Postconditions

## Context

This document captures the design space and trade-offs around
`ReadStream::read_some` postconditions when an error occurs. The central
question is whether the concept should guarantee `n == 0` on error, or
permit `n >= 0` on error. A secondary question is the behavior when the
caller passes a zero-length buffer. The analysis applies symmetrically
to `WriteStream::write_some`.

## Current Consensus

The current consensus declares the `ReadStream` and `WriteStream`
concepts with the following contracts:

```cpp
/** Concept for types providing awaitable read operations.

    A type satisfies ReadStream if it provides a read_some
    member function template that accepts any MutableBufferSequence
    and is an IoAwaitable yielding (error_code, std::size_t).

    Semantic Requirements:

    Attempts to read up to buffer_size( buffers ) bytes from
    the stream into the buffer sequence.

    If buffer_size( buffers ) > 0:

    - If !ec, then 1 <= n <= buffer_size( buffers ). n bytes
      were read into the buffer sequence.
    - If ec, then 0 <= n <= buffer_size( buffers ). n is the
      number of bytes read before the I/O condition arose.

    If buffer_empty( buffers ) is true, n is 0. The empty buffer
    is not itself a cause for error, but ec may reflect the state
    of the stream.

    Buffers in the sequence are filled in order.
*/
template< typename T >
concept ReadStream =
    requires( T& stream, mutable_buffer_archetype buffers )
    {
        { stream.read_some( buffers ) } -> IoAwaitable;
        requires awaitable_decomposes_to<
            decltype( stream.read_some( buffers ) ),
            std::error_code, std::size_t >;
    };
```

```cpp
/** Concept for types providing awaitable write operations.

    A type satisfies WriteStream if it provides a write_some
    member function template that accepts any ConstBufferSequence
    and is an IoAwaitable yielding (error_code, std::size_t).

    Semantic Requirements:

    Attempts to write up to buffer_size( buffers ) bytes from
    the buffer sequence to the stream.

    If buffer_size( buffers ) > 0:

    - If !ec, then 1 <= n <= buffer_size( buffers ). n bytes
      were written from the buffer sequence.
    - If ec, then 0 <= n <= buffer_size( buffers ). n is the
      number of bytes written before the I/O condition arose.

    If buffer_empty( buffers ) is true, n is 0. The empty buffer
    is not itself a cause for error, but ec may reflect the state
    of the stream.

    Buffers in the sequence are consumed in order.
*/
template< typename T >
concept WriteStream =
    requires( T& stream, const_buffer_archetype buffers )
    {
        { stream.write_some( buffers ) } -> IoAwaitable;
        requires awaitable_decomposes_to<
            decltype( stream.write_some( buffers ) ),
            std::error_code, std::size_t >;
    };
```

The rationale for these choices follows.

## Background

### The read_some Contract

`read_some` accepts a buffer sequence and returns `(error_code, size_t)`.
The success case is uncontroversial:

- **Success:** `!ec`, and `n >= 1` (at least one byte transferred).

The disputes concern the error case and the empty-buffer case.

### The Canonical I/O Loop

Every composed read algorithm that accumulates progress follows the same
pattern:

```cpp
auto [ec, n] = co_await s.read_some(
    mutable_buffer( buf + total, size - total ) );
total += n;
if( ec )
    co_return;
```

The advance-then-check ordering is the only correct pattern for I/O loops.
It is required for any function that can report partial progress alongside
an error - `read` returning `(eof, 47)` being the canonical example. If
the check precedes the advance, the 47 bytes are silently dropped.

This ordering is correct under both postconditions. Under the strict rule
(`n == 0` on error), the advance is a harmless no-op. Under the relaxed
rule (`n >= 0` on error), the advance captures partial progress. The
caller writes identical code either way.

### Concepts vs. Concrete Types

A concept specifies the least set of requirements that generic code may
rely on. Concrete types that model the concept may offer stronger
guarantees as refinements. The concept does not prevent a concrete type
from being more specific; it only determines what generic algorithms
can assume.

### The memcpy Precedent

C++ historically gave `memcpy` a precondition that the source and
destination pointers must be non-null, even when the byte count is zero.
This forced every call site to guard with `if( n > 0 )`, despite the
obvious meaning of copying zero bytes from a null range. C++23 corrected
this by allowing null pointers when the count is zero.

The zero-length buffer question in `read_some` is structurally identical.
`(nullptr, 0)` is an established representation of an empty range, and
requiring callers to guard against it creates friction with no
corresponding benefit.

## The Zero-Length Buffer Question

Three options exist for `read_some` when `buffer_empty(buffers)` is true:

### Option Z1: Precondition (buffer_size > 0)

Passing a zero-length buffer is undefined behavior.

**Arguments for:**

1. Avoids inconsistencies between platforms in the zero-length case.
2. Simplifies the specification by removing a corner case.

**Arguments against:**

1. Crashes are almost never desirable in a server. Unnecessary
   preconditions increase risk in environments with adversarial inputs
   and concurrency.
2. Empty ranges are legitimate. Protocols with length-prefixed payloads
   naturally produce zero-length payloads. Framing code that calls
   `write(payload)` after writing the length header should not need a
   guard for the empty case.
3. Repeats the `memcpy` mistake that C++23 corrected.
4. Forces every caller that might receive a zero-length buffer to wrap
   the call with `if( buffer_size(buffers) > 0 )`. The wrapping is
   pure friction.
5. Generic code constrained by `ReadStream` cannot distinguish "the
   buffer happened to be empty" from a bug. Crashing on a legitimate
   edge case is hostile to generic programming.

### Option Z2: Error (return EINVAL)

Return `(EINVAL, 0)` for a zero-length buffer.

**Arguments for:**

1. No crash. The caller receives a well-defined result.
2. Signals that the call was unusual, allowing the caller to distinguish
   the empty case from a real I/O error if desired.

**Arguments against:**

1. A zero-length buffer is not invalid input. It is the degenerate case
   of a legitimate operation, just as copying zero bytes is the
   degenerate case of `memcpy`.
2. Returning an error for a non-error condition forces every call site
   that might produce empty buffers to either pre-check or post-filter
   `EINVAL` out of the error path.
3. For `write`, the analysis is clear: `write` returns either
   `(0, n)` on success or `(ec, m)` with `m < n` on failure.
   Substituting `n = 0`, only the success case `(0, 0)` satisfies
   both conditions. The error case cannot arise.

### Option Z3: Not an Error (n == 0, ec unspecified)

The empty buffer is not itself an error condition: no EINVAL, no
precondition violation. `n` is 0. Whether `ec` is set depends on the
implementation - the operation may report errors arising from the state
of the stream (e.g., a closed fd or broken pipe). Whether a system call
occurs is unspecified.

**Arguments for:**

1. No guard needed at call sites. Generic code that might produce empty
   buffers works without special-casing.
2. Matches the corrected `memcpy` semantics in C++23: empty input is
   permitted, not an error in itself.
3. The concept leaves both system call behavior and `ec` unspecified,
   which is the weakest useful postcondition. Concrete types that
   short-circuit empty buffers with `(!ec, 0)` conform. Concrete types
   that forward the zero-length call to the OS and report whatever
   error arises also conform.
4. Permits zero-length operations to serve as probes (fd validation,
   broken pipe detection) without the concept forbidding the resulting
   error.

**Arguments against:**

1. Callers cannot rely on `(!ec, 0)` for empty buffers. A caller that
   passes an empty buffer expecting guaranteed success must handle the
   possibility of an error from the underlying I/O state. In practice
   this is not a burden because correct I/O code already checks `ec`
   unconditionally.

**Recommendation:** Option Z3. The concept should specify the weakest
useful postcondition. The only guarantee is `n == 0`; everything else
is left to the implementation.

## The Error Postcondition Question

When `buffer_size(buffers) > 0` and the operation fails, two
postconditions are under consideration:

### Option E1: Error Implies n == 0

On error, `ec` is set and `n` is guaranteed to be 0.

**Arguments for:**

1. **Clean partition of the return space.** The return values form a
   non-overlapping table:

   | Condition    | ec            | n      |
   | ------------ | ------------- | ------ |
   | Empty buffer | unspecified   | `0`    |
   | Success      | `!ec`         | `>= 1` |
   | Error        | `ec`          | `0`    |

   `n > 0` alone tells you the operation succeeded. When `n == 0` and
   `ec` is set, the operation failed. When `n == 0` and `!ec`, the
   buffer was empty.

2. **`total` is unambiguous in generic code.** After the canonical loop,
   `total` represents bytes that were transferred without any error
   during the transfer that produced them. Under the relaxed rule,
   `total` might include bytes from a transfer that also reported an
   error, and only type-specific knowledge can determine whether those
   bytes are trustworthy.

3. **Concrete types can refine upward.** If `tcp_socket::read_some`
   wants to expose POSIX short-read-on-error semantics, it can document
   `n >= 0` on error as a refinement. The concept does not prevent
   concrete types from having richer postconditions; it just means
   generic code does not rely on them.

4. **Matches POSIX semantics.** POSIX `read(2)` and `write(2)` return a
   single `ssize_t` - either a byte count or -1 with `errno`. They
   never report both simultaneously. When a partial transfer occurs
   before an error, POSIX returns the byte count on the current call and
   the error on the next. A `read_some` wrapper around POSIX naturally
   produces `(ec, 0)` on error with no special effort. E1 is the direct
   translation of the POSIX model into `(error_code, size_t)`.

**Arguments against:**

1. **Forces non-POSIX streams to emulate POSIX's deferred-error model.**
   POSIX splits partial-transfer-then-error into two calls because its
   API returns a single value. The `(error_code, size_t)` return type
   was designed to transcend this limitation. Layered streams (TLS,
   compression, protocol framers) may encounter an error after a partial
   transfer as a single atomic event. Under E1, they must artificially
   split this into two calls - reporting success now and deferring the
   error - reproducing a POSIX API limitation that the richer return
   type was meant to eliminate.

2. **Inverts the meaning of "least common denominator."** A concept
   should impose the weakest postcondition that all conforming types can
   satisfy. Every stream can satisfy `n >= 0` on error. Not every stream
   can satisfy `n == 0` on error without extra work. The strict rule is a
   *stronger* requirement on implementers, which is the opposite of what
   a least-common-denominator concept should impose.

3. **Loses information.** If the implementation transferred bytes into
   the buffer before encountering an error, reporting `(ec, 0)` instead
   of `(ec, k)` means the caller cannot know the buffer was partially
   written. For layered streams (TLS, compression, protocol framing),
   this lost information can matter for resynchronization or error
   recovery.

4. **Forces implementations to lie or buffer.** A TLS stream might
   decrypt 100 bytes into user space, then receive a fatal alert on the
   next record. Under the strict rule it must either report `(!ec, 100)`
   now and `(ec, 0)` on the next call (requiring deferred-error
   bookkeeping), or report `(ec, 0)` and discard 100 valid bytes.
   Neither is clean.

### Option E2: Error Permits n >= 0

On error, `ec` is set and `n` may be any value from 0 to the buffer
size.

**Arguments for:**

1. **Transcends the POSIX API limitation.** POSIX `read(2)` cannot
   report a byte count and an error simultaneously because it returns a
   single `ssize_t`. The `(error_code, size_t)` return type can. E2
   allows implementations to use this capability, reporting partial
   transfers alongside errors as a single result rather than
   artificially splitting them across two calls.

2. **Strictly weaker postcondition.** Any stream that can satisfy E1
   trivially satisfies E2. The reverse is not true. E2 admits a strictly
   larger set of conforming types.

3. **Preserves maximum information.** The caller receives the byte count
   regardless of error state. Callers who do not care can ignore `n` on
   error (the advance is a no-op when `n == 0`). Callers who do care
   get the data they need.

4. **Implementer freedom.** The concept does not dictate how the
   implementation manages partial I/O internally. If TLS decrypted 100
   bytes before the alert, it can report `(ec, 100)` honestly. No
   deferred-error bookkeeping, no discarded data.

5. **Zero additional call-site cost.** The canonical I/O loop advances
   before checking the error:

   ```cpp
   total += n;
   if( ec )
       co_return;
   ```

   This pattern is already mandatory for every function that can report
   partial progress alongside an error (EOF with partial data being the
   canonical case). The relaxed postcondition does not change the code
   callers must write - it only changes whether the advance is a no-op
   or captures real progress.

**Arguments against:**

1. **Ambiguity of partial bytes.** When `read_some` returns
   `(connection_reset, 42)`, are those 42 bytes valid and complete? The
   concept cannot answer this because the answer depends on the concrete
   stream. Generic code that accumulated them into `total` may be
   reporting a byte count that includes bytes from a failed transfer.

2. **Return space is no longer cleanly partitioned.** The table becomes:

   | Condition    | ec            | n      |
   | ------------ | ------------- | ------ |
   | Empty buffer | unspecified   | `0`    |
   | Success      | `!ec`         | `>= 1` |
   | Error        | `ec`          | `>= 0` |

   The error row overlaps with success on `n > 0`. `n > 0` is no longer
   a proxy for success; `ec` must always be checked independently.

3. **Concrete types can refine upward.** This argument applies
   symmetrically: the concept can say `n == 0` on error without
   preventing concrete types from documenting `n >= 0`. The concept
   constrains generic code, not concrete implementations.

## Analysis

### The Call-Site Argument Is Phantom

The strongest intuitive argument for E1 is that it simplifies call sites.
This does not survive scrutiny. The canonical I/O loop must advance before
checking the error regardless of which postcondition the concept chooses,
because the advance-then-check pattern is required for correctness with
any function that reports partial progress (such as `read` reporting
bytes transferred before EOF). The code is identical under both rules:

```cpp
auto [ec, n] = co_await s.read_some( buf );
total += n;
if( ec )
    co_return;
```

Under E1, the advance on error is a no-op (`n == 0`). Under E2, the
advance may capture real progress. In neither case does the caller write
different code. The perceived simplification of E1 exists only if the
caller writes the check-then-advance anti-pattern, which is already
incorrect for other reasons.

### The Concept's Job

A concept specifies what generic code may rely on. The question is: does
generic code benefit from knowing `n == 0` on error?

The primary consumer is the accumulation loop. As shown above, it writes
identical code either way. The secondary consumer is code that inspects
`n > 0` as a proxy for success. Under E1, this proxy works. Under E2,
the caller must check `ec` independently. But checking `ec` is what
correct I/O code does regardless - using `n > 0` as a success proxy is
a shortcut that masks the error status, not a pattern the concept should
encourage.

### The Implementer's Burden

Under E1, every stream that might encounter an error after partial
transfer must choose between:

- **Deferred errors.** Report `(!ec, k)` now, remember the error, and
  report `(ec, 0)` on the next call. This requires per-stream state and
  makes the stream's behavior depend on call history.
- **Data loss.** Report `(ec, 0)` and discard the `k` bytes that were
  transferred. The caller's buffer contains data the stream will not
  acknowledge.
- **Internal buffering.** Copy the `k` bytes into an internal buffer
  and replay them on the next call. This adds allocation and copying
  overhead for a postcondition the caller does not need.

Under E2, the implementation reports what happened: `(ec, k)`. No
deferred state, no data loss, no internal buffering.

### Symmetry with write_some

The analysis applies symmetrically to `write_some`. Consider a stream
that successfully sends 500 bytes of a 1000-byte buffer before
encountering a connection reset. Under E1, it must report `(ec, 0)` and
the caller cannot know that 500 bytes reached the peer. Under E2, it
reports `(ec, 500)` and the caller can account for the partial transfer
in retry logic or error reporting.

### The Partition Argument

E1 produces a cleaner partition of the return space, where `n > 0`
alone distinguishes success from error. This is a real property, but its
practical value is limited: correct I/O code always checks `ec`. The
partition is an aesthetic property of the specification, not a property
that changes what correct call sites look like.

### The Composed Operation Argument

The strict postcondition on `write_some` does not propagate to composed
operations. `write` (which transfers the full buffer) returns `(ec, m)`
where `m < n` on failure - it must report partial progress alongside
the error because there is no other way to tell the caller how many
bytes were sent. The `(ec, n>0)` case that E1 eliminates from
`write_some` is immediately reintroduced one layer up.

This observation has a structural consequence: E1 reproduces the POSIX
deferred-error model at the `_some` layer, then undoes it at the
composed layer. POSIX defers errors across calls because its API returns
a single `ssize_t`. The `(error_code, size_t)` return type was designed
to carry both values simultaneously. E1 artificially constrains the
primitive to not use this capability, then the first composed operation
built on top of it must reintroduce partial-progress-with-error because
the composition has no other way to report what happened. The strict
guarantee is local to the primitive and dissolves upon composition.

## Areas of Agreement

1. **The advance-then-check pattern is mandatory.** Regardless of which
   postcondition the concept chooses, the canonical loop advances
   `total` before checking `ec`. This is a requirement of correct I/O
   programming, not a consequence of the postcondition choice.

2. **Zero-length buffers should not crash.** A precondition requiring
   non-empty buffers repeats the `memcpy` mistake. Empty ranges are
   legitimate in protocols with variable-length payloads.

3. **Zero-length buffers are not an error.** The empty buffer itself
   should not cause a dedicated error. `n` is 0. Whether `ec` is set
   depends on the I/O state of the stream, not on the buffer size.

4. **The concept constrains generic code.** Concrete types may refine
   the concept's postconditions with stronger guarantees. The concept
   specifies the floor, not the ceiling.

5. **EOF is an error condition with partial data.** `read` returning
   `(eof, 47)` is the canonical example of partial progress with an
   error. Every I/O loop must handle this correctly, and the
   advance-then-check pattern exists precisely for this reason.

## Areas of Disagreement

1. **Whether the concept should minimize implementer burden or maximize
   return-value clarity.** E1 produces a cleaner partition of the return
   space. E2 avoids deferred-error bookkeeping, data loss, and internal
   buffering in implementations. The disagreement is over which cost is
   more important.

2. **Whether `n > 0` should be a success proxy.** Under E1, `n > 0`
   implies `!ec`. Under E2, it does not. One view holds that this proxy
   is a useful property for reasoning about return values. The other
   holds that correct code always checks `ec`, and the proxy encourages
   a shortcut that masks the error status.

3. **Whether "least common denominator" favors weaker or stronger
   postconditions.** One view holds that the concept should impose the
   weakest requirements all streams can satisfy (E2). The other holds
   that the concept should impose the strongest requirements that still
   admit all *useful* streams, on the grounds that a concept exists to
   enable generic programming, not to admit the widest set of types (E1).

4. **Whether the meaning of `total` matters.** Under E1, `total`
   after a loop represents bytes transferred without error. Under E2,
   `total` represents bytes transferred, some of which may have been
   accompanied by an error. One view holds that the former is more
   meaningful. The other holds that both are equally useful, since the
   bytes were transferred either way.

## Summary

| Property                         | E1 (n == 0 on error) | E2 (n >= 0 on error)  |
| -------------------------------- | -------------------- | --------------------- |
| Call-site code                   | Identical            | Identical             |
| Return-value partition           | Clean                | Overlapping           |
| Implementer burden               | Higher               | Lower                 |
| Information preservation         | Lossy                | Lossless              |
| POSIX alignment                  | Matches              | Transcends            |
| Composed operations              | Reintroduce (ec,n>0) | Consistent throughout |
| Concept philosophy               | Strongest useful     | Weakest possible      |
| n > 0 implies success            | Yes                  | No                    |
| Deferred-error bookkeeping       | Required for some    | Never required        |

The core tension is between specification clarity (E1) and implementation
honesty (E2). E1 matches POSIX, produces a clean return-value partition,
and is the natural translation of the single-return-value POSIX model
into `(error_code, size_t)`. E2 uses the richer return type to transcend
the POSIX limitation, avoids forcing non-POSIX streams into a
deferred-error model, and produces a postcondition that is consistent
from `_some` primitives through composed operations. Since the canonical
call-site code is identical under both rules, the choice reduces to
whether the concept should reproduce the POSIX deferred-error model at
the cost of implementation burden on layered streams, or whether it
should permit the implementation to report exactly what happened at the
cost of a less tidy specification.
