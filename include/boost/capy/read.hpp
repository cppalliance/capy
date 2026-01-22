//
// Copyright (c) 2025 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_READ_HPP
#define BOOST_CAPY_READ_HPP

#include <boost/capy/detail/config.hpp>
#include <boost/capy/buffers.hpp>
#include <boost/capy/concept/read_stream.hpp>
#include <boost/capy/task.hpp>
#include <boost/system/error_code.hpp>

#include <cstddef>
#include <utility>

namespace boost {
namespace capy {

/** Read data until the buffer sequence is full or an error occurs.

    This function reads data from the stream into the buffer sequence
    until either the entire buffer sequence is filled or an error
    occurs (including end-of-file).

    @tparam Stream The stream type, must satisfy @ref ReadStream.
    @tparam MB The buffer sequence type, must satisfy
        @ref MutableBufferSequence.

    @param stream The stream to read from.
    @param buffers The buffer sequence to read into.

    @return A task that yields `(system::error_code, std::size_t)`.
        On success, `ec` is default-constructed (no error) and `n` is
        `buffer_size(buffers)`. On error or EOF, `ec` contains the
        error code and `n` is the total number of bytes written before
        the error.

    @par Example
    @code
    task<void> example(ReadStream auto& stream)
    {
        char buf[1024];
        auto [ec, n] = co_await read(stream, mutable_buffer(buf, sizeof(buf)));
        if (ec == cond::eof)
        {
            // Handle end-of-file
        }
        else if (ec)
        {
            // Handle other error
        }
        // n bytes were read into buf
    }
    @endcode

    @see ReadStream, MutableBufferSequence
*/
auto
read(
    ReadStream auto& stream,
    MutableBufferSequence const& buffers) ->
        task<std::pair<system::error_code, std::size_t>>
{
    std::size_t total = 0;
    auto it = begin(buffers);
    auto const last = end(buffers);

    while(it != last)
    {
        mutable_buffer buf = *it;
        while(buf.size() > 0)
        {
            auto [ec, n] = co_await stream.read_some(buf);
            total += n;
            if(ec)
                co_return {ec, total};
            buf += n;
        }
        ++it;
    }
    co_return {system::error_code{}, total};
}

} // namespace capy
} // namespace boost

#endif
