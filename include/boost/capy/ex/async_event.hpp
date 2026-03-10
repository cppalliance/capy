//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
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

/** An asynchronous event for coroutines.

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
    @code
    async_event event;

    task<> waiter() {
        auto [ec] = co_await event.wait();
        if(ec)
            co_return;
        // ... event was set ...
    }

    task<> notifier() {
        // ... do some work ...
        event.set();  // Wake all waiters
    }
    @endcode
*/
class async_event
{
public:
    class wait_awaiter;

private:
    bool set_ = false;
    detail::intrusive_list<wait_awaiter> waiters_;

public:
    /** Awaiter returned by wait().
    */
    class wait_awaiter
        : public detail::intrusive_list<wait_awaiter>::node
    {
        friend class async_event;

        async_event* e_;
        std::coroutine_handle<> h_;
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
                    self_->ex_.post(self_->h_);
                }
            }
        };

        using stop_cb_t =
            std::stop_callback<cancel_fn>;

        // Aligned storage for stop_cb_t. Declared last:
        // its destructor may block while the callback
        // accesses the members above.
#ifdef _MSC_VER
# pragma warning(push)
# pragma warning(disable: 4324) // padded due to alignas
#endif
        alignas(stop_cb_t)
            unsigned char stop_cb_buf_[sizeof(stop_cb_t)];
#ifdef _MSC_VER
# pragma warning(pop)
#endif

        stop_cb_t& stop_cb_() noexcept
        {
            return *reinterpret_cast<stop_cb_t*>(
                stop_cb_buf_);
        }

    public:
        ~wait_awaiter()
        {
            if(active_)
                stop_cb_().~stop_cb_t();
            if(in_list_)
                e_->waiters_.remove(this);
        }

        explicit wait_awaiter(async_event* e) noexcept
            : e_(e)
        {
        }

        wait_awaiter(wait_awaiter&& o) noexcept
            : e_(o.e_)
            , h_(o.h_)
            , ex_(o.ex_)
            , claimed_(o.claimed_.load(
                std::memory_order_relaxed))
            , canceled_(o.canceled_)
            , active_(std::exchange(o.active_, false))
            , in_list_(std::exchange(o.in_list_, false))
        {
        }

        wait_awaiter(wait_awaiter const&) = delete;
        wait_awaiter& operator=(wait_awaiter const&) = delete;
        wait_awaiter& operator=(wait_awaiter&&) = delete;

        bool await_ready() const noexcept
        {
            return e_->set_;
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
            h_ = h;
            ex_ = env->executor;
            e_->waiters_.push_back(this);
            in_list_ = true;
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

    /// Copy constructor (deleted).
    async_event(async_event const&) = delete;

    /// Copy assignment (deleted).
    async_event& operator=(async_event const&) = delete;

    /// Move constructor (deleted).
    async_event(async_event&&) = delete;

    /// Move assignment (deleted).
    async_event& operator=(async_event&&) = delete;

    /** Returns an awaiter that waits until the event is set.

        If the event is already set, completes immediately.

        @return An awaitable that await-returns `(error_code)`.
    */
    wait_awaiter wait() noexcept
    {
        return wait_awaiter{this};
    }

    /** Sets the event.

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
                w->ex_.post(w->h_);
            }
        }
    }

    /** Clears the event.

        Subsequent calls to wait() will suspend until
        set() is called again.
    */
    void clear() noexcept
    {
        set_ = false;
    }

    /** Returns true if the event is currently set.
    */
    bool is_set() const noexcept
    {
        return set_;
    }
};

} // namespace capy
} // namespace boost

#endif
