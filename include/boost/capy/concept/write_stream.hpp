//
// Copyright (c) 2025 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_CONCEPT_WRITE_STREAM_HPP
#define BOOST_CAPY_CONCEPT_WRITE_STREAM_HPP

#include <boost/capy/detail/config.hpp>
#include <boost/capy/concept/buffer_archetype.hpp>
#include <boost/capy/concept/decomposes_to.hpp>
#include <boost/capy/concept/io_awaitable.hpp>
#include <boost/system/error_code.hpp>

#include <concepts>
#include <cstddef>

namespace boost {
namespace capy {

/** Concept for types that provide awaitable write operations.

    A type satisfies `WriteStream` if it provides a `write_some`
    member function template that accepts any @ref ConstBufferSequence
    and is an @ref IoAwaitable whose return value decomposes to
    `(system::error_code, std::size_t)`.

    @tparam T The stream type.

    @par Syntactic Requirements

    @li `T` must provide a `write_some` member function template
        accepting any @ref ConstBufferSequence
    @li The return type of `write_some` must satisfy @ref IoAwaitable
    @li The awaitable's result must decompose to
        `(system::error_code, std::size_t)` via structured bindings

    @par Semantic Requirements

    If `buffer_size(buffers) > 0`, the operation writes one or more
    bytes of data to the stream from the buffer sequence:

    @li On success: `!ec` is `true`, and `n` is the number of bytes
        written.
    @li On error: `!!ec` is `true`, and `n` is 0.

    If `buffer_size(buffers) == 0`, the operation completes
    immediately. `!ec` is `true`, and `n` is 0.

    Buffers in the sequence are written completely before proceeding
    to the next buffer.

    @par Buffer Lifetime

    The caller must ensure that the memory referenced by `buffers`
    remains valid until the `co_await` expression returns.

    @par Conforming Signatures

    @code
    // Templated for any ConstBufferSequence
    template<ConstBufferSequence CB>
    some_io_awaitable<std::pair<system::error_code, std::size_t>>
    write_some(CB const& buffers);
    @endcode

    @par Example

    @code
    template<WriteStream Stream>
    task<void> write_all(Stream& s, char const* buf, std::size_t size)
    {
        std::size_t total = 0;
        while (total < size)
        {
            auto [ec, n] = co_await s.write_some(
                const_buffer(buf + total, size - total));
            if (ec)
                co_return;
            total += n;
        }
    }
    @endcode

    @see IoAwaitable, ConstBufferSequence, awaitable_decomposes_to
*/
template<typename T>
concept WriteStream =
    requires(T& stream, const_buffer_archetype buffers)
    {
        { stream.write_some(buffers) } -> IoAwaitable;
        requires awaitable_decomposes_to<
            decltype(stream.write_some(buffers)),
            system::error_code, std::size_t>;
    };

} // namespace capy
} // namespace boost

#endif
