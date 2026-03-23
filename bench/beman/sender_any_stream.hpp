//
// Copyright (c) 2026 Steve Gerbino
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_BENCH_SENDER_ANY_STREAM_HPP
#define BOOST_CAPY_BENCH_SENDER_ANY_STREAM_HPP

#include "sender_stream_base.hpp"

/// Type-erased sender stream. Wraps a concrete sender stream
/// behind sender_stream_base. Each read_some constructs the
/// concrete sender and wraps it in any_read_sender, which
/// heap-allocates on connect.
template <class Stream>
struct sender_any_stream : sender_stream_base
{
    Stream stream_;

    template <class... Args>
    explicit sender_any_stream(Args&&... args)
        : stream_{std::forward<Args>(args)...} {}

    any_read_sender
        read_some(boost::capy::mutable_buffer buf) override
    {
        return any_read_sender{stream_.read_some(buf)};
    }
};

#endif
