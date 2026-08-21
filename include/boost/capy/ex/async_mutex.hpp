//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
// Copyright (c) 2026 Michael Vandeberg
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

/** Queues coroutines in `lock()` and resumes exactly one when the mutex is free.

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
    @par !example example

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
    /** Suspends the caller until the mutex is free, or resumes it with `error::canceled` on a stop request.
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
        /** Destroy the awaiter, leaving the mutex unable to reach it.

            If the awaiter is suspended in the wait queue, destroys the
            stop callback and unlinks the awaiter. Neither `unlock()` nor
            the stop callback can then reach a destroyed awaiter when the
            coroutine frame is torn down while suspended.

            @par Preconditions
            Called on the executor thread. The stop callback may fire from
            any thread, so destroying a still-suspended awaiter from
            another thread is undefined.
        */
        ~lock_awaiter()
        {
            if(active_)
            {
                stop_cb_().~stop_cb_t();
                m_->waiters_.remove(this);
            }
        }

        /** Construct an awaiter for the given mutex.

            @param m The mutex to acquire. It must outlive the awaiter.
        */
        explicit lock_awaiter(async_mutex* m) noexcept
            : m_(m)
        {
        }

        /** Construct by moving.

            The moved-from awaiter is left inert: its destructor no longer
            destroys the stop callback and no longer unlinks from the
            mutex's wait queue.

            @param o The awaiter to move from.
        */
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

        /** Copy construction is disabled; a waiter is linked into the
            mutex's wait queue by address.

            @param other The awaiter that would be copied.
        */
        lock_awaiter(lock_awaiter const& other) = delete;

        /** Copy assignment is disabled; a waiter is linked into the
            mutex's wait queue by address.

            @param other The awaiter that would be assigned from.

            @return A reference to `*this`.
        */
        lock_awaiter& operator=(lock_awaiter const& other) = delete;

        /** Move assignment is disabled; a waiter is linked into the
            mutex's wait queue by address.

            @param other The awaiter that would be moved from.

            @return A reference to `*this`.
        */
        lock_awaiter& operator=(lock_awaiter&& other) = delete;

        /** Acquire the mutex if it is free, reporting whether to suspend.

            This is not a pure query: on the fast path it takes the lock.
            When the mutex is unlocked, it marks the mutex locked and
            reports that no suspension is needed. The stop token is not
            consulted, so an uncontended `lock()` succeeds even when stop
            has already been requested.

            @return `true` if the mutex was free and is now held by the
            awaiting coroutine. `false` if the mutex is held elsewhere, in
            which case the coroutine suspends.
        */
        bool await_ready() const noexcept
        {
            if(!m_->locked_)
            {
                m_->locked_ = true;
                return true;
            }
            return false;
        }

        /** Enqueue the awaiting coroutine until the mutex is released.

            This is the @ref IoAwaitable overload of `await_suspend`.

            If a stop request is already pending on `env->stop_token`, the
            awaiter records the cancellation and does not enqueue. The
            mutex is not acquired.

            Otherwise it stores `h` and `env->executor`, links itself into
            the back of the mutex's wait queue, and registers a stop
            callback on `env->stop_token`. Whichever of `unlock()` and that
            callback claims the awaiter first posts `h` through the stored
            executor; the other skips it.

            @param h The awaiting coroutine, resumed when the mutex is
            acquired or the wait is canceled.

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
            m_->waiters_.push_back(this);
            ::new(stop_cb_buf_) stop_cb_t(
                env->stop_token, cancel_fn{this});
            active_ = true;
            return std::noop_coroutine();
        }

        /** Complete the acquisition and report the outcome.

            Destroys the stop callback if one is registered, and unlinks a
            canceled awaiter from the wait queue.

            @return An empty `io_result<>` if the mutex is now held by the
            awaiting coroutine. Otherwise one holding `error::canceled`,
            which means the stop token won the race and the mutex is not
            held.
        */
        [[nodiscard]] io_result<> await_resume() noexcept
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

    /** Unlocks the mutex automatically when destroyed.
    */
    class [[nodiscard]] lock_guard
    {
        async_mutex* m_;

    public:
        /// Unlock the mutex, if this guard holds one.
        ~lock_guard()
        {
            if(m_)
                m_->unlock();
        }

        /// Construct a guard that holds no mutex.
        lock_guard() noexcept
            : m_(nullptr)
        {
        }

        /** Construct a guard that releases the given mutex on destruction.

            Adopts an already-held lock; it does not acquire one.

            @param m The mutex to unlock on destruction. It must outlive
            the guard.
        */
        explicit lock_guard(async_mutex* m) noexcept
            : m_(m)
        {
        }

        /** Construct by moving, transferring the lock.

            @par Postconditions
            `o` holds no mutex, and its destructor unlocks nothing.

            @param o The guard to move from.
        */
        lock_guard(lock_guard&& o) noexcept
            : m_(std::exchange(o.m_, nullptr))
        {
        }

        /** Assign by moving, transferring the lock.

            If this guard already holds a mutex, that mutex is unlocked
            first. Self-assignment is a no-op.

            @par Postconditions
            `o` holds no mutex, and its destructor unlocks nothing.

            @param o The guard to move from.

            @return A reference to `*this`.
        */
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

        /** Copy construction is disabled; a guard uniquely owns the lock.

            @param other The guard that would be copied.
        */
        lock_guard(lock_guard const& other) = delete;

        /** Copy assignment is disabled; a guard uniquely owns the lock.

            @param other The guard that would be assigned from.

            @return A reference to `*this`.
        */
        lock_guard& operator=(lock_guard const& other) = delete;
    };

    /** Acquires the mutex like `lock_awaiter`, then resumes with a `lock_guard` that unlocks it.
    */
    class lock_guard_awaiter
    {
        async_mutex* m_;
        lock_awaiter inner_;

    public:
        /** Construct an awaiter for the given mutex.

            @param m The mutex to acquire. It must outlive the awaiter.
        */
        explicit lock_guard_awaiter(async_mutex* m) noexcept
            : m_(m)
            , inner_(m)
        {
        }

        /** Acquire the mutex if it is free, reporting whether to suspend.

            Delegates to @ref lock_awaiter::await_ready, so as there this is
            not a pure query: on the fast path it takes the lock.

            @return `true` if the mutex was free and is now held by the
            awaiting coroutine. `false` if the mutex is held elsewhere, in
            which case the coroutine suspends.
        */
        bool await_ready() const noexcept
        {
            return inner_.await_ready();
        }

        /** Enqueue the awaiting coroutine until the mutex is released.

            This is the @ref IoAwaitable overload of `await_suspend`. It
            delegates to @ref lock_awaiter::await_suspend on the wrapped
            awaiter, so it has that function's contract.

            @param h The awaiting coroutine, resumed when the mutex is
            acquired or the wait is canceled.

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
            return inner_.await_suspend(h, env);
        }

        /** Complete the acquisition and report the outcome.

            @return An `io_result<lock_guard>` destructuring as
            `[ec, guard]`. On success `ec` is empty and `guard` holds the
            mutex, releasing it when destroyed. If the wait was canceled,
            `ec` is `error::canceled` and `guard` holds no mutex.
        */
        [[nodiscard]] io_result<lock_guard> await_resume() noexcept
        {
            auto r = inner_.await_resume();
            if(std::get<0>(r))
                return {std::get<0>(r), lock_guard()};
            return {std::error_code(), lock_guard(m_)};
        }
    };

    /// Construct an unlocked mutex.
    async_mutex() = default;

    /** Copy construction is disabled; suspended waiters point into the
        mutex's wait queue.

        @param other The mutex that would be copied.
    */
    async_mutex(async_mutex const& other) = delete;

    /** Copy assignment is disabled; suspended waiters point into the
        mutex's wait queue.

        @param other The mutex that would be assigned from.

        @return A reference to `*this`.
    */
    async_mutex& operator=(async_mutex const& other) = delete;

    /** Move construction is disabled; suspended waiters point into the
        mutex's wait queue.

        @param other The mutex that would be moved from.
    */
    async_mutex(async_mutex&& other) = delete;

    /** Move assignment is disabled; suspended waiters point into the
        mutex's wait queue.

        @param other The mutex that would be moved from.

        @return A reference to `*this`.
    */
    async_mutex& operator=(async_mutex&& other) = delete;

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

        @return `true` if the mutex is held; otherwise `false`.
    */
    bool is_locked() const noexcept
    {
        return locked_;
    }
};

} // namespace capy
} // namespace boost

#endif
