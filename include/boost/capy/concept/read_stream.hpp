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
#include <boost/capy/any_dispatcher.hpp>
#include <boost/capy/buffers.hpp>
#include <boost/capy/concept/affine_awaitable.hpp>
#include <boost/system/error_code.hpp>

#include <concepts>
#include <cstddef>
#include <utility>

namespace boost {
namespace capy {

/** Concept for types that provide awaitable read operations.

    A type satisfies `read_stream` if it provides an affine awaitable
    `read_some` member function that reads data into a mutable
    buffer sequence.

    @tparam T The stream type.
    @tparam MutableBufferSequence The buffer sequence type, must satisfy
        `mutable_buffer_sequence`.

    @par Requirements
    @li `MutableBufferSequence` must satisfy `mutable_buffer_sequence`
    @li `T` must provide a templated `read_some` member function
    @li `read_some` must accept a `MutableBufferSequence const&`
    @li The awaitable returned by `read_some` must satisfy
        `capy::affine_awaitable<capy::any_dispatcher>`
    @li The awaitable must resolve to `std::pair<system::error_code, std::size_t>`
    @li When end-of-file is reached, `read_some` must return
        `capy::error::eof` as the error code

    @par Example
    @code
    template<read_stream<mutable_buffer> Stream>
    capy::task<void> read_all(Stream& s, char* buf, std::size_t size)
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
*/
template<typename T, typename MutableBufferSequence>
concept read_stream =
    mutable_buffer_sequence<MutableBufferSequence> &&
    requires(T& stream, MutableBufferSequence const& buffers)
    {
        { stream.read_some(buffers) } ->
            capy::affine_awaitable<capy::any_dispatcher>;
    };

} // namespace capy
} // namespace boost

#endif
