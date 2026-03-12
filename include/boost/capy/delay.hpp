//
// Copyright (c) 2026 Michael Vandeberg
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_DELAY_HPP
#define BOOST_CAPY_DELAY_HPP

#include <boost/capy/detail/config.hpp>
#include <boost/capy/ex/executor_ref.hpp>
#include <boost/capy/ex/io_env.hpp>
#include <boost/capy/ex/detail/timer_service.hpp>

#include <atomic>
#include <chrono>
#include <coroutine>
#include <new>
#include <stop_token>
#include <utility>

namespace boost {
namespace capy {

/** IoAwaitable returned by @ref delay.

    Suspends the calling coroutine until the deadline elapses
    or the environment's stop token is activated, whichever
    comes first. Resumption is always posted through the
    executor, never inline on the timer thread.

    Not intended to be named directly; use the @ref delay
    factory function instead.

    @par Cancellation

    If `stop_requested()` is true before suspension, the
    coroutine resumes immediately without scheduling a timer.
    If stop is requested while suspended, the stop callback
    claims the resume and posts it through the executor; the
    pending timer is cancelled on the next `await_resume` or
    destructor call.

    @par Thread Safety

    A single `delay_awaitable` must not be awaited concurrently.
    Multiple independent `delay()` calls on the same
    execution_context are safe and share one timer thread.

    @see delay, timeout
*/
class delay_awaitable
{
    std::chrono::nanoseconds dur_;

    detail::timer_service* ts_ = nullptr;
    detail::timer_service::timer_id tid_ = 0;

    // Declared before stop_cb_buf_: the callback
    // accesses these members, so they must still be
    // alive if the stop_cb_ destructor blocks.
    std::atomic<bool> claimed_{false};
    bool canceled_ = false;
    bool stop_cb_active_ = false;

    struct cancel_fn
    {
        delay_awaitable* self_;
        executor_ref ex_;
        std::coroutine_handle<> h_;

        void operator()() const noexcept
        {
            if(!self_->claimed_.exchange(
                true, std::memory_order_acq_rel))
            {
                self_->canceled_ = true;
                ex_.post(h_);
            }
        }
    };

    using stop_cb_t = std::stop_callback<cancel_fn>;

    // Aligned storage for the stop callback.
    // Declared last: its destructor may block while
    // the callback accesses the members above.
    BOOST_CAPY_MSVC_WARNING_PUSH
    BOOST_CAPY_MSVC_WARNING_DISABLE(4324)
    alignas(stop_cb_t)
        unsigned char stop_cb_buf_[sizeof(stop_cb_t)];
    BOOST_CAPY_MSVC_WARNING_POP

    stop_cb_t& stop_cb_() noexcept
    {
        return *reinterpret_cast<stop_cb_t*>(stop_cb_buf_);
    }

public:
    explicit delay_awaitable(std::chrono::nanoseconds dur) noexcept
        : dur_(dur)
    {
    }

    /// @pre The stop callback must not be active
    ///      (i.e. the object has not yet been awaited).
    delay_awaitable(delay_awaitable&& o) noexcept
        : dur_(o.dur_)
        , ts_(o.ts_)
        , tid_(o.tid_)
        , claimed_(o.claimed_.load(std::memory_order_relaxed))
        , canceled_(o.canceled_)
        , stop_cb_active_(std::exchange(o.stop_cb_active_, false))
    {
    }

    ~delay_awaitable()
    {
        if(stop_cb_active_)
            stop_cb_().~stop_cb_t();
        if(ts_)
            ts_->cancel(tid_);
    }

    delay_awaitable(delay_awaitable const&) = delete;
    delay_awaitable& operator=(delay_awaitable const&) = delete;
    delay_awaitable& operator=(delay_awaitable&&) = delete;

    bool await_ready() const noexcept
    {
        return dur_.count() <= 0;
    }

    std::coroutine_handle<>
    await_suspend(
        std::coroutine_handle<> h,
        io_env const* env) noexcept
    {
        // Already stopped: resume immediately
        if(env->stop_token.stop_requested())
        {
            canceled_ = true;
            return h;
        }

        ts_ = &env->executor.context().use_service<detail::timer_service>();

        // Schedule timer (won't fire inline since deadline is in the future)
        tid_ = ts_->schedule_after(dur_,
            [this, h, ex = env->executor]()
            {
                if(!claimed_.exchange(
                    true, std::memory_order_acq_rel))
                {
                    ex.post(h);
                }
            });

        // Register stop callback (may fire inline)
        ::new(stop_cb_buf_) stop_cb_t(
            env->stop_token,
            cancel_fn{this, env->executor, h});
        stop_cb_active_ = true;

        return std::noop_coroutine();
    }

    void await_resume() noexcept
    {
        if(stop_cb_active_)
        {
            stop_cb_().~stop_cb_t();
            stop_cb_active_ = false;
        }
        if(ts_)
            ts_->cancel(tid_);
    }
};

/** Suspend the current coroutine for a duration.

    Returns an IoAwaitable that completes at or after the
    specified duration, or earlier if the environment's stop
    token is activated. Completion is always normal (void
    return); no exception is thrown on cancellation.

    Zero or negative durations complete synchronously without
    scheduling a timer.

    @par Example
    @code
    co_await delay(std::chrono::milliseconds(100));
    @endcode

    @param dur The duration to wait.

    @return A @ref delay_awaitable whose `await_resume`
        returns `void`.

    @throws Nothing.

    @see timeout, delay_awaitable
*/
template<typename Rep, typename Period>
delay_awaitable
delay(std::chrono::duration<Rep, Period> dur) noexcept
{
    return delay_awaitable{
        std::chrono::duration_cast<std::chrono::nanoseconds>(dur)};
}

} // capy
} // boost

#endif
