# Design Rationale: Buffer Representation and Ownership

## Context

This document captures the design space and trade-offs for the buffer
subsystem in capy. The central question is how to represent memory
regions for asynchronous I/O in a way that is zero-copy, composable
with C++20 concepts, and safe across coroutine suspension points. The
analysis applies to four interrelated design decisions:

1. How to represent individual buffer regions and buffer sequences.
2. How to customize buffer operations (size, slicing) without virtual
   dispatch.
3. How to manage resizable buffers (DynamicBuffer) with correct
   lifetime semantics in coroutine-based APIs.
4. How to model the two buffer ownership patterns (caller-owns vs.
   callee-owns) for asynchronous data transfer.

The design was shaped by 25 years of Asio practice, the constraints
of C++20 coroutines, and the goal of supporting both POSIX scatter/gather
I/O and layered protocol streams.

## Current Design

### Primitive Types

Two non-owning reference types form the foundation:

```cpp
class mutable_buffer
{
    unsigned char* p_ = nullptr;
    std::size_t n_ = 0;
public:
    constexpr mutable_buffer(void* data, std::size_t size) noexcept;
    constexpr void* data() const noexcept;
    constexpr std::size_t size() const noexcept;
    mutable_buffer& operator+=(std::size_t n) noexcept;
};

class const_buffer
{
    unsigned char const* p_ = nullptr;
    std::size_t n_ = 0;
public:
    constexpr const_buffer(void const* data, std::size_t size) noexcept;
    constexpr const_buffer(mutable_buffer const& b) noexcept;
    // ...
};
```

`mutable_buffer` is implicitly convertible to `const_buffer`. Both
support `operator+=` for advancing the start position without
allocation.

### Buffer Sequence Concepts

```cpp
template<typename T>
concept ConstBufferSequence =
    std::is_convertible_v<T, const_buffer> || (
        std::ranges::bidirectional_range<T> &&
        std::is_convertible_v<std::ranges::range_value_t<T>, const_buffer>);

template<typename T>
concept MutableBufferSequence =
    std::is_convertible_v<T, mutable_buffer> || (
        std::ranges::bidirectional_range<T> &&
        std::is_convertible_v<std::ranges::range_value_t<T>, mutable_buffer>);
```

A single buffer satisfies the sequence concept (one-element range via
pointer arithmetic on `begin`/`end`). This eliminates the need for
callers to distinguish between single buffers and multi-buffer
sequences.

### Customization via tag_invoke

Buffer operations (`buffer_size`, slicing) are customized through
`tag_invoke` with dedicated tag types (`size_tag`, `slice_tag`). Types
that provide a `tag_invoke` overload for `slice_tag` are sliced
in-place; types that do not are wrapped in `slice_of<T>`.

### DynamicBuffer with Coroutine Safety

```cpp
template<class T>
concept DynamicBuffer = /* prepare/commit/consume interface */;

template<class B>
concept DynamicBufferParam =
    DynamicBuffer<std::remove_cvref_t<B>> &&
    (std::is_lvalue_reference_v<B> ||
     requires { typename std::remove_cvref_t<B>::is_dynamic_buffer_adapter; });
```

`DynamicBufferParam` restricts rvalue passing to adapter types that
reference external storage. Value types that store bookkeeping
internally are rejected as rvalues, preventing silent data loss when
a coroutine suspends.

### Buffer Ownership Models

Two concepts model asynchronous data transfer:

- **BufferSource** (pull model): Callee produces data, caller consumes.
  `pull()` returns buffer descriptors; `consume(n)` advances.

- **BufferSink** (callee-owns-buffers): Callee provides writable memory,
  caller writes into it. `prepare()` returns writable buffers;
  `commit(n)` finalizes; `commit_eof(n)` signals end-of-stream.

## Background

### The Scatter/Gather I/O Model

POSIX `readv` and `writev` accept arrays of `iovec` structures, each
describing a contiguous memory region. This scatter/gather model avoids
the cost of assembling a contiguous buffer before a system call. The
buffer sequence concept is the C++ generalization of this model: any
type that produces a range of `(pointer, size)` pairs can participate
in I/O without copying data into an intermediate buffer.

### The Asio Precedent

Boost.Asio established the buffer sequence model that capy inherits.
Asio's `const_buffer`, `mutable_buffer`, `ConstBufferSequence`, and
`MutableBufferSequence` concepts have been stable across 20+ years of
production use. The capy design preserves the conceptual model while
modernizing the mechanism:

- Asio uses `buffer_sequence_begin` / `buffer_sequence_end` free
  functions and SFINAE-based traits. Capy uses C++20 concepts and
  `std::ranges`.
- Asio's `dynamic_string_buffer` and `dynamic_vector_buffer` accept
  references. Capy adds the `DynamicBufferParam` concept to enforce
  lifetime safety at compile time.
- Asio has no equivalent of the `BufferSource` / `BufferSink` concepts,
  which capy introduces for structured data transfer pipelines.

### Coroutine Suspension and Buffer Lifetimes

When a coroutine suspends, its local variables live in the coroutine
frame. Parameters passed by reference may dangle if the caller's scope
exits before resumption:

```cpp
// WRONG: buffers may dangle across co_await
task<> read_some(MutableBufferSequence auto const& buffers);

// CORRECT: buffers copied into coroutine frame
task<> read_some(MutableBufferSequence auto buffers);
```

This constraint propagates through the design. `buffer_param` accepts
a `const&` in its constructor because the outer template function has
already captured the buffer sequence by value in the coroutine frame.
`DynamicBufferParam` enforces the rule at the concept level:
value types must be passed as lvalues (the caller retains ownership),
while adapter types may be passed as rvalues (the external storage
persists).

## The Buffer Representation Question

### Option R1: Non-Owning Pointer-Size Pair

Use two lightweight types (`const_buffer`, `mutable_buffer`) that store
a pointer and a size. No ownership, no allocation. The caller manages
the underlying memory.

**Arguments for:**

1. **Zero overhead.** A buffer descriptor is two machine words. Copying,
   comparing, and advancing are trivial operations.
2. **Matches the OS model.** `iovec` is `{void*, size_t}`. The buffer
   types are a direct, typesafe mapping.
3. **Composable.** Single buffers satisfy the buffer sequence concept.
   Multi-buffer containers (`buffer_array`, `buffer_pair`,
   `std::array<mutable_buffer, N>`) compose naturally.
4. **25 years of production stability.** Asio's identical representation
   has survived without modification.

**Arguments against:**

1. **No lifetime tracking.** The caller must ensure the referenced
   memory outlives the buffer descriptor. In coroutine contexts this
   requires discipline (pass by value, not reference).
2. **No capacity.** Unlike `span`, a buffer does not carry a "max size"
   distinct from "current size." Resizable behavior requires a
   separate DynamicBuffer wrapper.

### Option R2: Owning Buffer with Embedded Storage

Provide a buffer type that owns its memory, similar to `std::vector<char>`.

**Arguments for:**

1. **Lifetime safety by construction.** No dangling references.
2. **Simpler mental model** for users unfamiliar with non-owning types.

**Arguments against:**

1. **Allocation cost.** Every buffer construction allocates. I/O
   operations that should be zero-copy now copy into owned storage.
2. **Incompatible with scatter/gather.** The OS provides memory (e.g.,
   kernel buffers, memory-mapped regions); wrapping it in an owning
   type requires copying.
3. **Breaks the composition model.** A `read_some` that returns an
   owning buffer cannot write into caller-provided memory.
4. **No precedent.** No major I/O library (Asio, libuv, io_uring, Windows
   IOCP) uses owning buffers at the primitive level.

### Option R3: span-Based Representation

Use `std::span<std::byte>` and `std::span<std::byte const>` directly.

**Arguments for:**

1. **Standard vocabulary type.** Users already know `span`.
2. **Const-correctness through the type system.** `span<byte const>`
   is read-only; `span<byte>` is writable.

**Arguments against:**

1. **Type pollution.** `span<byte>` is not implicitly convertible to
   `span<byte const>` through the same mechanism as `mutable_buffer`
   to `const_buffer`. The generic code that accepts both must use
   additional template machinery.
2. **No customization points.** `span` does not support `tag_invoke`
   for size or slicing without wrapping.
3. **Element type mismatch.** `span<byte>` requires callers to cast
   from `void*` or `char*`. The buffer types accept `void*` directly,
   which matches the POSIX and Asio conventions.
4. **No `operator+=`.** Advancing a `span` requires constructing a
   new subspan. The buffer types support in-place advance, which is
   the dominant operation in I/O loops.

**Recommendation:** Option R1. The pointer-size pair is the minimal
representation that maps to the OS model, composes with scatter/gather
I/O, and has decades of production stability.

## The Customization Mechanism Question

Buffer operations need customization: `buffer_size` should be O(1) for
types that track total size, and slicing should be in-place for types
that support it. The question is how to dispatch to type-specific
implementations.

### Option C1: tag_invoke

Provide tag types (`size_tag`, `slice_tag`) and dispatch through ADL
`tag_invoke`. Types that provide an overload get the optimized path;
the default falls back to iteration.

**Arguments for:**

1. **Non-intrusive.** Third-party types can opt in without modifying
   their class definition.
2. **Composable.** The same mechanism handles `buffer_array`,
   `buffer_pair`, `slice_of`, and user-defined types uniformly.
3. **No virtual dispatch.** The call resolves at compile time.
4. **Established pattern.** `tag_invoke` is the customization mechanism
   used throughout the P2300 ecosystem.

**Arguments against:**

1. **Unfamiliar syntax.** `tag_invoke(slice_tag{}, bs, how, n)` is
   harder to read than `bs.slice(how, n)`.
2. **Discoverability.** Users cannot rely on IDE autocompletion to find
   available customization points.

### Option C2: Virtual Member Functions

Use a base class with virtual `size()` and `slice()` methods.

**Arguments for:**

1. **Familiar OOP pattern.** Users understand virtual dispatch.
2. **Discoverable.** IDE completion shows available methods.

**Arguments against:**

1. **Allocation and indirection.** Virtual dispatch requires a vtable
   pointer. Buffer descriptors are two machine words; adding a vtable
   pointer increases their size by 50%.
2. **Incompatible with value semantics.** Buffers are copied freely in
   I/O loops. Polymorphic types require heap allocation or slicing
   protection.
3. **Closed hierarchy.** New buffer types must inherit from the base
   class, which forecloses types that cannot be modified.

### Option C3: Concept-Based Overloading

Overload free functions on concept constraints without `tag_invoke`.

**Arguments for:**

1. **Simpler.** No tag types needed.
2. **C++20 native.** Concept-constrained overloads are the standard
   mechanism.

**Arguments against:**

1. **Ambiguity.** Without tags, two overloads for "size" on different
   concepts may conflict. `tag_invoke` scopes the customization point
   to the tag type, preventing collision.
2. **No fallback dispatch.** The default `buffer_size` iterates over
   the sequence and sums individual sizes. With `tag_invoke`, the
   default path and the optimized path coexist naturally; with concept
   overloading, the mechanism for selecting "use the optimized version
   if available, otherwise iterate" requires additional SFINAE.

**Recommendation:** Option C1. `tag_invoke` provides non-intrusive,
composable customization with compile-time dispatch. The syntax cost
is paid by library implementers, not users, since the free functions
(`buffer_size`, `keep_prefix`, `remove_prefix`, etc.) hide the
dispatch.

## The Dynamic Buffer Lifetime Question

Dynamic buffers support resizable I/O targets (the `prepare` /
`commit` / `consume` protocol). The question is how to enforce correct
passing in coroutine APIs.

### Option L1: Unconstrained Forwarding Reference

Accept `DynamicBuffer auto&&` in coroutine functions.

**Arguments for:**

1. **Simplest signature.** No additional concept needed.

**Arguments against:**

1. **Silent data loss.** A value type like `flat_dynamic_buffer` passed
   as an rvalue is moved into the coroutine frame. Its bookkeeping
   (size, position) is local to the frame. When the coroutine completes,
   the caller's original buffer is unchanged - the committed data is
   silently discarded.

### Option L2: Lvalue Reference Only

Accept `DynamicBuffer auto&` in coroutine functions.

**Arguments for:**

1. **Correct for value types.** The caller retains ownership and
   observes mutations.

**Arguments against:**

1. **Rejects valid adapters.** `string_dynamic_buffer` wraps an
   external `std::string*`. Passing it as an rvalue is safe because
   the external string retains the data. Requiring an lvalue forces
   the caller to name every temporary adapter, adding friction:

   ```cpp
   // Rejected, but safe:
   co_await read(stream, string_dynamic_buffer(&s));

   // Required workaround:
   auto buf = string_dynamic_buffer(&s);
   co_await read(stream, buf);
   ```

### Option L3: DynamicBufferParam Concept

Introduce a second concept that allows lvalues of any `DynamicBuffer`
and rvalues only for types that define
`using is_dynamic_buffer_adapter = void`:

```cpp
template<class B>
concept DynamicBufferParam =
    DynamicBuffer<std::remove_cvref_t<B>> &&
    (std::is_lvalue_reference_v<B> ||
     requires { typename std::remove_cvref_t<B>::is_dynamic_buffer_adapter; });
```

Coroutine functions use `DynamicBufferParam auto&&`.

**Arguments for:**

1. **Compile-time safety.** Value types passed as rvalues are rejected.
   Adapter types passed as rvalues are accepted. The correct passing
   convention is enforced, not documented.
2. **Zero runtime cost.** The check is entirely in the type system.
3. **Preserves ergonomics.** `co_await read(stream, dynamic_buffer(s))`
   works because the factory returns an adapter type.

**Arguments against:**

1. **Requires opt-in tag.** Every adapter type must define
   `is_dynamic_buffer_adapter`. Forgetting the tag causes a compile
   error, which is the safe failure mode but adds a requirement for
   implementers.
2. **Two concepts for one abstraction.** Users must learn when to use
   `DynamicBuffer` (non-coroutine, lvalue ref) vs `DynamicBufferParam`
   (coroutine, forwarding ref).

**Recommendation:** Option L3. The compile-time enforcement eliminates
a class of silent data-loss bugs that are difficult to diagnose at
runtime. The cost (an extra tag typedef and a second concept) is paid
by library authors, not users.

## The Buffer Ownership Question

Asynchronous data transfer between a producer and a consumer requires
a decision about who provides the memory. Two models exist.

### Option O1: Caller-Owns Buffers (WriteSink / ReadStream)

The caller provides buffers; the I/O operation reads from or writes
into them:

```cpp
auto [ec, n] = co_await stream.write_some(caller_buffers);
```

**Arguments for:**

1. **Caller controls allocation.** Stack buffers, pooled buffers, and
   memory-mapped regions are all usable without adaptation.
2. **Natural for stream I/O.** `read_some` / `write_some` have always
   worked this way.
3. **No internal buffering.** The data path is caller -> kernel, with
   no intermediate copy.

**Arguments against:**

1. **Caller must manage buffer lifetime.** The buffers must remain
   valid until the I/O completes (coroutine resumes).
2. **Does not support zero-copy callee-initiated transfers.** If the
   sink has internal storage (compression buffer, TLS record buffer),
   copying from caller buffers into internal storage is unavoidable.

### Option O2: Callee-Owns Buffers (BufferSink)

The sink provides writable memory; the caller writes directly into it:

```cpp
auto dst_bufs = sink.prepare(dst_arr);
std::size_t n = buffer_copy(dst_bufs, src_bufs);
auto [ec] = co_await sink.commit(n);
```

**Arguments for:**

1. **Zero-copy into internal storage.** The caller writes directly into
   the sink's compression buffer, TLS record buffer, or kernel buffer.
   No intermediate copy.
2. **Sink controls memory layout.** The sink can align buffers, size
   them for protocol framing, or provide buffers from a pool.
3. **Enables back-pressure.** An empty `prepare()` return signals that
   the sink has no available space; the caller must wait for `commit`
   to flush.

**Arguments against:**

1. **Sink must provide storage.** If the sink is a raw socket, it must
   either maintain an internal buffer or delegate to the kernel. For
   simple streams this is unnecessary overhead.
2. **More complex protocol.** Three operations (`prepare`, write,
   `commit`) vs. one (`write_some`).

### Option O3: Both Models, Separate Concepts

Provide both `WriteSink` (caller-owns) and `BufferSink` (callee-owns)
as distinct concepts. Similarly, provide both `ReadStream`
(caller-owns) and `BufferSource` (callee-owns) for producers.

**Arguments for:**

1. **Each model fits its natural domain.** Stream I/O uses
   caller-owns (the Asio model). Layered protocols and compression
   use callee-owns. Neither model subsumes the other.
2. **No forced adaptation.** A raw socket implements `WriteStream`
   directly. A TLS layer implements `BufferSink` directly. Neither
   must pretend to be the other.
3. **Transfer algorithms compose the two.** A generic `transfer`
   function can connect a `BufferSource` to a `BufferSink`, or a
   `BufferSource` to a `WriteStream`, choosing the ownership model
   that minimizes copies for each pairing.

**Arguments against:**

1. **Two concepts where one might suffice.** Users must learn both
   models and understand which to use.
2. **Adapter proliferation.** Converting between models requires
   adapter types.

**Recommendation:** Option O3. The two ownership models serve
different domains and neither subsumes the other. Providing both as
first-class concepts enables the library to minimize copies at each
layer boundary.

## The Windowed Access Question

Buffer sequences may contain many elements. Passing them through
virtual function boundaries or to system calls that accept a limited
number of `iovec` structures requires batching.

### Option W1: Flatten to Contiguous Buffer

Copy all data into a single contiguous buffer before the system call.

**Arguments for:**

1. **Simplest code.** A single buffer needs no batching logic.

**Arguments against:**

1. **Allocation and copy cost.** For large transfers this is
   prohibitive.
2. **Defeats scatter/gather.** The entire point of buffer sequences
   is to avoid this copy.

### Option W2: buffer_param Windowed Wrapper

Wrap the buffer sequence in `buffer_param`, which maintains a sliding
window of up to `max_iovec` buffer descriptors. `data()` returns the
current window as a `span`; `consume(n)` advances:

```cpp
task<> write(ConstBufferSequence auto buffers)
{
    buffer_param bp(buffers);
    while(true)
    {
        auto bufs = bp.data();
        if(bufs.empty())
            break;
        auto n = co_await do_write(bufs);
        bp.consume(n);
    }
}
```

**Arguments for:**

1. **Zero allocation.** The window is a fixed-size array in the
   `buffer_param` object.
2. **Natural batch size.** The window size matches the OS limit for
   scatter/gather I/O (`IOV_MAX`).
3. **Enables virtual dispatch.** The template captures the buffer
   sequence type; the virtual function receives `std::span<const_buffer>`.
   This bridges templates and virtual functions without type erasure.
4. **Empty buffers are skipped.** The window contains only non-empty
   buffers, which is a requirement of most OS scatter/gather APIs.

**Arguments against:**

1. **Fixed window size.** If the OS supports more `iovec` entries than
   `max_iovec`, the window is unnecessarily small. In practice,
   `IOV_MAX` is 1024 on Linux and `max_iovec` is tuned accordingly.

**Recommendation:** Option W2. The windowed wrapper eliminates
allocation, matches the OS batch size, and enables the template-to-
virtual-function bridge that layered protocol implementations require.

## Areas of Agreement

1. **Buffers are non-owning reference types.** The primitive buffer
   types describe memory; they do not own it. Ownership is the
   caller's responsibility, managed through stack allocation, dynamic
   buffers, or external containers.

2. **Single buffers satisfy the sequence concept.** Requiring callers
   to wrap a single buffer in an array or span adds friction with no
   corresponding benefit. The `begin`/`end` overloads that return
   pointers to a single buffer eliminate this friction.

3. **Coroutine APIs must accept buffer sequences by value.** Reference
   parameters dangle across suspension points. This is a hard
   constraint of C++20 coroutines, not a design preference.

4. **DynamicBuffer lifetime enforcement belongs in the type system.**
   A compile-time error for `flat_dynamic_buffer` passed as an rvalue
   to a coroutine is strictly better than silent data loss at runtime.

5. **Both buffer ownership models are necessary.** Caller-owns is
   natural for stream I/O. Callee-owns is natural for layered
   protocols. Neither subsumes the other.

6. **Customization should be non-intrusive.** Third-party buffer types
   must be able to opt into optimized `buffer_size` and slicing without
   modifying their class definitions.

## Areas of Disagreement

1. **Whether `tag_invoke` is the right customization mechanism.** The
   P2300 ecosystem uses `tag_invoke` extensively, but WG21 has moved
   toward `tag_invoke`'s successor proposals. The design could be
   updated to use a newer mechanism without changing the conceptual
   model.

2. **Whether two concepts (`DynamicBuffer` and `DynamicBufferParam`)
   are acceptable complexity.** One view holds that the compile-time
   safety justifies the additional concept. The other holds that a
   single concept with clear documentation is sufficient, and that
   the adapter tag is an implementation detail that leaks into the
   concept definition.

3. **Whether `BufferSink` should use synchronous or asynchronous
   `prepare`.** The current design makes `prepare` synchronous (it
   returns a span immediately) and `commit` asynchronous. An
   alternative makes both asynchronous, allowing the sink to wait for
   internal buffer space. The synchronous design was chosen because
   `prepare` is a memory operation (provide a pointer), not an I/O
   operation, and back-pressure is signaled by returning an empty span
   rather than by suspending.

## Summary

| Decision                | Chosen Design               | Alternative                     | Rationale                                              |
| ----------------------- | --------------------------- | ------------------------------- | ------------------------------------------------------ |
| Buffer representation   | Non-owning pointer-size     | Owning buffer, span             | Zero overhead, matches OS model, 25 years of stability |
| Buffer sequence concept | Convertible-or-range        | Iterator pair, span only        | Single buffers compose naturally                       |
| Customization           | tag_invoke with tag types   | Virtual dispatch, concept overload | Non-intrusive, composable, no runtime cost          |
| DynamicBuffer lifetime  | DynamicBufferParam concept  | Unconstrained, lvalue-only      | Compile-time enforcement of coroutine safety           |
| Buffer ownership        | Both caller-owns and callee-owns | Single model              | Neither subsumes the other                             |
| Windowed access         | buffer_param sliding window | Flatten to contiguous           | Zero allocation, matches OS batch size                 |
| Slicing                 | In-place via tag_invoke + slice_of fallback | Always copy  | Types that track size can slice in O(1)                |

The buffer subsystem is designed around a single principle: buffers
describe memory, they do not own it. This principle propagates through
every layer - from the two-word primitive types, through the sequence
concepts that treat single buffers and ranges uniformly, to the
DynamicBuffer adapters that reference external storage, to the
BufferSource and BufferSink concepts that structure data transfer
without dictating who provides the memory. The coroutine lifetime
constraint (pass by value) and the DynamicBufferParam concept are
consequences of this principle: when memory is not owned by the buffer
descriptor, lifetime must be managed explicitly, and the type system
should enforce correct management at compile time.
