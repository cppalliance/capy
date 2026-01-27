//
// Copyright (c) 2025 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_CORO_LOCK_HPP
#define BOOST_CAPY_CORO_LOCK_HPP

#include <boost/capy/detail/config.hpp>
#include <boost/capy/concept/executor.hpp>
#include <boost/capy/coro.hpp>
#include <boost/capy/ex/executor_ref.hpp>

#include <stop_token>

#include <coroutine>
#include <utility>

namespace boost {
namespace capy {

/** An asynchronous mutex for coroutines.

    This mutex provides mutual exclusion for coroutines without blocking.
    When a coroutine attempts to acquire a locked mutex, it suspends and
    is added to an intrusive wait queue. When the holder unlocks, the next
    waiter is resumed with the lock held.

    @par Zero Allocation

    The wait queue node is embedded in the lock_awaiter, which lives on
    the coroutine frame during suspension. No heap allocation occurs.

    @par Thread Safety

    This mutex is NOT thread-safe. It is designed for single-threaded
    use where multiple coroutines may contend for a resource.

    @par Example
    @code
    coro_lock cm;

    task<> protected_operation() {
        co_await cm.lock();
        // ... critical section ...
        cm.unlock();
    }

    // Or with RAII:
    task<> protected_operation() {
        auto guard = co_await cm.scoped_lock();
        // ... critical section ...
        // unlocks automatically
    }
    @endcode
*/
class coro_lock
{
public:
    class lock_awaiter;
    class lock_guard;
    class lock_guard_awaiter;

    /** Awaiter returned by lock().

        The awaiter contains the intrusive list node, which is stored
        on the coroutine frame during suspension.
    */
    class lock_awaiter
    {
        friend class coro_lock;

        coro_lock* m_;
        lock_awaiter* next_ = nullptr;
        std::coroutine_handle<> h_;
        executor_ref ex_;

    public:
        explicit lock_awaiter(coro_lock* m) noexcept
            : m_(m)
        {
        }

        bool await_ready() const noexcept
        {
            if(!m_->locked_)
            {
                m_->locked_ = true;
                return true;  // Fast path - acquire immediately
            }
            return false;
        }

        bool await_suspend(std::coroutine_handle<> h) noexcept
        {
            h_ = h;
            ex_ = {};
            if(m_->tail_)
                m_->tail_->next_ = this;
            else
                m_->head_ = this;
            m_->tail_ = this;
            return true;
        }

        /** IoAwaitable protocol overload. */
        std::coroutine_handle<>
        await_suspend(
            std::coroutine_handle<> h,
            executor_ref ex,
            std::stop_token = {}) noexcept
        {
            h_ = h;
            ex_ = ex;
            if(m_->tail_)
                m_->tail_->next_ = this;
            else
                m_->head_ = this;
            m_->tail_ = this;
            return std::noop_coroutine();
        }

        void await_resume() const noexcept
        {
            // Lock already acquired by unlock() or await_ready()
        }
    };

    /** RAII lock guard for coro_lock.

        Automatically unlocks the mutex when destroyed.
    */
    class [[nodiscard]] lock_guard
    {
        coro_lock* m_;

    public:
        explicit lock_guard(coro_lock* m) noexcept
            : m_(m)
        {
        }

        ~lock_guard()
        {
            if(m_)
                m_->unlock();
        }

        // Move-only
        lock_guard(lock_guard&& o) noexcept
            : m_(std::exchange(o.m_, nullptr))
        {
        }

        lock_guard& operator=(lock_guard&& o) noexcept
        {
            if(this != &o)
            {
                if(m_)
                    m_->unlock();
                m_ = std::exchange(o.m_, nullptr);
            }
            return *this;
        }

        lock_guard(lock_guard const&) = delete;
        lock_guard& operator=(lock_guard const&) = delete;
    };

    /** Awaiter returned by scoped_lock() that returns a lock_guard on resume.
    */
    class lock_guard_awaiter
    {
        coro_lock* m_;
        lock_awaiter inner_;

    public:
        explicit lock_guard_awaiter(coro_lock* m) noexcept
            : m_(m)
            , inner_(m)
        {
        }

        bool await_ready() const noexcept
        {
            return inner_.await_ready();
        }

        bool await_suspend(std::coroutine_handle<> h) noexcept
        {
            return inner_.await_suspend(h);
        }

        /** IoAwaitable protocol overload. */
        std::coroutine_handle<>
        await_suspend(
            std::coroutine_handle<> h,
            executor_ref ex,
            std::stop_token token = {}) noexcept
        {
            return inner_.await_suspend(h, ex, token);
        }

        lock_guard await_resume() noexcept
        {
            return lock_guard(m_);
        }
    };

    coro_lock() = default;

    // Non-copyable, non-movable
    coro_lock(coro_lock const&) = delete;
    coro_lock& operator=(coro_lock const&) = delete;

    /** Returns an awaiter that acquires the mutex.

        @return An awaiter that suspends if the mutex is locked,
                or completes immediately if unlocked.
    */
    lock_awaiter lock() noexcept
    {
        return lock_awaiter{this};
    }

    /** Returns an awaiter that acquires the mutex with RAII.

        @return An awaiter that returns a lock_guard on resume.
    */
    lock_guard_awaiter scoped_lock() noexcept
    {
        return lock_guard_awaiter(this);
    }

    /** Releases the mutex.

        If waiters are queued, the next waiter is resumed with the
        lock held. Otherwise, the mutex becomes unlocked.
    */
    void unlock() noexcept
    {
        if(head_)
        {
            auto* waiter = head_;
            head_ = head_->next_;
            if(!head_)
                tail_ = nullptr;
            // Lock ownership transfers to next waiter
            if(waiter->ex_)
                waiter->ex_.dispatch(coro{waiter->h_}).resume();
            else
                waiter->h_.resume();
        }
        else
        {
            locked_ = false;
        }
    }

    /** Returns true if the mutex is currently locked.
    */
    bool is_locked() const noexcept
    {
        return locked_;
    }

private:
    bool locked_ = false;
    lock_awaiter* head_ = nullptr;
    lock_awaiter* tail_ = nullptr;
};

} // namespace capy
} // namespace boost

#endif
