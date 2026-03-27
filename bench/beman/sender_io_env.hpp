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
// Provides pool_scheduler (the P2300 scheduler for
// sender_thread_pool), the capy executor adapter, and
// the io_env for beman::execution::task.
//

#ifndef BOOST_CAPY_BENCH_SENDER_IO_ENV_HPP
#define BOOST_CAPY_BENCH_SENDER_IO_ENV_HPP

#include "sender_thread_pool.hpp"
#include "awaitable_sender.hpp"

#include <boost/capy/continuation.hpp>
#include <boost/capy/ex/execution_context.hpp>

#include <beman/execution/execution.hpp>
#include <beman/task/task.hpp>

#include <coroutine>
#include <memory_resource>
#include <type_traits>
#include <utility>

// Adapter making sender_thread_pool satisfy capy's
// Executor concept so capy::task can run on it.
struct sender_as_capy_executor
{
    sender_thread_pool* pool_;

    boost::capy::execution_context& context() const noexcept
    {
        return *pool_;
    }

    void on_work_started() const noexcept
    {
        pool_->on_work_started();
    }

    void on_work_finished() const noexcept
    {
        pool_->on_work_finished();
    }

    void post(boost::capy::continuation& c) const;

    // Return the handle for symmetric transfer so the
    // caller resumes the coroutine inline. Posting would
    // cause a lifetime issue since run_async expects to
    // hand off ownership via symmetric transfer.
    std::coroutine_handle<>
    dispatch(boost::capy::continuation& c) const
    {
        return c.h;
    }

    bool operator==(
        sender_as_capy_executor const&) const noexcept = default;
};

namespace ex = beman::execution;

struct pool_scheduler
{
    using scheduler_concept = ex::scheduler_t;

    sender_thread_pool* pool_;

    struct env
    {
        sender_thread_pool* pool_;

        auto query(
            ex::get_completion_scheduler_t<ex::set_value_t> const&
        ) const noexcept
        {
            return pool_scheduler{pool_};
        }
    };

    template <ex::receiver Receiver>
    struct op_state : work_item
    {
        using operation_state_concept = ex::operation_state_t;

        std::remove_cvref_t<Receiver> rcvr_;
        sender_thread_pool* pool_;

        op_state(Receiver rcvr, sender_thread_pool* pool)
            : rcvr_(std::move(rcvr))
            , pool_(pool)
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
            pool_->enqueue(this);
        }
    };

    struct sender
    {
        using sender_concept = ex::sender_t;
        using completion_signatures =
            ex::completion_signatures<ex::set_value_t()>;

        sender_thread_pool* pool_;

        auto get_env() const noexcept { return env{pool_}; }

        template <ex::receiver Receiver>
        auto connect(Receiver&& rcvr)
            -> op_state<std::remove_cvref_t<Receiver>>
        {
            return {std::forward<Receiver>(rcvr), pool_};
        }
    };

    auto query(
        boost::capy::get_io_executor_t const&
    ) const noexcept -> sender_as_capy_executor
    {
        return sender_as_capy_executor{pool_};
    }

    auto schedule() -> sender { return {pool_}; }
    bool operator==(pool_scheduler const&) const = default;
};

inline pool_scheduler
sender_thread_pool::get_scheduler() noexcept
{
    return pool_scheduler{this};
}

// P2300 has no post(coroutine_handle<>). To resume a
// coroutine on a scheduler you must go through
// schedule → connect → start. The operation state
// must be heap-allocated because the coroutine is
// suspended and cannot host it.
struct scheduled_resume
{
    struct receiver
    {
        using receiver_concept = ex::receiver_t;

        scheduled_resume* self_;

        void set_value() && noexcept
        {
            auto h = self_->h_;
            delete self_;
            h.resume();
        }

        void set_error(auto&&) && noexcept
        {
            std::terminate();
        }

        void set_stopped() && noexcept
        {
            std::terminate();
        }
    };

    using op_state_t =
        pool_scheduler::op_state<receiver>;

    std::coroutine_handle<> h_;
    op_state_t op_;

    scheduled_resume(
        pool_scheduler sched,
        std::coroutine_handle<> h)
        : h_(h)
        , op_(ex::connect(
            sched.schedule(),
            receiver{this}))
    {}
};

inline void sender_as_capy_executor::post(
    boost::capy::continuation& c) const
{
    auto* p = new scheduled_resume(
        pool_scheduler{pool_}, c.h);
    ex::start(p->op_);
}

struct io_env
{
    using scheduler_type = pool_scheduler;
    using allocator_type = std::pmr::polymorphic_allocator<std::byte>;

    sender_thread_pool* pool_ = nullptr;

    io_env() = default;

    template <typename Env>
        requires requires(Env const& e) {
            pool_scheduler{ex::get_scheduler(e)};
        }
    io_env(Env const& e)
        : pool_(pool_scheduler{ex::get_scheduler(e)}.pool_)
    {}
};

#endif
