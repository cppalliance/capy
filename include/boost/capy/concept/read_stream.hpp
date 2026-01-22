//
// Copyright (c) 2025 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_CONCEPT_READ_STREAM_HPP
#define BOOST_CAPY_CONCEPT_READ_STREAM_HPP

#include <boost/capy/detail/config.hpp>
#include <boost/capy/buffers.hpp>
#include <boost/capy/concept/io_awaitable.hpp>
#include <boost/capy/type_traits.hpp>
#include <boost/system/error_code.hpp>

#include <concepts>
#include <cstddef>

namespace boost {
namespace capy {

/** Archetype for MutableBufferSequence concept checking.

    This type satisfies @ref MutableBufferSequence but cannot be
    instantiated. Use it in concept definitions to verify that
    a function template accepts any MutableBufferSequence.

    @par Example
    @code
    template<typename T>
    concept MyReadable =
        requires(T& stream, mutable_buffer_archetype buffers)
        {
            stream.read(buffers);
        };
    @endcode
*/
struct mutable_buffer_archetype_
{
    mutable_buffer_archetype_() = delete;
    mutable_buffer_archetype_(mutable_buffer_archetype_ const&) = delete;
    mutable_buffer_archetype_(mutable_buffer_archetype_&&) = delete;
    mutable_buffer_archetype_& operator=(mutable_buffer_archetype_ const&) = delete;
    mutable_buffer_archetype_& operator=(mutable_buffer_archetype_&&) = delete;

    operator mutable_buffer() const noexcept { return {}; }
};

#ifdef __clang__
// workaround: archetype crashes clang
using mutable_buffer_archetype = mutable_buffer;
#else
using mutable_buffer_archetype = mutable_buffer_archetype_;
#endif

//------------------------------------------------

/** Concept for types that provide awaitable read operations.

    A type satisfies `ReadStream` if it provides a `read_some`
    member function template that accepts any @ref MutableBufferSequence
    and is an @ref IoAwaitable whose return value decomposes to
    `(system::error_code, std::size_t)`.

    @tparam T The stream type.

    @par Syntactic Requirements

    @li `T` must provide a `read_some` member function template
        accepting any @ref MutableBufferSequence
    @li The return type of `read_some` must satisfy @ref IoAwaitable
    @li The awaitable's result must decompose to
        `(system::error_code, std::size_t)` via structured bindings

    @par Semantic Requirements

    If `buffer_size(buffers) > 0`, the operation reads one or more
    bytes from the stream into the buffer sequence:

    @li On success: `!ec` is `true`, and `n` is the number of bytes
        read (at least 1).
    @li On error: `!!ec` is `true`, and `n` is 0.
    @li On end-of-file: `ec == cond::eof` is `true`, and `n` is 0.

    If `buffer_size(buffers) == 0`, the operation completes
    immediately. `!ec` is `true`, and `n` is 0.

    Buffers in the sequence are filled completely before proceeding
    to the next buffer.

    @par Buffer Lifetime

    The caller must ensure that the memory referenced by `buffers`
    remains valid until the `co_await` expression returns.

    @par Conforming Signatures

    @code
    // Templated for any MutableBufferSequence
    template<MutableBufferSequence MB>
    some_io_awaitable<std::pair<system::error_code, std::size_t>>
    read_some(MB const& buffers);
    @endcode

    @par Example

    @code
    template<ReadStream Stream>
    task<void> read_all(Stream& s, char* buf, std::size_t size)
    {
        std::size_t total = 0;
        while (total < size)
        {
            auto [ec, n] = co_await s.read_some(
                mutable_buffer(buf + total, size - total));
            if (ec)
                co_return;
            total += n;
        }
    }
    @endcode

    @see IoAwaitable, MutableBufferSequence, awaitable_decomposes_to
*/
template<typename T>
concept ReadStream =
    requires(T& stream, mutable_buffer_archetype buffers)
    {
        { stream.read_some(buffers) } -> IoAwaitable;
        requires awaitable_decomposes_to<
            decltype(stream.read_some(buffers)),
            system::error_code, std::size_t>;
    };

} // namespace capy
} // namespace boost

#endif
