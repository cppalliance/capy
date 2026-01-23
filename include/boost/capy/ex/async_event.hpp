//
// Copyright (c) 2025 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_ASYNC_EVENT_HPP
#define BOOST_CAPY_ASYNC_EVENT_HPP

#include <boost/capy/detail/config.hpp>
#include <boost/capy/concept/executor.hpp>
#include <boost/capy/coro.hpp>
#include <boost/capy/ex/executor_ref.hpp>

#include <stop_token>

#include <coroutine>
#include <utility>

namespace boost {
namespace capy {

/** An asynchronous event for coroutines.

    This event provides a way to notify multiple coroutines that some
    condition has occurred. When a coroutine awaits an unset event, it
    suspends and is added to an intrusive wait queue. When the event is
    set, all waiting coroutines are resumed.

    @par Zero Allocation

    The wait queue node is embedded in the wait_awaiter, which lives on
    the coroutine frame during suspension. No heap allocation occurs.

    @par Thread Safety

    This event is NOT thread-safe. It is designed for single-threaded
    use where multiple coroutines may wait for a condition.

    @par Example
    @code
    async_event event;

    task<> waiter() {
        co_await event.wait();
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

    /** Awaiter returned by wait().

        The awaiter contains the intrusive list node, which is stored
        on the coroutine frame during suspension.
    */
    class wait_awaiter
    {
        friend class async_event;

        async_event* e_;
        wait_awaiter* next_ = nullptr;
        std::coroutine_handle<> h_;
        executor_ref ex_;

    public:
        explicit wait_awaiter(async_event* e) noexcept
            : e_(e)
        {
        }

        bool await_ready() const noexcept
        {
            return e_->set_;
        }

        bool await_suspend(std::coroutine_handle<> h) noexcept
        {
            h_ = h;
            ex_ = {};
            if(e_->tail_)
                e_->tail_->next_ = this;
            else
                e_->head_ = this;
            e_->tail_ = this;
            return true;
        }

        /** IoAwaitable protocol overload. */
        template<Executor Ex>
        auto await_suspend(
            std::coroutine_handle<> h,
            Ex const& ex,
            std::stop_token = {}) noexcept -> std::coroutine_handle<>
        {
            h_ = h;
            ex_ = ex;
            if(e_->tail_)
                e_->tail_->next_ = this;
            else
                e_->head_ = this;
            e_->tail_ = this;
            return std::noop_coroutine();
        }

        void await_resume() const noexcept
        {
        }
    };

    async_event() = default;

    // Non-copyable, non-movable
    async_event(async_event const&) = delete;
    async_event& operator=(async_event const&) = delete;

    /** Returns an awaiter that waits until the event is set.

        If the event is already set, returns immediately.
        Otherwise suspends until set() is called.

        @return An awaiter that suspends if the event is not set,
                or completes immediately if set.
    */
    wait_awaiter wait() noexcept
    {
        return wait_awaiter{this};
    }

    /** Sets the event.

        All tasks waiting for the event will be immediately awakened.
        Subsequent calls to wait() will return immediately until
        clear() is called.
    */
    void set() noexcept
    {
        set_ = true;
        while(head_)
        {
            auto* waiter = head_;
            head_ = head_->next_;
            if(waiter->ex_)
                waiter->ex_.dispatch(coro{waiter->h_}).resume();
            else
                waiter->h_.resume();
        }
        tail_ = nullptr;
    }

    /** Clears the event.

        Subsequent calls to wait() will block until set() is called again.
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

private:
    bool set_ = false;
    wait_awaiter* head_ = nullptr;
    wait_awaiter* tail_ = nullptr;
};

} // namespace capy
} // namespace boost

#endif
