//
// Copyright (c) 2026 Steve Gerbino
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_BENCH_STDEXEC_SNDR_READ_STREAM_HPP
#define BOOST_CAPY_BENCH_STDEXEC_SNDR_READ_STREAM_HPP

#include <boost/capy/buffers.hpp>

#include <exec/static_thread_pool.hpp>
#include <stdexec/execution.hpp>

#include <cstddef>

/// No-op sender stream for benchmarks.
///
/// Holds an exec::static_thread_pool* (analogous to how a
/// socket holds a reference to its execution context).
/// read_some() returns starts_on(sched, just(0)); the
/// sender is consumable by sender pipelines via connect
/// and by exec::task / capy::task via co_await.
struct sndr_read_stream
{
    exec::static_thread_pool* pool_;

    auto read_some(boost::capy::mutable_buffer)
    {
        return stdexec::starts_on(
            pool_->get_scheduler(),
            stdexec::just(std::size_t{0}));
    }
};

#endif
