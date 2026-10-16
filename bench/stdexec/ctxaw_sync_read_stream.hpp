//
// Copyright (c) 2026 Steve Gerbino
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

//
// Synchronous-completion ContextAwaitable stream.
//
// Every read completes immediately via symmetric
// transfer — await_suspend returns the coroutine
// handle, causing an inline resume with no scheduler
// round-trip.
//

#ifndef BOOST_CAPY_BENCH_STDEXEC_CTXAW_SYNC_READ_STREAM_HPP
#define BOOST_CAPY_BENCH_STDEXEC_CTXAW_SYNC_READ_STREAM_HPP

#include <boost/capy/ex/await_context.hpp>
#include <boost/capy/results.hpp>

#include <coroutine>
#include <cstddef>

struct ctxaw_sync_read_stream
{
    struct read_awaitable
    {
        bool await_ready() const noexcept
        {
            return false;
        }

        std::coroutine_handle<>
        await_suspend(
            std::coroutine_handle<> h,
            boost::capy::await_context const*)
        {
            // Data already buffered — resume inline
            return h;
        }

        boost::capy::results<std::size_t>
        await_resume() noexcept
        {
            return {std::error_code(), 0};
        }
    };

    read_awaitable read_some(auto)
    {
        return {};
    }
};

#endif
