//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/boostorg/capy
//

#ifndef BOOST_CAPY_EX_THREAD_POOL_HPP
#define BOOST_CAPY_EX_THREAD_POOL_HPP

#include <boost/capy/detail/config.hpp>
#include <boost/capy/ex/any_coro.hpp>
#include <boost/capy/ex/execution_context.hpp>
#include <cstddef>

namespace boost {
namespace capy {

/** A pool of threads for running work asynchronously.

    This class provides a pool of worker threads that execute
    submitted work items. It inherits from `execution_context`,
    providing service management and a nested `executor_type`
    that satisfies the `capy::executor` concept.

    Work is submitted via the executor obtained from `get_executor()`.
    The executor's `post()`, `dispatch()`, and `defer()` functions
    queue coroutines for execution on pool threads.

    @par Thread Safety
    All member functions may be called concurrently.

    @par Example
    @code
    thread_pool pool(4);  // 4 worker threads
    auto ex = pool.get_executor();

    // Post a coroutine for execution
    async_run(ex)( my_coro() );
    @endcode

    @see execution_context, executor
*/
class BOOST_CAPY_DECL
    thread_pool
    : public execution_context
{
    class impl;
    impl* impl_;

public:
    class executor_type;

    /** Destructor.

        Signals all threads to stop and waits for them to complete.
        Calls `shutdown()` and `destroy()` to clean up services.
    */
    ~thread_pool();

    /** Construct a thread pool.

        @param num_threads The number of worker threads to create.
        If zero, defaults to the hardware concurrency.
    */
    explicit
    thread_pool(std::size_t num_threads = 0);

    thread_pool(thread_pool const&) = delete;
    thread_pool& operator=(thread_pool const&) = delete;

    /** Return an executor for this thread pool.

        The returned executor can be used to post work items
        to this thread pool.

        @return An executor associated with this thread pool.
    */
    executor_type
    get_executor() const noexcept;
};

//------------------------------------------------------------------------------

/** An executor for dispatching work to a thread_pool.

    The executor provides the interface for posting work items
    to the associated thread_pool. It satisfies the `capy::executor`
    concept.

    Executors are lightweight handles that can be copied and compared
    for equality. Two executors compare equal if they refer to the
    same thread_pool.

    @par Thread Safety
    Distinct objects: Safe.@n
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
    /** Default constructor.

        Constructs an executor not associated with any thread_pool.
    */
    executor_type() = default;

    /** Return a reference to the associated execution context.

        @return Reference to the thread_pool.
    */
    thread_pool&
    context() const noexcept
    {
        return *pool_;
    }

    /** Informs the executor that work is beginning.

        Must be paired with `on_work_finished()`.
    */
    void
    on_work_started() const noexcept
    {
    }

    /** Informs the executor that work has completed.

        @par Preconditions
        A preceding call to `on_work_started()` on an equal executor.
    */
    void
    on_work_finished() const noexcept
    {
    }

    /** Dispatch a coroutine handle.

        For thread_pool, dispatch always posts the work since
        the calling thread is never "inside" the pool's run loop.

        @param h The coroutine handle to dispatch.

        @return `std::noop_coroutine()` since work is always posted.
    */
    any_coro
    dispatch(any_coro h) const
    {
        post(h);
        return std::noop_coroutine();
    }

    /** Post a coroutine for deferred execution.

        The coroutine will be resumed on one of the pool's
        worker threads.

        @param h The coroutine handle to post.
    */
    BOOST_CAPY_DECL
    void
    post(any_coro h) const;

    /** Queue a coroutine for deferred execution.

        This is semantically identical to `post`, but conveys that
        `h` is a continuation of the current call context.

        @param h The coroutine handle to defer.
    */
    void
    defer(any_coro h) const
    {
        post(h);
    }

    /** Compare two executors for equality.

        @return `true` if both executors refer to the same thread_pool.
    */
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
