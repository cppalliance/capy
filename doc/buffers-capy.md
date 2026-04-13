# Boost.Capy Buffer System - Technical Report

## 1. General Principle

Capy's buffer model descends directly from Boost.Asio's Networking TS design (N4771). The central insight: **I/O buffers are not byte ranges - they are memory region descriptors**. A single buffer is a `(void*, size_t)` pair that describes a contiguous memory region without owning it and without making semantic claims about its contents (unlike `std::span<std::byte>`). A *buffer sequence* is a bidirectional range of such descriptors, enabling scatter/gather I/O to map directly onto OS primitives like POSIX `writev`/`readv`.

The design differs from raw ranges in three critical ways:

- **`buffer_size` vs `ranges::size`** - `ranges::size` on `array<const_buffer, 3>` returns 3 (count of descriptors). `buffer_size` returns the sum of all bytes across all descriptors. I/O code needs total bytes, not descriptor count.
- **Element shrinking** - Range algorithms drop whole elements. Buffer algorithms shrink individual elements (advance a pointer, reduce a size) to model partial consumption.
- **Zero-allocation composition** - Concrete types like `span<span<byte>>` require allocation to concatenate. Buffer sequences compose at compile time through concept-constrained templates.

---

## 2. Foundation Types

### `mutable_buffer`

A non-owning reference to a writable memory region.

- **Internal state**: `unsigned char* p_` and `std::size_t n_`
- **Construction**: from `(void*, size_t)` - stores the pointer as `unsigned char*`
- **API**: `data()` returns `void*`, `size()` returns byte count
- **`operator+=`**: advances `p_` forward by `n` bytes (clamped to `n_`), shrinking the region - the fundamental "consume from front" operation
- **Slice CPO**: `tag_invoke(slice_tag, mutable_buffer&, slice_how, size_t)` dispatches to `remove_prefix` (advance) or `keep_prefix` (truncate)

### `const_buffer`

A non-owning reference to a read-only memory region. Structurally identical to `mutable_buffer` but stores `unsigned char const*`.

- **Implicit conversion from `mutable_buffer`**: enables any function accepting `ConstBufferSequence` to work with mutable buffers
- Same `operator+=` and `tag_invoke(slice_tag)` semantics

### Key design choice: `void*` not `std::byte*`

The types use `void*`/`void const*` in their public API (`data()` returns), matching POSIX `iovec` semantics. This makes no semantic claim about buffer contents - the memory could hold characters, integers, protocol frames, or raw binary. `std::byte` would impose the opinion that the contents are "bytes" and support bitwise operations, which is not always the right abstraction for I/O.

---

## 3. Buffer Sequence Concepts

### `ConstBufferSequence`

```cpp
template<typename T>
concept ConstBufferSequence =
    std::is_convertible_v<T, const_buffer> || (
        std::ranges::bidirectional_range<T> &&
        std::is_convertible_v<std::ranges::range_value_t<T>, const_buffer>);
```

Two satisfaction paths:

1. **Single buffer**: the type itself converts to `const_buffer` (e.g., `const_buffer`, `mutable_buffer`)
2. **Range of buffers**: a bidirectional range whose elements convert to `const_buffer` (e.g., `std::array<const_buffer, N>`, `std::vector<const_buffer>`)

### `MutableBufferSequence`

Same structure, but elements must convert to `mutable_buffer`. Every `MutableBufferSequence` is also a `ConstBufferSequence` (because `mutable_buffer` converts to `const_buffer`).

### Uniform iteration: `begin()` / `end()` CPOs

Customization point objects that handle both cases uniformly:

- **Single buffer** (convertible to `const_buffer`): returns `&b` / `&b + 1`, treating it as a one-element sequence
- **Range**: delegates to `std::ranges::begin` / `std::ranges::end`

This allows all buffer algorithms to iterate uniformly regardless of whether the input is a single buffer or a multi-buffer sequence.

---

## 4. Customization Protocol: `tag_invoke`

Capy uses two tag types for customization:

### `size_tag` - customizing `buffer_size`

The default `tag_invoke(size_tag, ...)` iterates all buffers and sums their sizes - O(n). User types can overload for O(1):

```cpp
std::size_t tag_invoke(size_tag const&, my_type const& x) noexcept {
    return x.cached_size_;
}
```

`buffer_array` does exactly this - it caches `size_` and returns it in O(1).

### `slice_tag` + `slice_how` - customizing slicing

A single overload handles both `remove_prefix` and `keep_prefix` via the `slice_how` enum. This forces types to implement both operations or neither, preventing irregular APIs. The free functions `keep_prefix`, `remove_prefix`, `keep_suffix`, `remove_suffix` (and their non-mutating counterparts `prefix`, `sans_prefix`, `suffix`, `sans_suffix`) all dispatch through `tag_invoke(slice_tag, ...)`.

The `slice_type<T>` alias selects between:

- `T` itself (if `T` has a `tag_invoke(slice_tag)` overload)
- `slice_of<T>` (a general-purpose wrapper that tracks byte offsets into an arbitrary sequence)

---

## 5. Buffer Algorithms

### `buffer_size`

Sums `size()` across all buffers. Dispatches through `tag_invoke(size_tag)`.

### `buffer_empty`

Short-circuits on the first non-zero-size buffer. More efficient than `buffer_size() == 0` for large sequences.

### `buffer_length`

Returns the count of buffer descriptors (not bytes). Uses random-access subtraction when possible, linear counting otherwise.

### `buffer_copy`

The workhorse algorithm. Copies bytes from a `ConstBufferSequence` source to a `MutableBufferSequence` destination, handling the scatter/gather complexity of iterating through discontiguous regions. Uses `memcpy` on each contiguous chunk - not byte-by-byte iteration. Accepts an optional `at_most` parameter. Returns total bytes copied.

The implementation maintains dual iterators (`it0`/`it1`) and position trackers (`pos0`/`pos1`) to handle partial buffer consumption at both source and destination boundaries.

### `front`

Returns the first buffer in a sequence, or an empty buffer if the sequence is empty. Preserves mutability.

### Slice operations

Eight operations in two groups:

**In-place mutating** (require `tag_invoke(slice_tag)` on the type):

- `keep_prefix(bs, n)` - trim to first n bytes
- `keep_suffix(bs, n)` - trim to last n bytes (computed via `remove_prefix(size - n)`)
- `remove_prefix(bs, n)` - drop first n bytes
- `remove_suffix(bs, n)` - drop last n bytes (computed via `keep_prefix(size - n)`)

**Non-mutating** (return a new value, wrapping in `slice_of<T>` if needed):

- `prefix(bs, n)` - copy, then `keep_prefix`
- `suffix(bs, n)` - copy, then `keep_suffix`
- `sans_prefix(bs, n)` - copy, then `remove_prefix`
- `sans_suffix(bs, n)` - copy, then `remove_suffix`

### `slice_of<BufferSequence>`

A general-purpose view over a sub-range of any buffer sequence. Stores the original sequence by value plus `begin_`, `end_` indices, `prefix_` and `suffix_` byte offsets, and `size_`. Its `const_iterator::operator*` adjusts the first and last buffer elements for the prefix/suffix byte offsets. This is the fallback when a type does not provide its own `tag_invoke(slice_tag)`.

---

## 6. Concrete Buffer Containers

### `buffer_pair`

Simple type aliases:

- `const_buffer_pair = std::array<const_buffer, 2>`
- `mutable_buffer_pair = std::array<mutable_buffer, 2>`

With custom `tag_invoke(slice_tag)` overloads that can shrink individual buffers within the pair. Used by `circular_dynamic_buffer` whose data/prepare may span two discontiguous regions.

### `buffer_array<N, IsConst>`

A fixed-capacity array holding 0 to N buffer descriptors. Key features:

- **Union-based storage** with placement new - avoids default-constructing unused slots
- **Filters empty buffers** during construction - never stores zero-size descriptors
- **Cached `size_`** for O(1) `buffer_size` via `tag_invoke(size_tag)`
- **Two construction modes**: silent truncation (drops excess buffers) vs `std::in_place_t` (throws `length_error`)
- **Span conversion** - implicit conversion to `std::span<value_type>`
- **Custom slicing** via `tag_invoke(slice_tag)` - delegates to compiled `.cpp` helper functions

Aliases: `const_buffer_array<N>` and `mutable_buffer_array<N>`.

### `make_buffer`

Factory function with overloads for every common container type. Returns `mutable_buffer` for mutable inputs, `const_buffer` for const inputs. Each overload has a variant with a `max_size` clamp. Supported types:

- Raw `void*` / `void const*` + size
- C arrays `T[N]`
- `std::array<T, N>`
- `std::vector<T, Alloc>` (requires `is_trivially_copyable<T>`)
- `std::basic_string<CharT>` / `std::basic_string_view<CharT>`
- `std::span<T, Extent>` (requires `sizeof(T) == 1`)
- Any `contiguous_range` with trivially copyable elements (general fallback)

---

## 7. Buffer Sequence Wrappers

### `buffer_param<BS>`

A windowed iterator over large buffer sequences, designed for coroutine I/O loops. Maintains an internal array of up to `max_iovec_` (16) buffer descriptors, auto-refilling from the underlying sequence as windows are consumed.

**Critical design for coroutines**: The outer template function must accept the buffer sequence **by value** (not by reference). When a coroutine suspends, reference parameters may dangle. `buffer_param` takes `BS const&` internally but the caller's template captures the sequence into the coroutine frame by value.

API: `data()` returns the current `span<buffer_type>` window (auto-refills if exhausted), `consume(n)` advances by n bytes, `more()` checks if additional buffers remain.

**Virtual interface pattern**: enables passing arbitrary buffer sequences through a virtual function boundary. The template function drives iteration; the virtual function receives a simple `span<const_buffer>`.

```cpp
class base
{
public:
    task<> write(ConstBufferSequence auto buffers)
    {
        buffer_param bp(buffers);
        while(true)
        {
            auto bufs = bp.data();
            if(bufs.empty())
                break;
            std::size_t n = 0;
            co_await write_impl(bufs, n);
            bp.consume(n);
        }
    }

protected:
    virtual task<> write_impl(
        std::span<const_buffer> buffers,
        std::size_t& bytes_written) = 0;
};
```

### `consuming_buffers<BufferSequence>`

Wraps a buffer sequence and tracks consumption progress. Stores a reference to the original sequence plus iterator position and byte offset within the current buffer. Its `const_iterator::operator*` adjusts the first buffer for consumed bytes. Simpler than `buffer_param` but references the original sequence rather than copying descriptors.

### `const_buffer_param<BS>`

Alias for `buffer_param<BS, true>` - always produces `const_buffer` regardless of input mutability.

---

## 8. Dynamic Buffers

### The `DynamicBuffer` concept

Models a two-phase write protocol:

1. `prepare(n)` - returns a `MutableBufferSequence` of n writable bytes
2. `commit(n)` - makes the first n prepared bytes readable via `data()`
3. `data()` - returns a `ConstBufferSequence` of readable bytes
4. `consume(n)` - discards n bytes from the front of readable data
5. `size()`, `max_size()`, `capacity()` - bookkeeping queries

Required nested types: `const_buffers_type` and `mutable_buffers_type`.

### Value Types vs Adapter Types

Capy distinguishes two categories:

- **Value types** (e.g., `flat_dynamic_buffer`) - store bookkeeping internally. Passing as rvalue to a coroutine loses state on suspend. Must be passed by lvalue reference.
- **Adapter types** - wrap external storage (`std::string*`, `std::vector*`). Define `using is_dynamic_buffer_adapter = void`. Safe as rvalues because the external object retains the data.

### `DynamicBufferParam` concept

Enforces safe passing in coroutines: accepts lvalues of any `DynamicBuffer`, but rvalues only for types tagged with `is_dynamic_buffer_adapter`. This prevents silent data loss from passing value-type dynamic buffers by rvalue into coroutines.

### Implementations

| Type | Backing Storage | `const_buffers_type` | `mutable_buffers_type` | Growth | Adapter? |
|------|----------------|---------------------|----------------------|--------|----------|
| `flat_dynamic_buffer` | External `void*` + capacity | `const_buffer` | `mutable_buffer` | Fixed capacity | Yes |
| `circular_dynamic_buffer` | External `void*` + capacity | `const_buffer_pair` | `mutable_buffer_pair` | Fixed capacity | Yes |
| `basic_string_dynamic_buffer` | `std::string*` | `const_buffer` | `mutable_buffer` | Grows via string | Yes |
| `basic_vector_dynamic_buffer` | `std::vector*` | `const_buffer` | `mutable_buffer` | Grows via vector | Yes |

**`flat_dynamic_buffer`**: Linear buffer. `prepare`/`data` return single-element sequences (always contiguous). `consume` advances `in_pos_` without moving data. Fixed capacity set at construction.

**`circular_dynamic_buffer`**: Ring buffer. Data can wrap around, so `data()` and `prepare()` may return a `buffer_pair` (two discontiguous regions). Efficient for FIFO patterns - `consume` never moves data, just advances the read pointer modulo capacity.

**`string_dynamic_buffer` / `vector_dynamic_buffer`**: Adapters over `std::string*` / `std::vector<unsigned char>*`. Can grow dynamically. `consume` uses `erase` from the front (O(n) data movement). Factory function `dynamic_buffer(s)` / `dynamic_buffer(v)` creates the adapter.

---

## 9. Asio Interoperability

Provides bidirectional conversion between Capy and Boost.Asio buffer types via `buffers/asio.hpp`:

- **`to_asio(bs)`** - wraps a Capy buffer sequence so its iterators yield `asio::mutable_buffer` or `asio::const_buffer`
- **`from_asio(bs)`** - wraps an Asio buffer sequence so its iterators yield `capy::mutable_buffer` or `capy::const_buffer`

The internal `buffer_sequence_adaptor<BufferSequence, IsMutable>` class template detects which library the source belongs to (via `is_native_asio_v`) and maps each dereference to the other library's buffer type. Supports random-access iteration when the source provides it.

---

## 10. Higher-Level Buffer Concepts

### `BufferSource`

The "callee owns buffers" read-side concept. A source provides:

- `pull(span<const_buffer>)` - async, fills the span with descriptors pointing to the source's internal storage, returns `(error_code, span<const_buffer>)`
- `consume(n)` - advances the read position by n bytes

Models a streaming producer. EOF is signaled via `cond::eof` error code.

### `BufferSink`

The "callee owns buffers" write-side concept. A sink provides:

- `prepare(span<mutable_buffer>)` - synchronous, fills the span with writable buffers from the sink's internal storage
- `commit(n)` - async, finalizes n written bytes
- `commit_eof(n)` - async, finalizes n bytes and signals end of stream

Together, `BufferSource` and `BufferSink` enable zero-copy transfer: the source exposes its internal memory, the sink exposes its internal memory, and `buffer_copy` bridges them without intermediate allocations.

---

## 11. Constants and Configuration

- `detail::max_iovec_ = 16` - maximum buffer descriptors per `buffer_param` window. Matches common OS limits for scatter/gather (`UIO_MAXIOV` on Linux is 1024, but 16 is a practical batch size to balance setup cost vs I/O throughput).
