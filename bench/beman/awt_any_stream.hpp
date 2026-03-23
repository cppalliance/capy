//
// Copyright (c) 2026 Steve Gerbino
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_BENCH_AWT_ANY_STREAM_HPP
#define BOOST_CAPY_BENCH_AWT_ANY_STREAM_HPP

#include "awt_stream_base.hpp"

/// Type-erased plain awaitable stream. Delegates to awt_stream
/// through the awt_stream_base virtual interface.
template <class Ex>
struct awt_any_stream : awt_stream_base<Ex>
{
    awt_stream<Ex> stream_;

    explicit awt_any_stream(Ex ex) noexcept
        : stream_{ex} {}

    typename awt_stream<Ex>::read_awaitable
        read_some(boost::capy::mutable_buffer buf) override
    {
        return stream_.read_some(buf);
    }
};

#endif
