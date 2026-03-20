//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
// Copyright (c) 2026 Michael Vandeberg
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_EX_THREAD_POOL_HPP
#define BOOST_CAPY_EX_THREAD_POOL_HPP

#include <boost/capy/detail/config.hpp>
#include <boost/capy/continuation.hpp>
#include <coroutine>
#include <boost/capy/ex/execution_context.hpp>
#include <cstddef>
#include <string_view>

namespace boost {
namespace capy {

/** A pool of threads for executing work concurrently.

    Use this when you need to run coroutines on multiple threads
    without the overhead of creating and destroying threads for
    each task. Work items are distributed across the pool using
    a shared queue.

    @par Thread Safety
    Distinct objects: Safe.
    Shared objects: Unsafe.

    @par Example
    @code
    thread_pool pool(4);  // 4 worker threads
    auto ex = pool.get_executor();
    ex.post(some_coroutine);
    // pool destructor waits for all work to complete
    @endcode
*/
class BOOST_CAPY_DECL
    thread_pool
    : public execution_context
{
    class impl;
    impl* impl_;

public:
    class executor_type;

    /** Destroy the thread pool.

        Signals all worker threads to stop, waits for them to
        finish, and destroys any pending work items.
    */
    ~thread_pool();

    /** Construct a thread pool.

        Creates a pool with the specified number of worker threads.
        If `num_threads` is zero, the number of threads is set to
        the hardware concurrency, or one if that cannot be determined.

        @param num_threads The number of worker threads, or zero
            for automatic selection.

        @param thread_name_prefix The prefix for worker thread names.
            Thread names appear as "{prefix}0", "{prefix}1", etc.
            The prefix is truncated to 12 characters. Defaults to
            "capy-pool-".
    */
    explicit
    thread_pool(
        std::size_t num_threads = 0,
        std::string_view thread_name_prefix = "capy-pool-");

    thread_pool(thread_pool const&) = delete;
    thread_pool& operator=(thread_pool const&) = delete;

    /** Wait for all outstanding work to complete.

        Releases the internal work guard, then blocks the calling
        thread until all outstanding work tracked by
        @ref executor_type::on_work_started and
        @ref executor_type::on_work_finished completes. After all
        work finishes, joins the worker threads.

        If @ref stop is called while `join()` is blocking, the
        pool stops without waiting for remaining work to
        complete. Worker threads finish their current item and
        exit; `join()` still waits for all threads to be joined
        before returning.

        This function is idempotent. The first call performs the
        join; subsequent calls return immediately.

        @par Preconditions
        Must not be called from a thread in this pool (undefined
        behavior).

        @par Postconditions
        All worker threads have been joined. The pool cannot be
        reused.

        @par Thread Safety
        May be called from any thread not in this pool.
    */
    void
    join() noexcept;

    /** Request all worker threads to stop.

        Signals all threads to exit after finishing their current
        work item. Queued work that has not started is abandoned.
        Does not wait for threads to exit.

        If @ref join is blocking on another thread, calling
        `stop()` causes it to stop waiting for outstanding
        work. The `join()` call still waits for worker threads
        to finish their current item and exit before returning.
    */
    void
    stop() noexcept;

    /** Return an executor for this thread pool.

        @return An executor associated with this thread pool.
    */
    executor_type
    get_executor() const noexcept;
};

/** An executor that submits work to a thread_pool.

    Executors are lightweight handles that can be copied and stored.
    All copies refer to the same underlying thread pool.

    @par Thread Safety
    Distinct objects: Safe.
    Shared objects: Safe.
*/
class thread_pool::executor_type
{
    friend class thread_pool;

    thread_pool* pool_ = nullptr;

    explicit
    executor_type(thread_pool& pool) noexcept
        : pool_(&pool)
    {
    }

public:
    /** Construct a default null executor.

        The resulting executor is not associated with any pool.
        `context()`, `dispatch()`, and `post()` require the
        executor to be associated with a pool before use.
    */
    executor_type() = default;

    /// Return the underlying thread pool.
    thread_pool&
    context() const noexcept
    {
        return *pool_;
    }

    /** Notify that work has started.

        Increments the outstanding work count. Must be paired
        with a subsequent call to @ref on_work_finished.

        @see on_work_finished, work_guard
    */
    BOOST_CAPY_DECL
    void
    on_work_started() const noexcept;

    /** Notify that work has finished.

        Decrements the outstanding work count. When the count
        reaches zero after @ref thread_pool::join has been called,
        the pool's worker threads are signaled to stop.

        @pre A preceding call to @ref on_work_started was made.

        @see on_work_started, work_guard
    */
    BOOST_CAPY_DECL
    void
    on_work_finished() const noexcept;

    /** Dispatch a continuation for execution.

        Posts the continuation to the thread pool for execution on a
        worker thread and returns `std::noop_coroutine()`. Thread
        pools never execute inline because no single thread "owns"
        the pool.

        @param c The continuation to execute. Must remain at a
                 stable address until dequeued and resumed.

        @return `std::noop_coroutine()` always.
    */
    std::coroutine_handle<>
    dispatch(continuation& c) const
    {
        post(c);
        return std::noop_coroutine();
    }

    /** Post a continuation to the thread pool.

        The continuation will be resumed on one of the pool's
        worker threads. The continuation must remain at a stable
        address until it is dequeued and resumed.

        @param c The continuation to execute.
    */
    BOOST_CAPY_DECL
    void
    post(continuation& c) const;

    /// Return true if two executors refer to the same thread pool.
    bool
    operator==(executor_type const& other) const noexcept
    {
        return pool_ == other.pool_;
    }
};

} // capy
} // boost

#endif
