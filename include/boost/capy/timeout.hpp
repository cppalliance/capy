//
// Copyright (c) 2026 Michael Vandeberg
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_TIMEOUT_HPP
#define BOOST_CAPY_TIMEOUT_HPP

#include <boost/capy/detail/config.hpp>
#include <boost/capy/concept/io_awaitable.hpp>
#include <boost/capy/delay.hpp>
#include <boost/capy/detail/io_result_combinators.hpp>
#include <boost/capy/error.hpp>
#include <boost/capy/io_result.hpp>
#include <boost/capy/task.hpp>
#include <boost/capy/when_all.hpp>

#include <atomic>
#include <chrono>
#include <exception>
#include <optional>

namespace boost {
namespace capy {
namespace detail {

template<typename T>
struct timeout_state
{
    when_all_core core_;
    std::atomic<int> winner_{-1}; // -1=none, 0=inner, 1=delay
    std::optional<T> inner_result_;
    std::exception_ptr inner_exception_;
    std::array<continuation, 2> runner_handles_{};

    timeout_state()
        : core_(2)
    {
    }
};

template<IoAwaitable Awaitable, typename T>
when_all_runner<timeout_state<T>>
make_timeout_inner_runner(
    Awaitable inner, timeout_state<T>* state)
{
    try
    {
        auto result = co_await std::move(inner);
        state->inner_result_.emplace(std::move(result));
    }
    catch(...)
    {
        state->inner_exception_ = std::current_exception();
    }

    int expected = -1;
    if(state->winner_.compare_exchange_strong(
        expected, 0, std::memory_order_relaxed))
        state->core_.stop_source_.request_stop();
}

template<typename DelayAw, typename T>
when_all_runner<timeout_state<T>>
make_timeout_delay_runner(
    DelayAw d, timeout_state<T>* state)
{
    auto result = co_await std::move(d);

    if(!result.ec)
    {
        int expected = -1;
        if(state->winner_.compare_exchange_strong(
            expected, 1, std::memory_order_relaxed))
            state->core_.stop_source_.request_stop();
    }
}

template<IoAwaitable Inner, typename DelayAw, typename T>
class timeout_launcher
{
    Inner* inner_;
    DelayAw* delay_;
    timeout_state<T>* state_;

public:
    timeout_launcher(
        Inner* inner, DelayAw* delay,
        timeout_state<T>* state)
        : inner_(inner)
        , delay_(delay)
        , state_(state)
    {
    }

    bool await_ready() const noexcept { return false; }

    std::coroutine_handle<> await_suspend(
        std::coroutine_handle<> continuation,
        io_env const* caller_env)
    {
        state_->core_.continuation_.h = continuation;
        state_->core_.caller_env_ = caller_env;

        if(caller_env->stop_token.stop_possible())
        {
            state_->core_.parent_stop_callback_.emplace(
                caller_env->stop_token,
                when_all_core::stop_callback_fn{
                    &state_->core_.stop_source_});

            if(caller_env->stop_token.stop_requested())
                state_->core_.stop_source_.request_stop();
        }

        auto token = state_->core_.stop_source_.get_token();

        auto r0 = make_timeout_inner_runner(
            std::move(*inner_), state_);
        auto h0 = r0.release();
        h0.promise().state_ = state_;
        h0.promise().env_ = io_env{
            caller_env->executor, token,
            caller_env->frame_allocator};
        state_->runner_handles_[0].h =
            std::coroutine_handle<>{h0};

        auto r1 = make_timeout_delay_runner(
            std::move(*delay_), state_);
        auto h1 = r1.release();
        h1.promise().state_ = state_;
        h1.promise().env_ = io_env{
            caller_env->executor, token,
            caller_env->frame_allocator};
        state_->runner_handles_[1].h =
            std::coroutine_handle<>{h1};

        caller_env->executor.post(
            state_->runner_handles_[0]);
        caller_env->executor.post(
            state_->runner_handles_[1]);

        return std::noop_coroutine();
    }

    void await_resume() const noexcept {}
};

} // namespace detail

/** Race an io_result-returning awaitable against a deadline.

    Starts the awaitable and a timer concurrently. The first to
    complete wins and cancels the other. If the awaitable finishes
    first, its result is returned as-is (success, error, or
    exception). If the timer fires first, an `io_result` with
    `ec == error::timeout` is produced.

    Unlike @ref when_any, exceptions from the inner awaitable
    are always propagated — they are never swallowed by the timer.

    @par Return Type

    Always returns `io_result<Ts...>` matching the inner
    awaitable's result type. On timeout, `ec` is set to
    `error::timeout` and payload values are default-initialized.

    @par Precision

    The timeout fires at or after the specified duration.

    @par Cancellation

    If the parent's stop token is activated, both children are
    cancelled. The inner awaitable's cancellation result is
    returned.

    @par Example
    @code
    auto [ec, n] = co_await timeout(sock.read_some(buf), 50ms);
    if (ec == cond::timeout) {
        // handle timeout
    }
    @endcode

    @tparam A An IoAwaitable returning `io_result<Ts...>`.

    @param a The awaitable to race against the deadline.
    @param dur The maximum duration to wait.

    @return `task<awaitable_result_t<A>>`.

    @throws Rethrows any exception from the inner awaitable,
        regardless of whether the timer has fired.

    @see delay, cond::timeout
*/
template<IoAwaitable A, typename Rep, typename Period>
    requires detail::is_io_result_v<awaitable_result_t<A>>
auto timeout(A a, std::chrono::duration<Rep, Period> dur)
    -> task<awaitable_result_t<A>>
{
    using T = awaitable_result_t<A>;

    auto d = delay(dur);
    detail::timeout_state<T> state;

    co_await detail::timeout_launcher<
        A, decltype(d), T>(&a, &d, &state);

    if(state.core_.first_exception_)
        std::rethrow_exception(state.core_.first_exception_);
    if(state.inner_exception_)
        std::rethrow_exception(state.inner_exception_);

    if(state.winner_.load(std::memory_order_relaxed) == 0)
        co_return std::move(*state.inner_result_);

    // Delay fired first: timeout
    T r{};
    r.ec = make_error_code(error::timeout);
    co_return r;
}

} // capy
} // boost

#endif
