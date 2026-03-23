//
// Copyright (c) 2026 Steve Gerbino
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

//
// No-op sender stream for benchmarks.
//
// Template parameter Ex is an executor type with enqueue(work_item*)
// (e.g. sender_executor). Stored by value — the stream holds a
// handle, not a context. The sender provides both as_awaitable
// (for coroutine consumption) and connect (for sender pipeline
// consumption via any_read_sender or D4126).
//

#ifndef BOOST_CAPY_BENCH_SENDER_STREAM_HPP
#define BOOST_CAPY_BENCH_SENDER_STREAM_HPP

#include "thread_pool.hpp"

#include <beman/execution/execution.hpp>

#include <coroutine>
#include <cstddef>
#include <type_traits>
#include <utility>

namespace ex = beman::execution;

template <class Ex>
struct sender_stream
{
    Ex ex_;

    struct read_sender
    {
        using sender_concept = ex::sender_t;
        using completion_signatures =
            ex::completion_signatures<ex::set_value_t(std::size_t)>;

        Ex ex_;

        // awaitable path (co_awaited from io_task via as_awaitable)
        template <typename Promise>
        struct awaitable : work_item
        {
            Ex ex_;
            std::coroutine_handle<> h_{};

            explicit awaitable(Ex ex) noexcept : ex_(ex) {}

            bool await_ready() const noexcept { return false; }

            void await_suspend(std::coroutine_handle<> h)
            {
                h_ = h;
                ex_.enqueue(this);
            }

            std::size_t await_resume() noexcept { return 0; }

            void execute() noexcept override { h_.resume(); }
        };

        template <typename Promise>
        auto as_awaitable(Promise&) -> awaitable<Promise>
        {
            return awaitable<Promise>{ex_};
        }

        // sender path (consumed via ex::connect)
        template <ex::receiver Receiver>
        struct op_state : work_item
        {
            using operation_state_concept = ex::operation_state_t;

            std::remove_cvref_t<Receiver> rcvr_;
            Ex ex_;

            op_state(Receiver rcvr, Ex ex)
                : rcvr_(std::move(rcvr))
                , ex_(ex)
            {}

            op_state(op_state const&) = delete;
            op_state(op_state&&) = delete;
            op_state& operator=(op_state const&) = delete;
            op_state& operator=(op_state&&) = delete;

            void execute() noexcept override
            {
                ex::set_value(std::move(rcvr_), std::size_t{0});
            }

            void start() & noexcept
            {
                ex_.enqueue(this);
            }
        };

        template <ex::receiver Receiver>
        auto connect(Receiver&& rcvr)
            -> op_state<std::remove_cvref_t<Receiver>>
        {
            return {std::forward<Receiver>(rcvr), ex_};
        }
    };

    read_sender read_some(auto)
    {
        return {ex_};
    }
};

#endif
