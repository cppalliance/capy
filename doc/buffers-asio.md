# Buffer Sequence Theory

This document explains Asio's buffer sequence abstraction - what it is, what rules govern it, and how users extend it with their own types.

## The Buffer Primitive

A buffer is a pointer and a size. It describes a contiguous region of memory without owning it.

Asio defines two buffer types:

- `mutable_buffer` - writable memory (`void*` + `size_t`)
- `const_buffer` - read-only memory (`const void*` + `size_t`)

Both expose two member functions: `data()` returns the pointer, `size()` returns the byte count.

`mutable_buffer` is implicitly convertible to `const_buffer` (writable memory can always be read). The reverse conversion is disallowed - you cannot write to read-only memory.

The pointer type is `void*`, not `std::byte*`. This is deliberate. POSIX uses `void*` in its I/O structures (`iovec`) for semantic neutrality - raw I/O should not opine on what the memory contains. The buffer types preserve this neutrality.

These types are non-owning descriptors. They reference memory but do not manage its lifetime. Creating a buffer from a pointer does not allocate, copy, or extend the life of anything. The caller is responsible for ensuring the memory remains valid while the buffer is in use.

## Why Sequences

Operating systems support scatter-gather I/O. A gather-write (`writev` on POSIX, scatter/gather with IOCP on Windows) transmits multiple buffers in a single syscall. A scatter-read (`readv`) receives data into multiple buffers at once.

This is important for performance. Consider sending an HTTP response: the status line is in one buffer, each header in another, the body in yet another. Without scatter-gather, you must copy everything into a single contiguous allocation before writing. With scatter-gather, you pass all the buffers to one syscall and the kernel handles the rest.

A buffer sequence is the abstraction that represents this collection of buffers. It is the C++ type that maps to the array of `iovec` structures that the OS expects.

## The Abstraction

A buffer sequence is any type that produces a bidirectional iteration of buffers.

More precisely: a type `T` is a buffer sequence if the free functions `buffer_sequence_begin(t)` and `buffer_sequence_end(t)` return bidirectional iterators whose value type is convertible to `const_buffer` (for read operations) or `mutable_buffer` (for write-into operations).

### Customization Points

`buffer_sequence_begin` and `buffer_sequence_end` are free functions that serve as customization points. For standard containers, Asio provides default overloads that call `begin()` and `end()`. For user-defined types, the user provides overloads found via ADL (argument-dependent lookup).

This is the same customization pattern used throughout Asio. The type's namespace determines which overload is found. Wrapping a buffer sequence in a type-erasing container (like stuffing it into a lambda or a `std::function`) destroys the type information that ADL needs, breaking the mechanism.

### Why Bidirectional

The iterators must be at least bidirectional - not merely forward. Two reasons:

1. Algorithms that consume buffer sequences sometimes need to traverse backwards. When removing a prefix from a buffer sequence (consuming bytes from the front after a partial read), the implementation may need to adjust the first unconsumed buffer.

2. A read or write operation fills or drains buffers in order, front to back. If the operation is interrupted partway through a buffer, the implementation needs to locate that buffer and adjust its starting position for the next call. Bidirectional iteration simplifies this bookkeeping.

Forward-only ranges do not satisfy the buffer sequence requirements.

### The Single-Buffer Case

A lone `const_buffer` or `mutable_buffer` is itself a valid buffer sequence - a sequence of exactly one element. Asio provides overloads of `buffer_sequence_begin` and `buffer_sequence_end` that return a pointer to the buffer and a pointer one past it, respectively. This makes a single buffer act like a one-element array.

This unification matters: any function that accepts a buffer sequence also accepts a single buffer. There is no need for separate overloads.

```cpp
template<ConstBufferSequence Buffers>
void send(const Buffers& buffers);

const_buffer single = ...;
send(single);                              // one buffer

std::array<const_buffer, 3> multiple = ...;
send(multiple);                            // three buffers
```

Both calls use the same function template. The concept is satisfied in both cases.

## The Formal Rules

A type `X` satisfies `ConstBufferSequence` if:

- `X` is `Destructible` and `CopyConstructible`
- `buffer_sequence_begin(x)` and `buffer_sequence_end(x)` return bidirectional iterators whose value type is convertible to `const_buffer`
- After copy construction `X u(x)`, the sequence of buffers in `u` is identical to the sequence in `x` - each corresponding buffer has the same `data()` pointer and the same `size()`

A type `X` satisfies `MutableBufferSequence` if the same rules hold with `mutable_buffer` in place of `const_buffer`.

Every `MutableBufferSequence` is automatically a `ConstBufferSequence`, because `mutable_buffer` converts to `const_buffer`. A function that accepts `ConstBufferSequence` will accept mutable buffer sequences without any additional work.

### The Copy Postcondition

The third rule deserves emphasis. After copying a buffer sequence, the copy must describe the exact same memory regions as the original. Same pointers. Same sizes. The copy is shallow - it duplicates the descriptors, not the bytes they point at.

This means a buffer sequence cannot own the memory it describes. If a type held an internal `std::string` and yielded a `const_buffer` pointing at that string's data, copying the type would copy the string to a new address. The copy's `data()` pointers would differ from the original's, violating the postcondition. Buffer sequences must reference externally-owned memory.

## What Already Satisfies the Requirements

Any standard bidirectional container of buffers works:

```cpp
std::array<const_buffer, 4> bufs;    // fixed-size, stack-allocated
std::vector<mutable_buffer> bufs;    // dynamic
std::list<const_buffer> bufs;        // linked, bidirectional
```

These types are `CopyConstructible`, their `begin()`/`end()` return bidirectional iterators, and their value types convert to the appropriate buffer type. Asio's default overloads of `buffer_sequence_begin`/`buffer_sequence_end` delegate to the container's own iterators.

A single `const_buffer` or `mutable_buffer` also satisfies the requirements, as described above.

A `std::forward_list<const_buffer>` does not qualify - its iterators are forward-only, not bidirectional.

## Writing Your Own Buffer Sequence

There are two ways to make a user-defined type satisfy the buffer sequence requirements.

### Provide begin() and end() Members

If your type behaves like a container - it has `begin()` and `end()` member functions returning bidirectional iterators over buffers - then Asio's default `buffer_sequence_begin`/`buffer_sequence_end` overloads will find them automatically:

```cpp
class header_buffers
{
    const_buffer bufs_[3];

public:
    header_buffers(
        const_buffer status_line,
        const_buffer headers,
        const_buffer separator)
        : bufs_{status_line, headers, separator}
    {
    }

    const const_buffer* begin() const { return bufs_; }
    const const_buffer* end() const { return bufs_ + 3; }
};
```

This type is `CopyConstructible` (the default copy copies the array of descriptors, preserving `data()` pointers and sizes). Its `begin()`/`end()` return pointers, which are random-access iterators (and therefore bidirectional). It satisfies `ConstBufferSequence`.

### Provide ADL Overloads

For types where `begin()`/`end()` members are not appropriate, provide free function overloads of `buffer_sequence_begin` and `buffer_sequence_end` in the same namespace as the type:

```cpp
namespace app {

class composite_buffers
{
    const_buffer bufs_[2];

public:
    composite_buffers(const_buffer head, const_buffer body)
        : bufs_{head, body}
    {
    }

    friend const const_buffer*
    buffer_sequence_begin(const composite_buffers& b)
    {
        return b.bufs_;
    }

    friend const const_buffer*
    buffer_sequence_end(const composite_buffers& b)
    {
        return b.bufs_ + 2;
    }
};

} // namespace app
```

ADL finds the friend functions when Asio calls `buffer_sequence_begin(x)` with an `app::composite_buffers` argument.

### A More Interesting Example

The real power of user-defined buffer sequences is lazy composition. Consider a type that concatenates two buffer sequences without allocating:

```cpp
template<class BS1, class BS2>
class buffers_cat
{
    BS1 bs1_;
    BS2 bs2_;

public:
    class const_iterator
    {
        // Bidirectional iterator that walks bs1_ first, then bs2_.
        // When it reaches the end of bs1_, it transitions to
        // the beginning of bs2_. Decrementing from the beginning
        // of bs2_ transitions back to the end of bs1_.
        // ...
    };

    buffers_cat(BS1 bs1, BS2 bs2)
        : bs1_(std::move(bs1))
        , bs2_(std::move(bs2))
    {
    }

    const_iterator begin() const;
    const_iterator end() const;
};
```

Iterating this type yields all buffers from the first sequence followed by all buffers from the second. No allocation occurs - the composed sequence is a view over the two sub-sequences. The resulting type satisfies `ConstBufferSequence` (assuming both sub-sequences do), and it can be passed directly to `async_write`.

This is the composition that concrete types like `span<span<byte>>` cannot provide without allocation.

## Ownership and Lifetime

Buffer sequences have a two-layer ownership model. The buffer sequence object (the descriptor) and the underlying memory (the bytes it points at) follow separate rules.

### The Implementation Copies the Sequence

When an asynchronous read or write operation is initiated, the implementation stores a copy of the buffer sequence inside its composed operation state. The Asio specification states:

> If a read or write operation is also an asynchronous operation, the operation shall maintain one or more copies of the buffer sequence until such time as the operation no longer requires access to the memory specified by the buffers in the sequence.

This is why `CopyConstructible` is a requirement. It is not an abstract nicety - the implementation literally copies the buffer sequence object into its internal state so it can re-use it across the multiple `async_read_some` or `async_write_some` calls that compose the full operation.

### The Caller Owns the Memory

The implementation copies the buffer sequence object, but it never copies the underlying bytes. The Asio documentation for `async_read` and `async_write` states:

> Although the buffers object may be copied as necessary, ownership of the underlying memory blocks is retained by the caller, which must guarantee that they remain valid until the completion handler is called.

More precisely, the memory must remain valid until:

- the last copy of the buffer sequence is destroyed, or
- the completion handler is invoked,

whichever comes first.

### What This Means in Practice

The buffer sequence is a view. It describes memory it does not own. The implementation copies the view. The caller owns the memory the view points at.

A common mistake: passing a buffer that references a local variable to an asynchronous operation, then returning from the function before the operation completes. The local variable is destroyed, the buffer's `data()` pointer dangles, and the operation reads or writes garbage.

```cpp
void bad_example(tcp::socket& sock)
{
    char buf[1024];
    // buf is on the stack - it will be destroyed when
    // this function returns, but the async operation
    // has not completed yet
    async_read(sock, mutable_buffer(buf, sizeof(buf)),
        [](error_code ec, std::size_t n) { /* ... */ });
}
```

The buffer sequence (a single `mutable_buffer`) is copied into the async operation's state - that copy is fine. But the memory at `buf` ceases to exist when `bad_example` returns. The operation proceeds to write into a destroyed stack frame.

The fix is to ensure the memory outlives the operation - allocate on the heap, use a member variable, or tie the buffer's lifetime to the completion handler via a shared pointer or similar mechanism.
