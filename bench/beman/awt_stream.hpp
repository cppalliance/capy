//
// Copyright (c) 2026 Steve Gerbino
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

//
// No-op plain awaitable stream for benchmarks.
//
// Uses the standard await_suspend(coroutine_handle<>) signature —
// no IoAwaitable, no io_env. Gets the executor at construction
// time, stored by value. Used by Column D's frame_cb sender
// pipeline.
//

#ifndef BOOST_CAPY_BENCH_AWT_STREAM_HPP
#define BOOST_CAPY_BENCH_AWT_STREAM_HPP

#include "thread_pool.hpp"

#include <coroutine>
#include <cstddef>

template <class Ex>
struct awt_stream
{
    Ex ex_;

    struct read_awaitable : work_item
    {
        Ex ex_;
        std::coroutine_handle<> h_{};

        explicit read_awaitable(Ex ex) noexcept : ex_(ex) {}

        bool await_ready() const noexcept { return false; }

        void await_suspend(std::coroutine_handle<> h)
        {
            h_ = h;
            ex_.enqueue(this);
        }

        std::size_t await_resume() noexcept { return 0; }

        void execute() noexcept override { h_.resume(); }
    };

    read_awaitable read_some(auto)
    {
        return read_awaitable{ex_};
    }
};

#endif
