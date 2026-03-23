//
// Copyright (c) 2026 Steve Gerbino
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_BENCH_SENDER_STREAM_BASE_HPP
#define BOOST_CAPY_BENCH_SENDER_STREAM_BASE_HPP

#include "sender_any_sender.hpp"

#include <boost/capy/buffers.hpp>

/// Virtual base class for type-erased sender streams.
struct sender_stream_base
{
    virtual any_read_sender
        read_some(boost::capy::mutable_buffer) = 0;
    virtual ~sender_stream_base() = default;
};

#endif
