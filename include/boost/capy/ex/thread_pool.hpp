//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
// Copyright (c) 2026 Michael Vandeberg
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/boostorg/capy
//

#ifndef BOOST_CAPY_EX_THREAD_POOL_HPP
#define BOOST_CAPY_EX_THREAD_POOL_HPP

#include <boost/capy/detail/config.hpp>
#include <boost/capy/coro.hpp>
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

    /** Request all worker threads to stop.

        Signals all threads to exit. Threads will finish their
        current work item before exiting. Does not wait for
        threads to exit.
    */
    void
    stop() noexcept;

    /** Return an executor for this thread pool.

        @return An executor associated with this thread pool.
    */
    executor_type
    get_executor() const noexcept;
};

//------------------------------------------------------------------------------

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
    /// Default construct a null executor.
    executor_type() = default;

    /// Return the underlying thread pool.
    thread_pool&
    context() const noexcept
    {
        return *pool_;
    }

    /// Notify that work has started (no-op for thread pools).
    void
    on_work_started() const noexcept
    {
    }

    /// Notify that work has finished (no-op for thread pools).
    void
    on_work_finished() const noexcept
    {
    }

    /** Dispatch a coroutine for execution.

        Posts the coroutine to the thread pool and returns
        immediately. The caller should suspend after calling
        this function.

        @param h The coroutine handle to execute.

        @return A noop coroutine handle to resume.
    */
    coro
    dispatch(coro h) const
    {
        post(h);
        return std::noop_coroutine();
    }

    /** Post a coroutine to the thread pool.

        The coroutine will be resumed on one of the pool's
        worker threads.

        @param h The coroutine handle to execute.
    */
    BOOST_CAPY_DECL
    void
    post(coro h) const;

    /// Return true if two executors refer to the same thread pool.
    bool
    operator==(executor_type const& other) const noexcept
    {
        return pool_ == other.pool_;
    }
};

//------------------------------------------------------------------------------

inline
auto
thread_pool::
get_executor() const noexcept ->
    executor_type
{
    return executor_type(const_cast<thread_pool&>(*this));
}

} // capy
} // boost

#endif
