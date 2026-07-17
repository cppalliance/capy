//
// Copyright (c) 2026 Steve Gerbino
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_EXAMPLE_READ_OP_HPP
#define BOOST_CAPY_EXAMPLE_READ_OP_HPP

#include "awaitable_sender_base.hpp"

#include <boost/capy/continuation.hpp>
#include <boost/capy/ex/async_waker.hpp>
#include <boost/capy/ex/io_env.hpp>
#include <boost/capy/io_result.hpp>
#include <boost/capy/task.hpp>

#include <coroutine>
#include <cstddef>
#include <optional>
#include <system_error>
#include <utility>

namespace boost::capy {

/** Mock I/O op with a scripted outcome, modeled on the real
    corosio op shape: starts in `await_suspend`, completes
    through the environment's executor, and is a sender via
    @ref awaitable_sender_base. Its stop-wait mode composes
    `async_waker` rather than hand-rolling stop-callback arming:
    the waker's arbiter already resolves the races between
    arming, stop requests, and completion.
*/
class read_op : public awaitable_sender_base<read_op>
{
    // Data members are private so the op is invisible to
    // std::execution sender decomposition: tag_of_t claims any
    // type whose members admit a structured binding, and
    // inaccessible members make that binding ill-formed (it also
    // keeps the type a non-aggregate, which is the condition
    // today's implementations actually probe).
    std::error_code ec_{};
    std::size_t n_ = 0;
    bool immediate_ = false;

    io_env const* env_ = nullptr;
    continuation cont_{};

    // Stop-wait mode: a coroutine owning a waker nobody wakes, so
    // it completes only when the environment's stop token fires
    // (with error::canceled). The waker lives in the coroutine
    // frame; the task is movable, keeping read_op movable.
    std::optional<task<io_result<>>> stop_task_;

    static task<io_result<>> stop_wait()
    {
        async_waker waker;
        co_return co_await waker.wait();
    }

public:
    /// Construct an op that completes successfully with zero bytes.
    read_op() = default;

    // Scripted construction goes through factories, not multi-arg
    // constructors — a scaffolding accommodation, unnecessary at
    // graduation: the bundled pre-standard implementation probes
    // decomposable senders by brace-initializing with 2..6
    // placeholder arguments, and any constructor of that arity
    // matches the probe and re-enters its decomposition machinery.

    /// Create an op with a scripted `(ec, n)` completion.
    static read_op result(std::error_code ec, std::size_t n)
    {
        read_op op;
        op.ec_ = ec;
        op.n_ = n;
        return op;
    }

    /// Create an op that completes inline via `await_ready`.
    static read_op immediate(std::error_code ec, std::size_t n)
    {
        read_op op = result(ec, n);
        op.immediate_ = true;
        return op;
    }

    /// Create an op that never self-completes; it finishes only
    /// on a stop request.
    static read_op waits_for_stop()
    {
        read_op op;
        op.stop_task_ = stop_wait();
        return op;
    }

    bool await_ready() const noexcept
    {
        return immediate_;
    }

    std::coroutine_handle<> await_suspend(
        std::coroutine_handle<> h,
        io_env const* env)
    {
        if(stop_task_)
            return stop_task_->await_suspend(h, env);
        env_ = env;
        cont_ = continuation{h};
        env_->executor.post(cont_);
        return std::noop_coroutine();
    }

    io_result<std::size_t> await_resume() noexcept
    {
        // The op reports its own disposition in-band: a stop-wait
        // ends with the waker's error::canceled, which the sender
        // machinery routes to set_stopped() and a co_await caller
        // observes as operation_canceled.
        if(stop_task_)
        {
            auto [ec] = stop_task_->await_resume();
            return {ec ? ec : ec_, n_};
        }
        return {ec_, n_};
    }
};

} // namespace boost::capy

#endif
