//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_ASYNC_MUTEX_HPP
#define BOOST_CAPY_ASYNC_MUTEX_HPP

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

/*  async_mutex implementation notes
    ================================

    Waiters form a doubly-linked intrusive list (fair FIFO). lock_awaiter
    inherits intrusive_list<lock_awaiter>::node; the list is owned by
    async_mutex::waiters_.

    Cancellation via stop_token
    ---------------------------
    A std::stop_callback is registered in await_suspend. Two actors can
    race to resume the suspended coroutine: unlock() and the stop callback.
    An atomic bool `claimed_` resolves the race -- whoever does
    claimed_.exchange(true) and reads false wins. The loser does nothing.

    The stop callback calls ex_.post(h_). The stop_callback is
    destroyed later in await_resume. cancel_fn touches no members
    after post returns (same pattern as delete-this).

    unlock() pops waiters from the front. If the popped waiter was
    already claimed by the stop callback, unlock() skips it and tries
    the next. await_resume removes the (still-linked) canceled waiter
    via waiters_.remove(this).

    The stop_callback lives in a union to suppress automatic
    construction/destruction. Placement new in await_suspend, explicit
    destructor call in await_resume and ~lock_awaiter.

    Member ordering constraint
    --------------------------
    The union containing stop_cb_ must be declared AFTER the members
    the callback accesses (h_, ex_, claimed_, canceled_). If the
    stop_cb_ destructor blocks waiting for a concurrent callback, those
    members must still be alive (C++ destroys in reverse declaration
    order).

    active_ flag
    ------------
    Tracks both list membership and stop_cb_ lifetime (they are always
    set and cleared together). Used by the destructor to clean up if the
    coroutine is destroyed while suspended (e.g. execution_context
    shutdown).

    Cancellation scope
    ------------------
    Cancellation only takes effect while the coroutine is suspended in
    the wait queue. If the mutex is unlocked, await_ready acquires it
    immediately without checking the stop token. This is intentional:
    the fast path has no token access and no overhead.

    Threading assumptions
    ---------------------
    - All list mutations happen on the executor thread (await_suspend,
      await_resume, unlock, ~lock_awaiter).
    - The stop callback may fire from any thread, but only touches
      claimed_ (atomic) and then calls post. It never touches the
      list.
    - ~lock_awaiter must be called from the executor thread. This is
      guaranteed during normal shutdown but NOT if the coroutine frame
      is destroyed from another thread while a stop callback could
      fire (precondition violation, same as cppcoro/folly).
*/

namespace boost {
namespace capy {

/** An asynchronous mutex for coroutines.

    This mutex provides mutual exclusion for coroutines without blocking.
    When a coroutine attempts to acquire a locked mutex, it suspends and
    is added to an intrusive wait queue. When the holder unlocks, the next
    waiter is resumed with the lock held.

    @par Cancellation

    When a coroutine is suspended waiting for the mutex and its stop
    token is triggered, the waiter completes with `error::canceled`
    instead of acquiring the lock.

    Cancellation only applies while the coroutine is suspended in the
    wait queue. If the mutex is unlocked when `lock()` is called, the
    lock is acquired immediately even if the stop token is already
    signaled.

    @par Zero Allocation

    No heap allocation occurs for lock operations.

    @par Thread Safety

    Distinct objects: Safe.@n
    Shared objects: Unsafe.

    The mutex operations are designed for single-threaded use on one
    executor. The stop callback may fire from any thread.

    This type is non-copyable and non-movable because suspended
    waiters hold intrusive pointers into the mutex's internal list.

    @par Example
    @code
    async_mutex cm;

    task<> protected_operation() {
        auto [ec] = co_await cm.lock();
        if(ec)
            co_return;
        // ... critical section ...
        cm.unlock();
    }

    // Or with RAII:
    task<> protected_operation() {
        auto [ec, guard] = co_await cm.scoped_lock();
        if(ec)
            co_return;
        // ... critical section ...
        // unlocks automatically
    }
    @endcode
*/
class async_mutex
{
public:
    class lock_awaiter;
    class lock_guard;
    class lock_guard_awaiter;

private:
    bool locked_ = false;
    detail::intrusive_list<lock_awaiter> waiters_;

public:
    /** Awaiter returned by lock().
    */
    class lock_awaiter
        : public detail::intrusive_list<lock_awaiter>::node
    {
        friend class async_mutex;

        async_mutex* m_;
        continuation cont_;
        executor_ref ex_;

        // These members must be declared before stop_cb_
        // (see comment on the union below).
        std::atomic<bool> claimed_{false};
        bool canceled_ = false;
        bool active_ = false;

        struct cancel_fn
        {
            lock_awaiter* self_;

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
        ~lock_awaiter()
        {
            if(active_)
            {
                stop_cb_().~stop_cb_t();
                m_->waiters_.remove(this);
            }
        }

        explicit lock_awaiter(async_mutex* m) noexcept
            : m_(m)
        {
        }

        lock_awaiter(lock_awaiter&& o) noexcept
            : m_(o.m_)
            , cont_(o.cont_)
            , ex_(o.ex_)
            , claimed_(o.claimed_.load(
                std::memory_order_relaxed))
            , canceled_(o.canceled_)
            , active_(std::exchange(o.active_, false))
        {
        }

        lock_awaiter(lock_awaiter const&) = delete;
        lock_awaiter& operator=(lock_awaiter const&) = delete;
        lock_awaiter& operator=(lock_awaiter&&) = delete;

        bool await_ready() const noexcept
        {
            if(!m_->locked_)
            {
                m_->locked_ = true;
                return true;
            }
            return false;
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
            m_->waiters_.push_back(this);
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
                if(canceled_)
                {
                    m_->waiters_.remove(this);
                    active_ = false;
                    return {make_error_code(
                        error::canceled)};
                }
                active_ = false;
            }
            if(canceled_)
                return {make_error_code(
                    error::canceled)};
            return {{}};
        }
    };

    /** RAII lock guard for async_mutex.

        Automatically unlocks the mutex when destroyed.
    */
    class [[nodiscard]] lock_guard
    {
        async_mutex* m_;

    public:
        ~lock_guard()
        {
            if(m_)
                m_->unlock();
        }

        lock_guard() noexcept
            : m_(nullptr)
        {
        }

        explicit lock_guard(async_mutex* m) noexcept
            : m_(m)
        {
        }

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
        async_mutex* m_;
        lock_awaiter inner_;

    public:
        explicit lock_guard_awaiter(async_mutex* m) noexcept
            : m_(m)
            , inner_(m)
        {
        }

        bool await_ready() const noexcept
        {
            return inner_.await_ready();
        }

        /** IoAwaitable protocol overload. */
        std::coroutine_handle<>
        await_suspend(
            std::coroutine_handle<> h,
            io_env const* env) noexcept
        {
            return inner_.await_suspend(h, env);
        }

        io_result<lock_guard> await_resume() noexcept
        {
            auto r = inner_.await_resume();
            if(r.ec)
                return {r.ec, {}};
            return {{}, lock_guard(m_)};
        }
    };

    /// Construct an unlocked mutex.
    async_mutex() = default;

    /// Copy constructor (deleted).
    async_mutex(async_mutex const&) = delete;

    /// Copy assignment (deleted).
    async_mutex& operator=(async_mutex const&) = delete;

    /// Move constructor (deleted).
    async_mutex(async_mutex&&) = delete;

    /// Move assignment (deleted).
    async_mutex& operator=(async_mutex&&) = delete;

    /** Returns an awaiter that acquires the mutex.

        @return An awaitable that await-returns `(error_code)`.
    */
    lock_awaiter lock() noexcept
    {
        return lock_awaiter{this};
    }

    /** Returns an awaiter that acquires the mutex with RAII.

        @return An awaitable that await-returns `(error_code,lock_guard)`.
    */
    lock_guard_awaiter scoped_lock() noexcept
    {
        return lock_guard_awaiter(this);
    }

    /** Releases the mutex.

        If waiters are queued, the next eligible waiter is
        resumed with the lock held. Canceled waiters are
        skipped. If no eligible waiter remains, the mutex
        becomes unlocked.
    */
    void unlock() noexcept
    {
        for(;;)
        {
            auto* waiter = waiters_.pop_front();
            if(!waiter)
            {
                locked_ = false;
                return;
            }
            if(!waiter->claimed_.exchange(
                true, std::memory_order_acq_rel))
            {
                waiter->ex_.post(waiter->cont_);
                return;
            }
        }
    }

    /** Returns true if the mutex is currently locked.
    */
    bool is_locked() const noexcept
    {
        return locked_;
    }
};

} // namespace capy
} // namespace boost

#endif
