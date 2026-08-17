//
// Copyright (c) 2026 Steve Gerbino
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_BENCH_IOAW_READ_STREAM_HPP
#define BOOST_CAPY_BENCH_IOAW_READ_STREAM_HPP

#include <boost/capy/buffers.hpp>
#include <boost/capy/concept/read_stream.hpp>
#include <boost/capy/continuation.hpp>
#include <boost/capy/ex/io_env.hpp>
#include <boost/capy/io_result.hpp>
#include <coroutine>
#include <cstddef>

/// No-op ReadStream for benchmarking.
///
/// Uses the executor from io_env (passed by capy::task's
/// transform_awaiter) to post the coroutine back. Satisfies
/// ReadStream so it can be wrapped by capy::any_read_stream.
struct ioaw_read_stream
{
    struct read_awaitable
    {
        boost::capy::continuation cont_{};

        bool await_ready() const noexcept { return false; }

        std::coroutine_handle<>
        await_suspend(
            std::coroutine_handle<> h,
            boost::capy::io_env const* env)
        {
            cont_.h = h;
            env->executor.post(cont_);
            return std::noop_coroutine();
        }

        boost::capy::io_result<std::size_t>
        await_resume() noexcept { return {std::error_code(), 0}; }
    };

    template <boost::capy::MutableBufferSequence MB>
    read_awaitable read_some(MB)
    {
        return {};
    }
};

static_assert(boost::capy::ReadStream<ioaw_read_stream>);

#endif
