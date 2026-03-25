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
// The stream holds a sender_executor (I/O context handle),
// analogous to how a socket holds a reference to io_context.
// read_some() returns a sender that captures this handle.
// The sender provides both as_awaitable (for coroutine
// consumption) and connect (for sender pipeline consumption).
//

#ifndef BOOST_CAPY_BENCH_SNDR_READ_STREAM_HPP
#define BOOST_CAPY_BENCH_SNDR_READ_STREAM_HPP

#include "sender_thread_pool.hpp"
#include "thread_pool.hpp"

#include <beman/execution/execution.hpp>

#include <coroutine>
#include <cstddef>
#include <type_traits>
#include <utility>

namespace ex = beman::execution;

struct sndr_read_stream
{
    sender_executor ex_;

    struct read_sender
    {
        using sender_concept = ex::sender_t;
        using completion_signatures =
            ex::completion_signatures<ex::set_value_t(std::size_t)>;

        sender_executor ex_;

        // awaitable path (co_awaited from io_task via as_awaitable)
        template <typename Promise>
        struct awaitable : work_item
        {
            sender_executor ex_;
            std::coroutine_handle<> h_{};

            explicit awaitable(sender_executor ex) noexcept
                : ex_(ex) {}

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
            sender_executor ex_;

            op_state(Receiver rcvr, sender_executor ex)
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
