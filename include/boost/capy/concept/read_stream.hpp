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
#include <boost/capy/ex/executor_ref.hpp>
#include <boost/capy/buffers.hpp>
#include <boost/capy/concept/io_awaitable.hpp>
#include <boost/system/error_code.hpp>

#include <concepts>
#include <cstddef>
#include <utility>

namespace boost {
namespace capy {

/** Concept for types that provide awaitable read operations.

    A type satisfies `ReadStream` if it provides an I/O awaitable
    `read_some` member function that reads data into a mutable
    buffer sequence.

    @tparam T The stream type.
    @tparam MB The buffer sequence type, must satisfy
        `MutableBufferSequence`.

    @par Requirements
    @li `MB` must satisfy `MutableBufferSequence`
    @li `T` must provide a templated `read_some` member function
    @li `read_some` must accept a `MB const&`
    @li The awaitable returned by `read_some` must satisfy
        `capy::IoAwaitable<capy::executor_ref>`
    @li The awaitable must resolve to `std::pair<system::error_code, std::size_t>`
    @li When end-of-file is reached, `read_some` must return
        `capy::error::eof` as the error code. Check `ec == cond::eof`
        for portable comparison.

    @par Example
    @code
    template<ReadStream<mutable_buffer> Stream>
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
template<typename T, typename MB>
concept ReadStream =
    MutableBufferSequence<MB> &&
    requires(T& stream, MB const& buffers)
    {
        { stream.read_some(buffers) } ->
            capy::IoAwaitable<capy::executor_ref>;
    };

} // namespace capy
} // namespace boost

#endif
