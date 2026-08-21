//
// Copyright (c) 2026 Steve Gerbino
// Copyright (c) 2026 Michael Vandeberg
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
    `wait()` must only be awaited by one coroutine at a time. The
    executor must never run the coroutine's continuations
    concurrently: use a single-threaded executor, or a strand over
    a multi-threaded one. That is the same threading model as
    `async_event` and `async_mutex`. Awaiting `wait()` directly
    on a multi-threaded executor is undefined.

    This type is non-copyable and non-movable because a suspended
    waiter holds a pointer into the object.

    @par Example
    @par !example example

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
    /** Suspends the caller until `wake()` runs, or resumes it with `error::canceled` on a stop request.
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
        /** Destroy the awaiter, leaving the waker unable to reach it.

            Destroys the stop callback if one is registered. If the awaiter
            is still armed, it also returns the waker's slot to the empty
            state, so a later `wake()` cannot dereference a destroyed
            awaiter. That case means the frame is being torn down without
            ever being resumed; a wake arriving afterward latches a token
            instead.
        */
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

        /** Construct an awaiter for the given waker.

            @param waker The waker to wait on. It must outlive the awaiter.
        */
        explicit wait_awaiter(async_waker* waker) noexcept
            : waker_(waker)
        {
        }

        /** Construct by moving.

            The moved-from awaiter is left inert: its destructor no longer
            destroys the stop callback and no longer deregisters from the
            waker.

            @param o The awaiter to move from.
        */
        wait_awaiter(wait_awaiter&& o) noexcept
            : waker_(o.waker_)
            , cont_(o.cont_)
            , ex_(o.ex_)
            , canceled_(o.canceled_)
            , active_(std::exchange(o.active_, false))
            , published_(std::exchange(o.published_, false))
        {
        }

        /** Copy construction is disabled; an armed waiter is registered
            with the waker by address.

            @param other The awaiter that would be copied.
        */
        wait_awaiter(wait_awaiter const& other) = delete;

        /** Copy assignment is disabled; an armed waiter is registered
            with the waker by address.

            @param other The awaiter that would be assigned from.

            @return A reference to `*this`.
        */
        wait_awaiter& operator=(wait_awaiter const& other) = delete;

        /** Move assignment is disabled; an armed waiter is registered
            with the waker by address.

            @param other The awaiter that would be moved from.

            @return A reference to `*this`.
        */
        wait_awaiter& operator=(wait_awaiter&& other) = delete;

        /** Consume a latched token, completing synchronously.

            This is not a pure query: the check is a compare-exchange that
            takes the token. Calling it twice is not idempotent: the second
            call reports `false`, because the first already consumed the
            wakeup.

            @return `true` if a pending wakeup token was latched and has now
            been consumed, in which case the awaiting coroutine does not
            suspend; otherwise `false`.
        */
        bool await_ready() noexcept
        {
            int expected = state_token;
            return waker_->st_.compare_exchange_strong(
                expected, state_empty,
                std::memory_order_acq_rel,
                std::memory_order_acquire);
        }

        /** Arm the waker with the awaiting coroutine.

            This is the @ref IoAwaitable overload of `await_suspend`.
            Unlike `async_event` and `async_mutex`, it has three outcomes,
            because a `wake()` from another thread can land in the window
            between `await_ready` and this call.

            @li A stop request is already pending on `env->stop_token`: the
                awaiter records the cancellation and does not arm.

            @li The waker's slot is no longer empty. Under the single-waiter
                precondition that means a wakeup was latched after
                `await_ready` looked, so the token is consumed here instead
                and the wait succeeds.

            @li Otherwise the slot moves to the armed state, publishing this
                awaiter to the waker, and a stop callback is registered on
                `env->stop_token`. Whichever of `wake()` and that callback
                wins the armed-to-empty transition posts `h` through
                `env->executor`. The loser does nothing, and a losing
                `wake()` re-latches its token for the next `wait()`.

            @param h The awaiting coroutine, resumed when the waker fires
            or the wait is canceled.

            @param env The execution environment. Its executor posts the
            resumption and its stop token is watched for the duration of
            the wait. It must outlive the wait.

            @return `h` in the first two cases, which resumes the awaiting
            coroutine immediately; otherwise `std::noop_coroutine()`, which
            leaves the coroutine suspended and returns control to the
            resumer.
        */
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

        /** Complete the wait and report the outcome.

            Destroys the stop callback if one is registered and clears the
            armed bookkeeping, so the destructor does not deregister a slot
            the resumption already consumed.

            @return An empty `io_result<>` if the wait was woken, whether by
            `wake()` or by a token consumed inline. Otherwise one holding
            `error::canceled`, which means the stop token won the race.
        */
        [[nodiscard]] io_result<> await_resume() noexcept
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

    /** Copy construction is disabled; an armed waiter points into the
        waker.

        @param other The waker that would be copied.
    */
    async_waker(async_waker const& other) = delete;

    /** Copy assignment is disabled; an armed waiter points into the waker.

        @param other The waker that would be assigned from.

        @return A reference to `*this`.
    */
    async_waker& operator=(async_waker const& other) = delete;

    /** Move construction is disabled; an armed waiter points into the
        waker.

        @param other The waker that would be moved from.
    */
    async_waker(async_waker&& other) = delete;

    /** Move assignment is disabled; an armed waiter points into the waker.

        @param other The waker that would be moved from.

        @return A reference to `*this`.
    */
    async_waker& operator=(async_waker&& other) = delete;

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
