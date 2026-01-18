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
#include <boost/capy/ex/any_executor_ref.hpp>
#include <boost/capy/buffers.hpp>
#include <boost/capy/concept/io_awaitable.hpp>
#include <boost/system/error_code.hpp>

#include <concepts>
#include <cstddef>
#include <utility>

namespace boost {
namespace capy {

/** Concept for types that provide awaitable write operations.

    A type satisfies `WriteStream` if it provides an I/O awaitable
    `write_some` member function that writes data from a const
    buffer sequence.

    @tparam T The stream type.
    @tparam CB The buffer sequence type, must satisfy
        `ConstBufferSequence`.

    @par Requirements
    @li `CB` must satisfy `ConstBufferSequence`
    @li `T` must provide a templated `write_some` member function
    @li `write_some` must accept a `CB const&`
    @li The awaitable returned by `write_some` must satisfy
        `capy::IoAwaitable<capy::any_executor_ref>`
    @li The awaitable must resolve to `std::pair<system::error_code, std::size_t>`

    @par Example
    @code
    template<WriteStream<const_buffer> Stream>
    capy::task<void> write_all(Stream& s, char const* buf, std::size_t size)
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
*/
template<typename T, typename CB>
concept WriteStream =
    ConstBufferSequence<CB> &&
    requires(T& stream, CB const& buffers)
    {
        { stream.write_some(buffers) } ->
            capy::IoAwaitable<capy::any_executor_ref>;
    };

} // namespace capy
} // namespace boost

#endif
