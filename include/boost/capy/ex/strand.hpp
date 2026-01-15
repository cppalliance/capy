//
// Copyright (c) 2025 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_EX_STRAND_HPP
#define BOOST_CAPY_EX_STRAND_HPP

#include <boost/capy/detail/config.hpp>
#include <boost/capy/ex/any_coro.hpp>
#include <boost/capy/ex/detail/strand_service.hpp>

namespace boost {
namespace capy {

namespace detail {

/** Push a coroutine to the strand queue.

    @param impl The strand implementation.
    @param h The coroutine handle to enqueue.
    @param should_run Set to true if the caller should run the batch.
*/
BOOST_CAPY_DECL
void
strand_enqueue(
    strand_impl& impl,
    any_coro h,
    bool& should_run);

/** Dispatch all pending coroutines.

    Resumes all coroutines in the pending queue. After this
    function returns, the queue will be empty.

    @param impl The strand implementation.
*/
BOOST_CAPY_DECL
void
strand_dispatch_pending(strand_impl& impl);

/** Release the strand lock.

    Sets locked_ to false, allowing another caller to acquire
    the strand. Must be called after dispatching is complete.

    @param impl The strand implementation.
*/
BOOST_CAPY_DECL
void
strand_unlock(strand_impl& impl);

/** Check if the strand is currently executing.

    @param impl The strand implementation.
    @return true if a coroutine is running in the strand.
*/
BOOST_CAPY_DECL
bool
strand_running_in_this_thread(strand_impl& impl) noexcept;

} // namespace detail

//----------------------------------------------------------

/** Provides serialized coroutine execution for any executor type.

    A strand wraps an inner executor and ensures that coroutines
    dispatched through it never run concurrently. At most one
    coroutine executes at a time within a strand, even when the
    underlying executor runs on multiple threads.

    Strands are lightweight handles that can be copied freely.
    Copies share the same internal serialization state, so
    coroutines dispatched through any copy are serialized with
    respect to all other copies.

    @par Invariant
    Coroutines resumed through a strand shall not run concurrently.

    @par Implementation
    The strand uses a service-based architecture with a fixed pool
    of 211 implementation objects. New strands hash to select an
    impl from the pool. Strands that hash to the same index share
    serialization, which is harmless (just extra serialization)
    and rare with 211 buckets.

    @par Executor Concept
    This class satisfies the `executor` concept, providing:
    - `context()` - Returns the underlying execution context
    - `on_work_started()` / `on_work_finished()` - Work tracking
    - `dispatch(h)` - May run immediately if strand is idle
    - `post(h)` - Always queues for later execution
    - `defer(h)` - Same as post (continuation hint)

    @par Thread Safety
    Distinct objects: Safe.
    Shared objects: Safe.

    @par Example
    @code
    thread_pool pool(4);
    auto strand = make_strand(pool.get_executor());

    // These coroutines will never run concurrently
    strand.post(coro1);
    strand.post(coro2);
    strand.post(coro3);
    @endcode

    @tparam Executor The type of the underlying executor. Must
        satisfy the `executor` concept.

    @see make_strand, executor
*/
template<typename Executor>
class strand
{
    Executor ex_;
    detail::strand_impl* impl_;

public:
    /** The type of the underlying executor.
    */
    using inner_executor_type = Executor;

    /** Construct a strand for the specified executor.

        Obtains a strand implementation from the service associated
        with the executor's context. The implementation is selected
        from a fixed pool using a hash function.

        @param ex The inner executor to wrap. Coroutines will
            ultimately be dispatched through this executor.
    */
    explicit
    strand(Executor ex)
        : ex_(std::move(ex))
        , impl_(ex_.context()
            .template use_service<detail::strand_service>()
            .get_implementation())
    {
    }

    /** Copy constructor.

        Creates a strand that shares serialization state with
        the original. Coroutines dispatched through either strand
        will be serialized with respect to each other.
    */
    strand(strand const&) = default;

    /** Move constructor.
    */
    strand(strand&&) = default;

    /** Copy assignment operator.
    */
    strand& operator=(strand const&) = default;

    /** Move assignment operator.
    */
    strand& operator=(strand&&) = default;

    /** Return the underlying executor.

        @return A const reference to the inner executor.
    */
    Executor const&
    get_inner_executor() const noexcept
    {
        return ex_;
    }

    /** Return the underlying execution context.

        @return A reference to the execution context associated
            with the inner executor.
    */
    auto&
    context() const noexcept
    {
        return ex_.context();
    }

    /** Notify that work has started.

        Delegates to the inner executor's `on_work_started()`.
        This is a no-op for most executor types.
    */
    void
    on_work_started() const noexcept
    {
        ex_.on_work_started();
    }

    /** Notify that work has finished.

        Delegates to the inner executor's `on_work_finished()`.
        This is a no-op for most executor types.
    */
    void
    on_work_finished() const noexcept
    {
        ex_.on_work_finished();
    }

    /** Determine whether the strand is running in the current thread.

        @return true if a coroutine is currently executing within
            this strand's serialization context.

        @note This is an approximation based on the strand's lock
            state rather than true thread-local tracking.
    */
    bool
    running_in_this_thread() const noexcept
    {
        return detail::strand_running_in_this_thread(*impl_);
    }

    /** Compare two strands for equality.

        Two strands are equal if they share the same internal
        serialization state. Equal strands serialize coroutines
        with respect to each other.

        @param other The strand to compare against.
        @return true if both strands share the same implementation.
    */
    bool
    operator==(strand const& other) const noexcept
    {
        return impl_ == other.impl_;
    }

    /** Dispatch a coroutine through the strand.

        If no coroutine is currently running in the strand, the
        coroutine is executed immediately along with any other
        pending coroutines. Otherwise, it is queued for later
        execution when the current holder releases the strand.

        @param h The coroutine handle to dispatch.
        @return A coroutine handle for symmetric transfer. Returns
            `noop_coroutine()` if the work was queued.
    */
    any_coro
    dispatch(any_coro h) const
    {
        bool should_run = false;
        detail::strand_enqueue(*impl_, h, should_run);
        if(should_run)
        {
            detail::strand_dispatch_pending(*impl_);
            detail::strand_unlock(*impl_);
        }
        return std::noop_coroutine();
    }

    /** Post a coroutine to the strand.

        The coroutine is queued for execution. If this is the first
        work item queued (strand was idle), all pending coroutines
        are dispatched immediately on the current thread.

        @param h The coroutine handle to post.
    */
    void
    post(any_coro h) const
    {
        bool should_run = false;
        detail::strand_enqueue(*impl_, h, should_run);
        if(should_run)
        {
            detail::strand_dispatch_pending(*impl_);
            detail::strand_unlock(*impl_);
        }
    }

    /** Defer a coroutine to the strand.

        Equivalent to `post()`. The defer hint indicates that the
        coroutine is a continuation of the current execution context,
        but strands treat this the same as post.

        @param h The coroutine handle to defer.
    */
    void
    defer(any_coro h) const
    {
        post(h);
    }

    /** Dispatch a coroutine through the strand.

        This operator provides a dispatcher-style interface for
        use with symmetric transfer. Equivalent to `dispatch()`.

        @param h The coroutine handle to dispatch.
        @return A coroutine handle for symmetric transfer.
    */
    any_coro
    operator()(any_coro h) const
    {
        return dispatch(h);
    }
};

} // namespace capy
} // namespace boost

#endif
