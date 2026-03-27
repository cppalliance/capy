//
// Copyright (c) 2026 Steve Gerbino
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

//
// Synchronous-completion sender stream.
//
// Every read completes immediately. The sender's
// start() calls set_value synchronously. The
// as_awaitable path returns the coroutine handle
// for symmetric transfer.
//
// WARNING: Using this sender in a loop algorithm
// like repeat_effect_until will stack overflow —
// there is no trampoline for synchronous
// completions. Coroutines handle this via symmetric
// transfer; sender pipelines cannot.
//

#ifndef BOOST_CAPY_BENCH_SNDR_SYNC_READ_STREAM_HPP
#define BOOST_CAPY_BENCH_SNDR_SYNC_READ_STREAM_HPP

#include <beman/execution/execution.hpp>

#include <coroutine>
#include <cstddef>
#include <type_traits>
#include <utility>

namespace ex = beman::execution;

struct sndr_sync_read_stream
{
    struct read_sender
    {
        using sender_concept = ex::sender_t;
        using completion_signatures =
            ex::completion_signatures<ex::set_value_t(std::size_t)>;

        // awaitable path (co_awaited from bex::task via as_awaitable)
        template <typename Promise>
        struct awaitable
        {
            bool await_ready() const noexcept
            {
                return false;
            }

            std::coroutine_handle<>
            await_suspend(std::coroutine_handle<> h)
            {
                // Data already buffered — resume inline
                return h;
            }

            std::size_t await_resume() noexcept
            {
                return 0;
            }
        };

        template <typename Promise>
        auto as_awaitable(Promise&) -> awaitable<Promise>
        {
            return {};
        }

        // sender path (consumed via ex::connect)
        template <ex::receiver Receiver>
        struct op_state
        {
            using operation_state_concept =
                ex::operation_state_t;

            std::remove_cvref_t<Receiver> rcvr_;

            op_state(Receiver rcvr)
                : rcvr_(std::move(rcvr))
            {}

            op_state(op_state const&) = delete;
            op_state(op_state&&) = delete;
            op_state& operator=(op_state const&) = delete;
            op_state& operator=(op_state&&) = delete;

            void start() & noexcept
            {
                // Synchronous completion — causes
                // stack overflow in loop algorithms
                // without a trampoline
                ex::set_value(
                    std::move(rcvr_), std::size_t{0});
            }
        };

        template <ex::receiver Receiver>
        auto connect(Receiver&& rcvr)
            -> op_state<std::remove_cvref_t<Receiver>>
        {
            return {std::forward<Receiver>(rcvr)};
        }
    };

    read_sender read_some(auto)
    {
        return {};
    }
};

#endif
