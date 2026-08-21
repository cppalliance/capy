//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
// Copyright (c) 2026 Michael Vandeberg
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_CONCEPT_READ_STREAM_HPP
#define BOOST_CAPY_CONCEPT_READ_STREAM_HPP

#include <boost/capy/detail/config.hpp>
#include <boost/capy/concept/buffer_archetype.hpp>
#include <boost/capy/concept/decomposes_to.hpp>
#include <boost/capy/concept/io_awaitable.hpp>
#include <system_error>

#include <concepts>
#include <cstddef>

namespace boost {
namespace capy {

/** Requires `read_some` to return an `IoAwaitable` decomposing to an error code and a size.

    A type satisfies `ReadStream` if it provides a `read_some`
    member function template that accepts any @ref MutableBufferSequence
    and await-returns `(error_code, std::size_t)`.

    @par Syntactic Requirements
    @li `T` must provide a `read_some` member function template
        accepting any @ref MutableBufferSequence.
    @li The return type of `read_some` must satisfy @ref IoAwaitable.
    @li The awaitable's result must decompose to
        `(error_code,std::size_t)` via structured bindings.

    @par Semantic Requirements
    Attempts to read up to `buffer_size( buffers )` bytes from
    the stream into the buffer sequence.

    If `buffer_size( buffers ) > 0`:

    @li If `!ec`, then `n >= 1 && n <= buffer_size( buffers )`.
        `n` bytes were read into the buffer sequence.
    @li If `ec`, then `n >= 0 && n < buffer_size( buffers )`.
        `n` is the number of bytes read before the I/O
        contingency arose.

    Equivalently, `n == buffer_size( buffers )` implies `!ec`. A
    completion that fills the buffer sequence is a success, even when
    the underlying operation also signals a contingency such as
    end-of-stream. That contingency is reported on a subsequent read.
    This lets generic composition algorithms such as `when_all` and
    `when_any` distinguish a completed transfer from a failure.

    If `buffer_empty( buffers )` is `true`, `n` is 0. The empty
    buffer is not itself a cause for error, but `ec` may reflect
    the state of the stream.

    Buffers in the sequence are filled in order.

    @par After an Error
    A subsequent `read_some` call is permitted. A conforming stream
    may report the same contingency, report a different one, or
    resume delivering data.

    @par Error Reporting
    I/O contingencies arising from the underlying I/O system are
    reported via the `error_code` component of the return value.
    Examples are EOF, connection reset, and broken pipe. Failures
    in the library wrapper itself (such as memory allocation
    failure) are reported via exceptions.

    @throws std::bad_alloc If coroutine frame allocation fails.

    @par Buffer Lifetime
    The caller must ensure that the memory referenced by `buffers`
    remains valid until the `co_await` expression returns.

    @par Conforming Signatures
    @par !example example_1


    @warning **Pass buffer sequences by value.** A by-value parameter
    is copied into the coroutine frame, or into the awaitable's state.
    The returned awaitable is therefore self-contained. A caller may
    store it, move it across threads, or wrap it into a sender without
    lifetime concerns. A by-const-reference parameter binds to caller
    storage. It is safe only when `co_await` consumes the awaitable
    immediately, in the same scope. Storing such an awaitable produces
    a dangling reference.

    @note Callers who want to avoid copying an expensive buffer
    sequence (for example, a `std::vector<mutable_buffer>` with many
    entries) can pass `std::views::all(seq)` at the call site. The
    resulting `ref_view` satisfies the buffer-sequence concepts and
    copies in O(1). See `doc/buffers-passing-rationale.md`.

    @par Example
    @par !example example_2


    @see IoAwaitable, MutableBufferSequence, awaitable_decomposes_to
*/
template<typename T>
concept ReadStream =
    requires(T& stream, mutable_buffer_archetype buffers)
    {
        { stream.read_some(buffers) } -> IoAwaitable;
        requires awaitable_decomposes_to<
            decltype(stream.read_some(buffers)),
            std::error_code, std::size_t>;
    };

} // namespace capy
} // namespace boost

#endif
