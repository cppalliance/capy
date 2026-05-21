//
// Copyright (c) 2026 Steve Gerbino
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_BENCH_STDEXEC_SNDR_SYNC_READ_STREAM_HPP
#define BOOST_CAPY_BENCH_STDEXEC_SNDR_SYNC_READ_STREAM_HPP

#include <boost/capy/buffers.hpp>

#include <stdexec/execution.hpp>

#include <cstddef>

/// Synchronous no-op sender stream.
///
/// read_some returns just(0); no scheduler trip.
/// Used as the synchronous-baseline row in the bench.
struct sndr_sync_read_stream
{
    auto read_some(boost::capy::mutable_buffer)
    {
        return stdexec::just(std::size_t{0});
    }
};

#endif
