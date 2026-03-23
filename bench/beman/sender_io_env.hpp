//
// Copyright (c) 2026 Steve Gerbino
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

//
// Beman execution environment for benchmarks.
//
// Provides a minimal scheduler, executor, and io_env that bridge
// sender_thread_pool to beman::execution::task. Used by any
// benchmark that co_awaits senders from an io_task.
//

#ifndef BOOST_CAPY_BENCH_SENDER_IO_ENV_HPP
#define BOOST_CAPY_BENCH_SENDER_IO_ENV_HPP

#include "sender_thread_pool.hpp"

#include <beman/execution/execution.hpp>
#include <beman/task/task.hpp>

#include <coroutine>
#include <memory_resource>
#include <type_traits>
#include <utility>

namespace ex = beman::execution;

struct pool_scheduler
{
    using scheduler_concept = ex::scheduler_t;

    sender_executor ex_;

    struct env
    {
        sender_executor ex_;
        auto query(
            ex::get_completion_scheduler_t<ex::set_value_t> const&
        ) const noexcept
        {
            return pool_scheduler{ex_};
        }
    };

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
            ex::set_value(std::move(rcvr_));
        }

        void start() & noexcept
        {
            ex_.enqueue(this);
        }
    };

    struct sender
    {
        using sender_concept = ex::sender_t;
        using completion_signatures =
            ex::completion_signatures<ex::set_value_t()>;

        sender_executor ex_;

        auto get_env() const noexcept { return env{ex_}; }

        template <ex::receiver Receiver>
        auto connect(Receiver&& rcvr)
            -> op_state<std::remove_cvref_t<Receiver>>
        {
            return {std::forward<Receiver>(rcvr), ex_};
        }
    };

    auto schedule() -> sender { return {ex_}; }
    bool operator==(pool_scheduler const&) const = default;
};

struct get_sender_executor_t
{
    constexpr bool query(
        ex::forwarding_query_t const&) const noexcept
    {
        return true;
    }

    template <typename Env>
        requires requires(Env const& env) {
            env.query(std::declval<get_sender_executor_t const&>());
        }
    auto operator()(Env const& env) const noexcept
    {
        return env.query(*this);
    }
};
inline constexpr get_sender_executor_t get_sender_executor{};

struct io_env
{
    using scheduler_type = pool_scheduler;
    using allocator_type = std::pmr::polymorphic_allocator<std::byte>;

    sender_executor executor;

    auto query(
        get_sender_executor_t const&) const noexcept -> sender_executor
    {
        return executor;
    }

    io_env() = default;

    template <typename Env>
        requires requires(Env const& e) {
            pool_scheduler{ex::get_scheduler(e)};
        }
    io_env(Env const& e)
        : executor(pool_scheduler{ex::get_scheduler(e)}.ex_)
    {}
};

template <typename T = void>
using io_task = ex::task<T, io_env>;

#endif
