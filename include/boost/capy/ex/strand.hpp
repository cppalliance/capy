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

#include <type_traits>

namespace boost {
namespace capy {

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
    - `operator()(h)` - May run immediately if strand is idle
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
    detail::strand_impl* impl_;
    post_dispatcher<Executor> post_;

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

        @note This constructor is disabled if the argument is a
            strand type, to prevent strand-of-strand wrapping.
    */
    template<typename Executor1,
        typename = std::enable_if_t<
            !std::is_same_v<std::decay_t<Executor1>, strand> &&
            !detail::is_strand<std::decay_t<Executor1>>::value &&
            std::is_convertible_v<Executor1, Executor>>>
    explicit
    strand(Executor1&& ex)
        : impl_(detail::get_strand_service(ex.context())
            .get_implementation())
        , post_(std::forward<Executor1>(ex))
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
        return post_.get_inner_executor();
    }

    /** Return the underlying execution context.

        @return A reference to the execution context associated
            with the inner executor.
    */
    auto&
    context() const noexcept
    {
        return post_.get_inner_executor().context();
    }

    /** Notify that work has started.

        Delegates to the inner executor's `on_work_started()`.
        This is a no-op for most executor types.
    */
    void
    on_work_started() const noexcept
    {
        post_.get_inner_executor().on_work_started();
    }

    /** Notify that work has finished.

        Delegates to the inner executor's `on_work_finished()`.
        This is a no-op for most executor types.
    */
    void
    on_work_finished() const noexcept
    {
        post_.get_inner_executor().on_work_finished();
    }

    /** Determine whether the strand is running in the current thread.

        @return true if the current thread is executing a coroutine
            within this strand's dispatch loop.
    */
    bool
    running_in_this_thread() const noexcept
    {
        return detail::strand_service::running_in_this_thread(*impl_);
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

    /** Post a coroutine to the strand.

        The coroutine is always queued for execution, never resumed
        immediately. When the strand becomes available, queued
        coroutines execute in FIFO order on the underlying executor.

        @par Ordering
        Guarantees strict FIFO ordering relative to other post() calls.
        Use this instead of dispatch() when ordering matters.

        @param h The coroutine handle to post.
    */
    void
    post(any_coro h) const
    {
        detail::strand_service::post(*impl_, any_dispatcher(post_), h);
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

        If the calling thread is already executing within this strand,
        the coroutine is resumed immediately via symmetric transfer,
        bypassing the queue. This provides optimal performance but
        means the coroutine may execute before previously queued work.

        Otherwise, the coroutine is queued and will execute in FIFO
        order relative to other queued coroutines.

        @par Ordering
        Callers requiring strict FIFO ordering should use post()
        instead, which always queues the coroutine.

        @param h The coroutine handle to dispatch.
        @return A coroutine handle for symmetric transfer.
    */
    // TODO: measure before deciding to split strand_impl for inlining fast-path check
    any_coro
    operator()(any_coro h) const
    {
        return detail::strand_service::dispatch(*impl_, any_dispatcher(post_), h);
    }
};

// Deduction guide
template<typename Executor>
strand(Executor) -> strand<Executor>;

} // namespace capy
} // namespace boost

#endif
