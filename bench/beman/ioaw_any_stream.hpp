//
// Copyright (c) 2026 Steve Gerbino
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_BENCH_IOAW_ANY_STREAM_HPP
#define BOOST_CAPY_BENCH_IOAW_ANY_STREAM_HPP

#include "ioaw_stream_base.hpp"

/// Type-erased IoAwaitable stream. Delegates to ioaw_stream
/// through the ioaw_stream_base virtual interface.
struct ioaw_any_stream : ioaw_stream_base
{
    ioaw_stream stream_;

    ioaw_stream::read_awaitable
        read_some(boost::capy::mutable_buffer buf) override
    {
        return stream_.read_some(buf);
    }
};

#endif
