//
// Copyright (c) 2025 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_BUFFERS_ANY_STREAM_HPP
#define BOOST_CAPY_BUFFERS_ANY_STREAM_HPP

#include <boost/capy/detail/config.hpp>

#if 0

#include <boost/capy/buffers.hpp>
#include <boost/capy/async_op.hpp>
#include <boost/system/error_code.hpp>
#include <cstdint>
#include <memory>

namespace boost {
namespace capy {
namespace buffers {

/** Result of an asynchronous I/O operation.
*/
struct io_result
{
    /** The error code, if any occurred.
    */
    system::error_code ec;

    /** Number of bytes transferred.
    */
    std::size_t bytes_transferred;
};

/** Asynchronous result type for I/O operations.
*/
using async_io_result = capy::async_op<io_result>;

class any_stream
{
public:
    struct impl
    {
        virtual ~impl() = default;

        virtual auto read_some(
            mutable_buffer) ->
                async_io_result = 0;

        virtual auto write_some(
            const_buffer) ->
                async_io_result = 0;
    };

    /** Constructor
        @param p A pointer to the stream implementation.
    */
    any_stream(
        std::shared_ptr<impl> p) noexcept
        : impl_(std::move(p))
    {
    }

    /** Read some data into the buffer.

        This coroutine reads some data into the buffer
        and returns the number of bytes read.

        @param b The buffer to read into.
        @return The asynchronous result.
    */
    auto read_some(
        mutable_buffer b) ->
            async_io_result
    {
        return impl_->read_some(b);
    }

    /** Write some data from the buffer.
        This coroutine writes some data from the buffer
        and returns the number of bytes written.
        @param b The buffer to write from.
        @return The asynchronous result.
    */
    auto write_some(
        const_buffer b) ->
            async_io_result
    {
        return impl_->write_some(b);
    }

private:
    std::shared_ptr<impl> impl_;
};

} // buffers
} // capy
} // boost

#endif

#endif
