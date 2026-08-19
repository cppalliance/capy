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

/** Distributes posted work across a fixed group of worker threads via a shared queue.

    Use this when you need to run coroutines on multiple threads
    without the overhead of creating and destroying threads for
    each task. Work items are distributed across the pool using
    a shared queue.

    @par Thread Safety
    Distinct objects: Safe.
    Shared objects: Safe for @ref get_executor, @ref join, and
    @ref stop. Unsafe for construction and destruction.

    @par Example
    @code
    thread_pool pool(4);  // 4 worker threads
    auto ex = pool.get_executor();
    run_async(ex)(some_task());  // start work; tracked so join() waits for it
    pool.join();  // wait for outstanding work to complete
    // pool destructor stops the pool, discarding any pending work
    @endcode

    @note `join()` waits only for work that holds outstanding-work
    counting, which `run_async` (and `make_work_guard`) provide. A bare
    `executor_type::post()` does not register outstanding work, so
    `join()` does not wait for it.
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

        @pre No thread outside this pool may post or dispatch work to it
        (or to a strand built on it) concurrently with, or after,
        destruction. Doing so is undefined behavior. Submit such work
        through @ref run_async or @ref run and call @ref join before
        the pool is destroyed, so it has completed first.
    */
    ~thread_pool();

    /** Construct a thread pool.

        Records the requested worker count; no threads are created
        yet. Threads start lazily on the executor's first `post()`.
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

    /** Copy construction is disabled; a pool owns its worker threads.

        @param other The pool that would be copied.
    */
    thread_pool(thread_pool const& other) = delete;

    /** Copy assignment is disabled; a pool owns its worker threads.

        @param other The pool that would be assigned from.

        @return A reference to `*this`.
    */
    thread_pool& operator=(thread_pool const& other) = delete;

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

        @pre Must not be called from a thread in this pool (undefined
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

        @par Thread Safety
        May be called concurrently from any thread, including a
        thread in this pool.
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

    /** Return the underlying thread pool.

        @return A reference to the associated pool. The behavior is
            undefined if the executor is not associated with a pool.
    */
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
        reaches zero after @ref thread_pool::join is called,
        the pool's worker threads are signaled to stop.

        @pre A preceding call to @ref on_work_started was made.

        @see on_work_started, work_guard
    */
    BOOST_CAPY_DECL
    void
    on_work_finished() const noexcept;

    /** Dispatch a continuation for execution.

        If the calling thread is a worker of this pool, returns
        `c.h` for symmetric transfer so the caller can resume the
        continuation inline. Otherwise, posts the continuation to
        the pool for execution on a worker thread and returns
        `std::noop_coroutine()`.

        @param c The continuation to execute. On the post path,
                 must remain at a stable address until dequeued
                 and resumed.

        @return `c.h` when the calling thread is a pool worker;
                `std::noop_coroutine()` otherwise.
    */
    BOOST_CAPY_DECL
    std::coroutine_handle<>
    dispatch(continuation& c) const;

    /** Post a continuation to the thread pool.

        The continuation is resumed on one of the pool's
        worker threads. The continuation must remain at a stable
        address until it is dequeued and resumed.

        @param c The continuation to execute.
    */
    BOOST_CAPY_DECL
    void
    post(continuation& c) const;

    /** Return true if two executors refer to the same thread pool.

        @param other The executor to compare against.

        @return `true` if both executors refer to the same pool.
    */
    bool
    operator==(executor_type const& other) const noexcept
    {
        return pool_ == other.pool_;
    }
};

} // capy
} // boost

#endif
