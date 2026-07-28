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
    arming, stop requests, and completion. Completes with a bare
    `io_result<>`: a result carrying payload alongside the
    `error_code` cannot be a sender (see @ref as_sender).
*/
class read_op : public awaitable_sender_base<read_op>
{
    // Data members are private so the op is invisible to
    // std::execution sender decomposition: tag_of_t claims any
    // type whose members admit a structured binding, and
    // inaccessible members make that binding ill-formed.
    std::error_code ec_{};
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
    /// Construct an op that completes successfully.
    read_op() = default;

    /// Create an op with a scripted `error_code` completion.
    static read_op result(std::error_code ec)
    {
        read_op op;
        op.ec_ = ec;
        return op;
    }

    /// Create an op that completes inline via `await_ready`.
    static read_op immediate(std::error_code ec)
    {
        read_op op = result(ec);
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

    io_result<> await_resume() noexcept
    {
        // The op reports its own disposition in-band: a stop-wait
        // ends with the waker's error::canceled, which the sender
        // machinery routes to set_stopped() and a co_await caller
        // observes as operation_canceled.
        if(stop_task_)
        {
            auto [ec] = stop_task_->await_resume();
            return {ec ? ec : ec_};
        }
        return {ec_};
    }
};

/** Mock byte-producing op: payload via caller-owned state.

    Demonstrates how a base-derived op delivers a count now that
    compound results cannot be senders: `await_resume()` reports
    only the disposition, and the byte count goes to a
    caller-owned location, the same ownership model as the buffer
    it would describe. The base has no wrapper seam to intercept,
    so the side channel is baked in at op-design time.
*/
class counted_read_op
    : public awaitable_sender_base<counted_read_op>
{
    std::error_code ec_{};
    std::size_t n_ = 0;
    std::size_t* n_out_ = nullptr;

    io_env const* env_ = nullptr;
    continuation cont_{};

public:
    /// Create an op with a scripted `(ec, n)` completion that
    /// reports `n` through `*n_out`.
    static counted_read_op result(
        std::error_code ec, std::size_t n, std::size_t* n_out)
    {
        counted_read_op op;
        op.ec_ = ec;
        op.n_ = n;
        op.n_out_ = n_out;
        return op;
    }

    bool await_ready() const noexcept
    {
        return false;
    }

    std::coroutine_handle<> await_suspend(
        std::coroutine_handle<> h,
        io_env const* env)
    {
        env_ = env;
        cont_ = continuation{h};
        env_->executor.post(cont_);
        return std::noop_coroutine();
    }

    io_result<> await_resume() noexcept
    {
        // Written before either protocol observes completion, so
        // the count is valid on every channel, including
        // set_stopped(), which cannot carry data itself.
        *n_out_ = n_;
        return {ec_};
    }
};

} // namespace boost::capy

#endif
