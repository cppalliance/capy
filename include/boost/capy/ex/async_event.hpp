//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
// Copyright (c) 2026 Michael Vandeberg
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_ASYNC_EVENT_HPP
#define BOOST_CAPY_ASYNC_EVENT_HPP

#include <boost/capy/detail/config.hpp>
#include <boost/capy/detail/intrusive.hpp>
#include <boost/capy/continuation.hpp>
#include <boost/capy/concept/executor.hpp>
#include <boost/capy/error.hpp>
#include <boost/capy/ex/io_env.hpp>
#include <boost/capy/io_result.hpp>

#include <stop_token>

#include <atomic>
#include <coroutine>
#include <new>
#include <utility>

/*  async_event implementation notes
    =================================

    Same cancellation pattern as async_mutex (see that file for the
    full discussion on claimed_, stop_cb lifetime, member ordering,
    and threading assumptions).

    Key difference: set() wakes ALL waiters (broadcast), not one.
    It pops every waiter from the list and posts the ones it
    claims. Waiters already claimed by a stop callback are skipped.

    Because set() pops all waiters, a canceled waiter may have been
    removed from the list by set() before its await_resume runs.
    This requires a separate in_list_ flag (unlike async_mutex where
    active_ served double duty). await_resume only calls remove()
    when in_list_ is true.
*/

namespace boost {
namespace capy {

/** Queues coroutines in `wait()` and resumes all of them when `set()` is called.

    This event provides a way to notify multiple coroutines that some
    condition has occurred. When a coroutine awaits an unset event, it
    suspends and is added to a wait queue. When the event is set, all
    waiting coroutines are resumed.

    @par Cancellation

    When a coroutine is suspended waiting for the event and its stop
    token is triggered, the waiter completes with `error::canceled`
    instead of waiting for `set()`.

    Cancellation only applies while the coroutine is suspended in the
    wait queue. If the event is already set when `wait()` is called,
    the wait completes immediately even if the stop token is already
    signaled.

    @par Zero Allocation

    No heap allocation occurs for wait operations.

    @par Thread Safety

    Distinct objects: Safe.@n
    Shared objects: Unsafe.

    The event operations are designed for single-threaded use on one
    executor. The stop callback may fire from any thread.

    This type is non-copyable and non-movable because suspended
    waiters hold intrusive pointers into the event's internal list.

    @par Example
    @par !example example

*/
class async_event
{
public:
    class wait_awaiter;

private:
    bool set_ = false;
    detail::intrusive_list<wait_awaiter> waiters_;

public:
    /** Suspends the caller until `set()` runs, or resumes it with `error::canceled` on a stop request.
    */
    class wait_awaiter
        : public detail::intrusive_list<wait_awaiter>::node
    {
        friend class async_event;

        async_event* e_;
        continuation cont_;
        executor_ref ex_;

        // Declared before stop_cb_buf_: the callback
        // accesses these members, so they must still be
        // alive if the stop_cb_ destructor blocks.
        std::atomic<bool> claimed_{false};
        bool canceled_ = false;
        bool active_ = false;
        bool in_list_ = false;

        struct cancel_fn
        {
            wait_awaiter* self_;

            void operator()() const noexcept
            {
                if(!self_->claimed_.exchange(
                    true, std::memory_order_acq_rel))
                {
                    self_->canceled_ = true;
                    self_->ex_.post(self_->cont_);
                }
            }
        };

        using stop_cb_t =
            std::stop_callback<cancel_fn>;

        // Aligned storage for stop_cb_t. Declared last:
        // its destructor may block while the callback
        // accesses the members above.
        BOOST_CAPY_MSVC_WARNING_PUSH
        BOOST_CAPY_MSVC_WARNING_DISABLE(4324) // padded due to alignas
        alignas(stop_cb_t)
            unsigned char stop_cb_buf_[sizeof(stop_cb_t)];
        BOOST_CAPY_MSVC_WARNING_POP

        stop_cb_t& stop_cb_() noexcept
        {
            return *reinterpret_cast<stop_cb_t*>(
                stop_cb_buf_);
        }

    public:
        /** Destroy the awaiter, leaving the event unable to reach it.

            Destroys the stop callback if one is registered, and unlinks
            the awaiter from the event's wait queue if it is still linked.
            Both are necessary when the coroutine frame is torn down while
            suspended, so that neither `set()` nor the stop callback can
            reach a destroyed awaiter.
        */
        ~wait_awaiter()
        {
            if(active_)
                stop_cb_().~stop_cb_t();
            if(in_list_)
                e_->waiters_.remove(this);
        }

        /** Construct an awaiter for the given event.

            @param e The event to wait on. It must outlive the awaiter.
        */
        explicit wait_awaiter(async_event* e) noexcept
            : e_(e)
        {
        }

        /** Construct by moving.

            The moved-from awaiter is left inert: its destructor no longer
            destroys the stop callback and no longer unlinks from the
            event's wait queue.

            @param o The awaiter to move from.
        */
        wait_awaiter(wait_awaiter&& o) noexcept
            : e_(o.e_)
            , cont_(o.cont_)
            , ex_(o.ex_)
            , claimed_(o.claimed_.load(
                std::memory_order_relaxed))
            , canceled_(o.canceled_)
            , active_(std::exchange(o.active_, false))
            , in_list_(std::exchange(o.in_list_, false))
        {
        }

        /** Copy construction is disabled; a waiter is linked into the
            event's wait queue by address.

            @param other The awaiter that would be copied.
        */
        wait_awaiter(wait_awaiter const& other) = delete;

        /** Copy assignment is disabled; a waiter is linked into the
            event's wait queue by address.

            @param other The awaiter that would be assigned from.

            @return A reference to `*this`.
        */
        wait_awaiter& operator=(wait_awaiter const& other) = delete;

        /** Move assignment is disabled; a waiter is linked into the
            event's wait queue by address.

            @param other The awaiter that would be moved from.

            @return A reference to `*this`.
        */
        wait_awaiter& operator=(wait_awaiter&& other) = delete;

        /** Report whether the event is already set.

            @return `true` if the event is set, in which case the awaiting
            coroutine does not suspend; otherwise `false`.
        */
        bool await_ready() const noexcept
        {
            return e_->set_;
        }

        /** Enqueue the awaiting coroutine until the event is set.

            This is the @ref IoAwaitable overload of `await_suspend`.

            If a stop request is already pending on `env->stop_token`, the
            awaiter records the cancellation and does not enqueue.

            Otherwise it stores `h` and `env->executor`, links itself into
            the event's wait queue, and registers a stop callback on
            `env->stop_token`. Exactly one of `set()` and that callback posts
            `h` through the stored executor, whichever claims the waiter
            first. Only the post is subject to that race. A losing stop
            callback does nothing at all, but `set()` unlinks every waiter it
            pops whether it claims it or not. That is why @ref await_resume
            unlinks a canceled waiter only when it is still linked.

            @param h The awaiting coroutine, resumed when the event is set
            or the wait is canceled.

            @param env The execution environment. Its executor posts the
            resumption and its stop token is watched for the duration of
            the wait. It must outlive the wait.

            @return `h` if a stop request was already pending, which
            resumes the awaiting coroutine immediately without enqueuing
            it. Otherwise `std::noop_coroutine()`, which leaves the
            coroutine suspended and returns control to the resumer.
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
            e_->waiters_.push_back(this);
            in_list_ = true;
            ::new(stop_cb_buf_) stop_cb_t(
                env->stop_token, cancel_fn{this});
            active_ = true;
            return std::noop_coroutine();
        }

        /** Complete the wait and report the outcome.

            Destroys the stop callback if one is registered. If the wait
            was canceled while still linked into the event's wait queue,
            unlinks it. `set()` pops every waiter, so a canceled waiter may
            or may not still be linked when it resumes.

            @return An empty `io_result<>` if the event was set, or one
            holding `error::canceled` if the stop token fired first.
        */
        [[nodiscard]] io_result<> await_resume() noexcept
        {
            if(active_)
            {
                stop_cb_().~stop_cb_t();
                active_ = false;
            }
            if(canceled_)
            {
                if(in_list_)
                {
                    e_->waiters_.remove(this);
                    in_list_ = false;
                }
                return {make_error_code(
                    error::canceled)};
            }
            return {{}};
        }
    };

    /// Construct an unset event.
    async_event() = default;

    /** Copy construction is disabled; suspended waiters point into the
        event's wait queue.

        @param other The event that would be copied.
    */
    async_event(async_event const& other) = delete;

    /** Copy assignment is disabled; suspended waiters point into the
        event's wait queue.

        @param other The event that would be assigned from.

        @return A reference to `*this`.
    */
    async_event& operator=(async_event const& other) = delete;

    /** Move construction is disabled; suspended waiters point into the
        event's wait queue.

        @param other The event that would be moved from.
    */
    async_event(async_event&& other) = delete;

    /** Move assignment is disabled; suspended waiters point into the
        event's wait queue.

        @param other The event that would be moved from.

        @return A reference to `*this`.
    */
    async_event& operator=(async_event&& other) = delete;

    /** Returns an awaiter that waits until the event is set.

        If the event is already set, completes immediately.

        @return An awaitable that await-returns `(error_code)`.
    */
    wait_awaiter wait() noexcept
    {
        return wait_awaiter{this};
    }

    /** Resumes every waiting coroutine and marks the event set for later `wait()` calls.

        All waiting coroutines are resumed. Canceled waiters
        are skipped. Subsequent calls to wait() complete
        immediately until clear() is called.
    */
    void set()
    {
        set_ = true;
        for(;;)
        {
            auto* w = waiters_.pop_front();
            if(!w)
                break;
            w->in_list_ = false;
            if(!w->claimed_.exchange(
                true, std::memory_order_acq_rel))
            {
                w->ex_.post(w->cont_);
            }
        }
    }

    /** Clears the event.

        Subsequent calls to wait() suspend until
        set() is called again.
    */
    void clear() noexcept
    {
        set_ = false;
    }

    /** Returns true if the event is currently set.

        @return `true` if the event is set; otherwise `false`.
    */
    bool is_set() const noexcept
    {
        return set_;
    }
};

} // namespace capy
} // namespace boost

#endif
