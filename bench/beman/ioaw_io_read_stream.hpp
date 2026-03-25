//
// Copyright (c) 2026 Steve Gerbino
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_BENCH_IOAW_IO_READ_STREAM_HPP
#define BOOST_CAPY_BENCH_IOAW_IO_READ_STREAM_HPP

#include "ioaw_read_stream.hpp"

/// Abstract interface for IoAwaitable read streams.
struct ioaw_io_read_stream
{
    virtual ioaw_read_stream::read_awaitable
        read_some(boost::capy::mutable_buffer) = 0;
    virtual ~ioaw_io_read_stream() = default;
};

/// Concrete implementation of ioaw_io_read_stream wrapping
/// an ioaw_read_stream.
struct ioaw_io_read_stream_impl : ioaw_io_read_stream
{
    ioaw_read_stream stream_;

    ioaw_read_stream::read_awaitable
        read_some(boost::capy::mutable_buffer buf) override
    {
        return stream_.read_some(buf);
    }
};

#endif
