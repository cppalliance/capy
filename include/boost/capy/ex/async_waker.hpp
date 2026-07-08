//
// Copyright (c) 2026 Steve Gerbino
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_EX_ASYNC_WAKER_HPP
#define BOOST_CAPY_EX_ASYNC_WAKER_HPP

#include <boost/capy/detail/config.hpp>
#include <boost/capy/continuation.hpp>
#include <boost/capy/error.hpp>
#include <boost/capy/ex/executor_ref.hpp>
#include <boost/capy/ex/io_env.hpp>
#include <boost/capy/io_result.hpp>

#include <atomic>
#include <coroutine>
#include <new>
#include <stop_token>
#include <utility>

/*  async_waker implementation notes
    ===================================

    wake() must be callable from foreign threads (that is the whole
    point: the user's thread provides the timing). A waiter-side
    claimed_ flag is not enough there -- the
    waker has to dereference the waiter, and nothing would pin the
    waiter's frame between reading the pointer and claiming it.

    So the three-state st_ atomic is the single arbiter:

        empty --arm-->  armed --wake/cancel CAS--> empty
        empty --wake--> token --wait consumes--> empty

    Whoever wins the armed->empty CAS owns the resume and may
    dereference waiter_: the frame cannot die underneath the
    winner because the coroutine only resumes when the winner
    posts it. The loser never touches the waiter. When the stop
    callback wins, a concurrent wake retries, finds empty, and
    latches a token -- a racing wakeup is deferred, never lost.

    Serialized resumption is required: await_suspend keeps
    writing after the publishing armed-CAS (the stop_cb
    placement-new and active_ = true), so a wake/cancel winner
    can post the continuation while that tail is still running.
    The posted resume must be ordered after await_suspend's
    return, which holds on a single-threaded executor (the one
    thread is still inside await_suspend) and on a strand (the
    resume is a later turn, synchronized with the current one).
    A raw multi-threaded executor lets another worker run
    await_resume against those in-flight writes. async_event and
    async_mutex make the same assumption; it is stated explicitly
    here because wake() invites foreign threads into the picture.
*/

namespace boost {
namespace capy {

/** A single-slot waker that hands one wakeup to a waiting coroutine.

    This is the escape hatch for timing and other external events:
    the user provides the thread and the clock, capy provides the
    suspension point. One coroutine suspends in `wait()`; any
    thread wakes it with `wake()`.

    A wakeup with no waiter present is latched as a single pending
    token, and the next `wait()` consumes it immediately. This
    makes the wake-before-wait race benign without any lock
    protocol. Multiple wakes collapse into one token.

    @par Cancellation

    If the environment's stop token is triggered while suspended,
    the wait completes with `error::canceled`. A wake that loses
    the race against cancellation is latched for the next `wait()`
    rather than dropped.

    @par Zero Allocation

    No heap allocation occurs for wait or wake operations.

    @par Thread Safety

    Distinct objects: Safe.@n
    Shared objects: `wake()` may be called from any thread.
    `wait()` must only be awaited by one coroutine at a time, and
    only on an executor that never runs the coroutine's
    continuations concurrently: a single-threaded executor or a
    strand over a multi-threaded one (the same threading model as
    `async_event` and `async_mutex`). Awaiting `wait()` directly
    on a multi-threaded executor is undefined.

    This type is non-copyable and non-movable because a suspended
    waiter holds a pointer into the object.

    @par Example
    @code
    async_waker waker;

    // user-provided timing thread
    std::thread th([&waker] {
        std::this_thread::sleep_for(100ms);
        waker.wake();
    });

    task<> waiter() {
        auto [ec] = co_await waker.wait();
        // resumed on the executor after ~100ms
    }
    // ... th.join() after the pool drains
    @endcode
*/
class async_waker
{
public:
    class wait_awaiter;

private:
    static constexpr int state_empty = 0; // no token, no waiter
    static constexpr int state_token = 1; // latched wakeup
    static constexpr int state_armed = 2; // waiter suspended

    std::atomic<int> st_{state_empty};
    wait_awaiter* waiter_ = nullptr;

public:
    /** Awaiter returned by wait().
    */
    class wait_awaiter
    {
        friend class async_waker;

        async_waker* waker_;
        continuation cont_;
        executor_ref ex_;

        // Declared before stop_cb_buf_: the callback accesses
        // these members, so they must still be alive if the
        // stop_cb_ destructor blocks.
        bool canceled_ = false;
        bool active_ = false;
        bool published_ = false;

        struct cancel_fn
        {
            wait_awaiter* self_;

            void operator()() const noexcept
            {
                int expected = state_armed;
                if(self_->waker_->st_.compare_exchange_strong(
                    expected, state_empty,
                    std::memory_order_acq_rel,
                    std::memory_order_acquire))
                {
                    self_->canceled_ = true;
                    self_->ex_.post(self_->cont_);
                }
            }
        };

        using stop_cb_t = std::stop_callback<cancel_fn>;

        // Aligned storage for stop_cb_t. Declared last: its
        // destructor may block while the callback accesses the
        // members above.
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
        ~wait_awaiter()
        {
            if(active_)
                stop_cb_().~stop_cb_t();
            if(published_)
            {
                // Destroyed while still armed (frame torn down
                // without resuming): deregister so a later
                // wake cannot touch the dead frame.
                int expected = state_armed;
                waker_->st_.compare_exchange_strong(
                    expected, state_empty,
                    std::memory_order_acq_rel,
                    std::memory_order_acquire);
            }
        }

        explicit wait_awaiter(async_waker* waker) noexcept
            : waker_(waker)
        {
        }

        wait_awaiter(wait_awaiter&& o) noexcept
            : waker_(o.waker_)
            , cont_(o.cont_)
            , ex_(o.ex_)
            , canceled_(o.canceled_)
            , active_(std::exchange(o.active_, false))
            , published_(std::exchange(o.published_, false))
        {
        }

        wait_awaiter(wait_awaiter const&) = delete;
        wait_awaiter& operator=(wait_awaiter const&) = delete;
        wait_awaiter& operator=(wait_awaiter&&) = delete;

        /// Consume a latched token, completing synchronously.
        bool await_ready() noexcept
        {
            int expected = state_token;
            return waker_->st_.compare_exchange_strong(
                expected, state_empty,
                std::memory_order_acq_rel,
                std::memory_order_acquire);
        }

        /** IoAwaitable protocol overload. */
        std::coroutine_handle<>
        await_suspend(
            std::coroutine_handle<> h,
            io_env const* env) noexcept
        {
            if(env->stop_token.stop_requested())
            {
                canceled_ = true;
                return h;
            }
            cont_.h = h;
            ex_ = env->executor;
            waker_->waiter_ = this;

            int expected = state_empty;
            if(!waker_->st_.compare_exchange_strong(
                expected, state_armed,
                std::memory_order_acq_rel,
                std::memory_order_acquire))
            {
                // Single-waiter precondition: a second concurrent
                // wait would find the slot armed.
                BOOST_CAPY_ASSERT(expected == state_token);

                // A wake latched between await_ready and here;
                // consume it and resume inline.
                waker_->st_.store(
                    state_empty, std::memory_order_release);
                return h;
            }
            published_ = true;

            ::new(stop_cb_buf_) stop_cb_t(
                env->stop_token, cancel_fn{this});
            active_ = true;
            return std::noop_coroutine();
        }

        io_result<> await_resume() noexcept
        {
            if(active_)
            {
                stop_cb_().~stop_cb_t();
                active_ = false;
            }
            published_ = false;
            if(canceled_)
                return {make_error_code(error::canceled)};
            return {{}};
        }
    };

    /// Construct with no token latched.
    async_waker() = default;

    /// Copy constructor (deleted).
    async_waker(async_waker const&) = delete;

    /// Copy assignment (deleted).
    async_waker& operator=(async_waker const&) = delete;

    /// Move constructor (deleted).
    async_waker(async_waker&&) = delete;

    /// Move assignment (deleted).
    async_waker& operator=(async_waker&&) = delete;

    /** Asynchronously wait until woken.

        If a token is latched, completes immediately and consumes
        it. Otherwise suspends until `wake()` or the stop token
        fires.

        @par Preconditions
        No other coroutine is currently waiting on this object.

        @return An awaitable that await-returns `io_result<>`;
            empty on wakeup, `error::canceled` if the stop
            token wins.
    */
    wait_awaiter wait() noexcept
    {
        return wait_awaiter{this};
    }

    /** Wake the waiter, or latch the wakeup if none waits.

        Callable from any thread. The waiter's resumption is
        posted through its executor; this call never resumes a
        coroutine inline. Multiple calls without an intervening
        `wait()` collapse into a single token.
    */
    void wake() noexcept
    {
        for(;;)
        {
            int s = st_.load(std::memory_order_acquire);
            if(s == state_token)
                return;
            if(s == state_empty)
            {
                if(st_.compare_exchange_weak(
                    s, state_token,
                    std::memory_order_acq_rel,
                    std::memory_order_acquire))
                    return;
                continue;
            }
            // armed: winning this CAS claims the waiter, whose
            // frame is pinned until we post its resumption.
            if(st_.compare_exchange_weak(
                s, state_empty,
                std::memory_order_acq_rel,
                std::memory_order_acquire))
            {
                auto* w = waiter_;
                w->ex_.post(w->cont_);
                return;
            }
        }
    }
};

} // namespace capy
} // namespace boost

#endif
