//
// Copyright (c) 2025 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_IO_PULL_FROM_HPP
#define BOOST_CAPY_IO_PULL_FROM_HPP

#include <boost/capy/detail/config.hpp>
#include <boost/capy/buffers.hpp>
#include <boost/capy/cond.hpp>
#include <boost/capy/concept/buffer_sink.hpp>
#include <boost/capy/concept/read_source.hpp>
#include <boost/capy/concept/read_stream.hpp>
#include <boost/capy/io_result.hpp>
#include <boost/capy/task.hpp>

#include <cstddef>
#include <span>

namespace boost {
namespace capy {

/** Transfer data from a ReadSource to a BufferSink.

    This function reads data from the source directly into the sink's
    internal buffers using the callee-owns-buffers model. The sink
    provides writable buffers via `prepare()`, the source reads into
    them, and the sink commits the data. When the source signals EOF,
    `commit_eof()` is called on the sink to finalize the transfer.

    @tparam Src The source type, must satisfy @ref ReadSource.
    @tparam Sink The sink type, must satisfy @ref BufferSink.

    @param source The source to read data from.
    @param sink The sink to write data to.

    @return A task that yields `(system::error_code, std::size_t)`.
        On success, `ec` is default-constructed (no error) and `n` is
        the total number of bytes transferred. On error, `ec` contains
        the error code and `n` is the total number of bytes transferred
        before the error.

    @par Example
    @code
    task<void> transfer_body(ReadSource auto& source, BufferSink auto& sink)
    {
        auto [ec, n] = co_await pull_from(source, sink);
        if (ec.failed())
        {
            // Handle error
        }
        // n bytes were transferred
    }
    @endcode

    @see ReadSource, BufferSink, push_to
*/
template<ReadSource Src, BufferSink Sink>
task<io_result<std::size_t>>
pull_from(Src& source, Sink& sink)
{
    mutable_buffer dst_arr[detail::max_iovec_];
    std::size_t total = 0;

    for(;;)
    {
        std::size_t dst_count = sink.prepare(dst_arr, detail::max_iovec_);
        if(dst_count == 0)
        {
            // No buffer space available; commit nothing to flush
            auto [flush_ec] = co_await sink.commit(0);
            if(flush_ec.failed())
                co_return {flush_ec, total};
            continue;
        }

        auto [ec, n] = co_await source.read(
            std::span<mutable_buffer const>(dst_arr, dst_count));

        if(n > 0)
        {
            auto [commit_ec] = co_await sink.commit(n);
            if(commit_ec.failed())
                co_return {commit_ec, total};
            total += n;
        }

        if(ec == cond::eof)
        {
            auto [eof_ec] = co_await sink.commit_eof();
            co_return {eof_ec, total};
        }

        if(ec.failed())
            co_return {ec, total};
    }
}

/** Transfer data from a ReadStream to a BufferSink.

    This function reads data from the stream directly into the sink's
    internal buffers using the callee-owns-buffers model. The sink
    provides writable buffers via `prepare()`, the stream reads into
    them using `read_some()`, and the sink commits the data. When the
    stream signals EOF, `commit_eof()` is called on the sink to
    finalize the transfer.

    This overload handles partial reads from the stream, committing
    data incrementally as it arrives. It loops until EOF is encountered
    or an error occurs.

    @tparam Src The source type, must satisfy @ref ReadStream.
    @tparam Sink The sink type, must satisfy @ref BufferSink.

    @param source The stream to read data from.
    @param sink The sink to write data to.

    @return A task that yields `(system::error_code, std::size_t)`.
        On success, `ec` is default-constructed (no error) and `n` is
        the total number of bytes transferred. On error, `ec` contains
        the error code and `n` is the total number of bytes transferred
        before the error.

    @par Example
    @code
    task<void> transfer_body(ReadStream auto& stream, BufferSink auto& sink)
    {
        auto [ec, n] = co_await pull_from(stream, sink);
        if (ec.failed())
        {
            // Handle error
        }
        // n bytes were transferred
    }
    @endcode

    @see ReadStream, BufferSink, push_to
*/
template<ReadStream Src, BufferSink Sink>
task<io_result<std::size_t>>
pull_from(Src& source, Sink& sink)
{
    mutable_buffer dst_arr[detail::max_iovec_];
    std::size_t total = 0;

    for(;;)
    {
        // Prepare destination buffers from the sink
        std::size_t dst_count = sink.prepare(dst_arr, detail::max_iovec_);
        if(dst_count == 0)
        {
            // No buffer space available; commit nothing to flush
            auto [flush_ec] = co_await sink.commit(0);
            if(flush_ec.failed())
                co_return {flush_ec, total};
            continue;
        }

        // Read data from the stream into the sink's buffers
        auto [ec, n] = co_await source.read_some(
            std::span<mutable_buffer const>(dst_arr, dst_count));

        // Commit any data that was read
        if(n > 0)
        {
            auto [commit_ec] = co_await sink.commit(n);
            if(commit_ec.failed())
                co_return {commit_ec, total};
            total += n;
        }

        // Check for EOF condition
        if(ec == cond::eof)
        {
            auto [eof_ec] = co_await sink.commit_eof();
            co_return {eof_ec, total};
        }

        // Check for other errors
        if(ec.failed())
            co_return {ec, total};
    }
}

} // namespace capy
} // namespace boost

#endif
