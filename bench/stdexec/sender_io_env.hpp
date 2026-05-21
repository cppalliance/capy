//
// Copyright (c) 2026 Steve Gerbino
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

//
// stdexec execution environment for benchmarks.
//
// Provides the capy executor adapter wrapping exec::static_thread_pool,
// so capy::task can run on it.
//

#ifndef BOOST_CAPY_BENCH_STDEXEC_SENDER_IO_ENV_HPP
#define BOOST_CAPY_BENCH_STDEXEC_SENDER_IO_ENV_HPP

#include "awaitable_sender.hpp"

#include <boost/capy/continuation.hpp>
#include <boost/capy/ex/execution_context.hpp>

#include <stdexec/execution.hpp>
#include <exec/static_thread_pool.hpp>
#include <exec/start_detached.hpp>

#include <coroutine>

// Minimal execution_context shell for exec::static_thread_pool.
// exec::static_thread_pool does not inherit from capy's
// execution_context, but the Executor concept requires context()
// to return one. This stub satisfies the requirement without
// any service machinery.
struct static_pool_context
    : boost::capy::execution_context
{
    static_pool_context()
        : boost::capy::execution_context(this)
    {}

    ~static_pool_context()
    {
        shutdown();
        destroy();
    }

    static_pool_context(static_pool_context const&) = delete;
    static_pool_context& operator=(static_pool_context const&) = delete;
};

// Adapter making exec::static_thread_pool satisfy capy's
// Executor concept so capy::task can run on it.
struct sender_as_capy_executor
{
    exec::static_thread_pool* pool_;
    static_pool_context* ctx_;

    boost::capy::execution_context& context() const noexcept
    {
        return *ctx_;
    }

    void on_work_started() const noexcept {}
    void on_work_finished() const noexcept {}

    void post(boost::capy::continuation& cont) const;

    // Return the handle for symmetric transfer so the
    // caller resumes the coroutine inline.
    std::coroutine_handle<>
    dispatch(boost::capy::continuation& c) const
    {
        return c.h;
    }

    bool operator==(
        sender_as_capy_executor const&) const noexcept = default;
};

// Heap-allocated trampoline; not zero-alloc by design.
// Honest reflection of what exec::static_thread_pool costs.
inline void sender_as_capy_executor::post(
    boost::capy::continuation& cont) const
{
    // upon_error eats the set_error_t(exception_ptr) channel
    // that stdexec::then advertises so start_detached's
    // no-error precondition is satisfied at the type level.
    exec::start_detached(
        stdexec::schedule(pool_->get_scheduler())
        | stdexec::then([&cont]() noexcept { cont.h.resume(); })
        | stdexec::upon_error([](auto&&) noexcept {}));
}

// Forward declaration needed so pool_schedule_sender can name pool_scheduler.
struct pool_scheduler;

// Custom schedule-sender for pool_scheduler. Wraps the pool's native
// schedule sender but reports pool_scheduler as the completion scheduler,
// which stdexec's starts_on adapter requires.
struct pool_schedule_sender
{
    using sender_concept = stdexec::sender_tag;

    exec::static_thread_pool* pool_;
    pool_scheduler const* sched_;

    // exec::static_thread_pool::scheduler completes with set_value_t()
    // and set_stopped_t() (when the receiver carries a stop token).
    template<class Self, class Env>
    static consteval auto get_completion_signatures() noexcept
    {
        return stdexec::completion_signatures<
            stdexec::set_value_t(),
            stdexec::set_stopped_t()>{};
    }

    struct env_t
    {
        pool_scheduler const* sched_;

        auto query(stdexec::get_completion_scheduler_t<
                   stdexec::set_value_t> const&) const noexcept
            -> pool_scheduler const&
        {
            return *sched_;
        }
    };

    env_t get_env() const noexcept { return {sched_}; }

    template<class Receiver>
    auto connect(Receiver&& rcvr) &&
    {
        return stdexec::connect(
            pool_->get_scheduler().schedule(),
            std::forward<Receiver>(rcvr));
    }

    template<class Receiver>
    auto connect(Receiver&& rcvr) const&
    {
        return stdexec::connect(
            pool_->get_scheduler().schedule(),
            std::forward<Receiver>(rcvr));
    }
};

// Scheduler wrapper that delegates schedule() to exec::static_thread_pool
// but answers boost::capy's get_io_executor_t query. Required by the
// capy::as_sender bridge in awaitable_sender.hpp, which queries the
// receiver-env scheduler for the capy executor at instantiation time.
struct pool_scheduler
{
    using scheduler_concept = stdexec::scheduler_t;

    exec::static_thread_pool* pool_;
    static_pool_context* ctx_;

    pool_schedule_sender schedule() const noexcept
    {
        return {pool_, this};
    }

    auto query(boost::capy::get_io_executor_t const&) const noexcept
        -> sender_as_capy_executor
    {
        return sender_as_capy_executor{pool_, ctx_};
    }

    bool operator==(pool_scheduler const&) const = default;
};

#endif
