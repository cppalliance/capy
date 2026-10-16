//
// Copyright (c) 2026 Steve Gerbino
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_BENCH_STDEXEC_CTXAW_IO_READ_STREAM_HPP
#define BOOST_CAPY_BENCH_STDEXEC_CTXAW_IO_READ_STREAM_HPP

#include "ctxaw_read_stream.hpp"

/// Abstract interface for ContextAwaitable read streams.
struct ctxaw_io_read_stream
{
    virtual ctxaw_read_stream::read_awaitable
        read_some(boost::capy::mutable_buffer) = 0;
    virtual ~ctxaw_io_read_stream() = default;
};

/// Concrete implementation of ctxaw_io_read_stream wrapping
/// an ctxaw_read_stream.
struct ctxaw_io_read_stream_impl : ctxaw_io_read_stream
{
    ctxaw_read_stream stream_;

    ctxaw_read_stream::read_awaitable
        read_some(boost::capy::mutable_buffer buf) override
    {
        return stream_.read_some(buf);
    }
};

#endif
