//
// Copyright (c) 2026 Steve Gerbino
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

//
// D4126 bridge: wraps a plain awaitable as a sender.
//
// Uses frame_cb — a synthetic coroutine frame whose layout matches
// MSVC/GCC/Clang — to drive the awaitable through await_suspend(h)
// without a real coroutine. The op_state is stored inline; no
// heap allocation occurs in connect or start.
//

#ifndef BOOST_CAPY_BENCH_D4126_BRIDGE_HPP
#define BOOST_CAPY_BENCH_D4126_BRIDGE_HPP

#include <beman/execution/execution.hpp>

#include <coroutine>
#include <cstddef>
#include <type_traits>
#include <utility>

namespace ex = beman::execution;

namespace detail {

struct frame_cb
{
    void (*resume)(frame_cb*);
    void (*destroy)(frame_cb*);
    void* data;
};

} // namespace detail

template <class Aw>
struct d4126_connect_sender
{
    using sender_concept = ex::sender_t;
    using completion_signatures =
        ex::completion_signatures<ex::set_value_t(std::size_t)>;

    Aw aw_;

    template <ex::receiver Receiver>
    struct op_state
    {
        using operation_state_concept = ex::operation_state_t;

        Aw aw_;
        std::remove_cvref_t<Receiver> rcvr_;
        detail::frame_cb cb_{};

        op_state(Aw aw, Receiver rcvr)
            : aw_(std::move(aw))
            , rcvr_(std::move(rcvr))
        {}

        op_state(op_state const&) = delete;
        op_state(op_state&&) = delete;
        op_state& operator=(op_state const&) = delete;
        op_state& operator=(op_state&&) = delete;

        static void on_resume(detail::frame_cb* p) noexcept
        {
            auto* self = static_cast<op_state*>(p->data);
            auto result = self->aw_.await_resume();
            ex::set_value(std::move(self->rcvr_), result);
        }

        static void on_destroy(detail::frame_cb*) noexcept {}

        void start() & noexcept
        {
            if (aw_.await_ready())
            {
                auto result = aw_.await_resume();
                ex::set_value(std::move(rcvr_), result);
                return;
            }

            cb_.resume = &on_resume;
            cb_.destroy = &on_destroy;
            cb_.data = this;

            auto h = std::coroutine_handle<>::from_address(
                static_cast<void*>(&cb_));
            aw_.await_suspend(h);
        }
    };

    template <ex::receiver Receiver>
    auto connect(Receiver rcvr) &&
        -> op_state<std::remove_cvref_t<Receiver>>
    {
        return {std::move(aw_), std::move(rcvr)};
    }
};

#endif
