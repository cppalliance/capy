//
// Copyright (c) 2026 Steve Gerbino
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_BENCH_SNDR_IO_READ_STREAM_HPP
#define BOOST_CAPY_BENCH_SNDR_IO_READ_STREAM_HPP

#include "sndr_any_read_sender.hpp"
#include "sndr_read_stream.hpp"

#include <boost/capy/buffers.hpp>

/// Abstract interface for sender-based read streams.
struct sndr_io_read_stream
{
    virtual sndr_any_read_sender
        read_some(boost::capy::mutable_buffer) = 0;
    virtual ~sndr_io_read_stream() = default;
};

/// Concrete implementation wrapping sndr_read_stream.
struct sndr_io_read_stream_impl : sndr_io_read_stream
{
    sndr_read_stream stream_;

    explicit sndr_io_read_stream_impl(sender_executor ex)
        : stream_{ex} {}

    sndr_any_read_sender
        read_some(boost::capy::mutable_buffer buf) override
    {
        return sndr_any_read_sender{stream_.read_some(buf)};
    }
};

#endif
