//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_EX_STRAND_HPP
#define BOOST_CAPY_EX_STRAND_HPP

#include <boost/capy/detail/config.hpp>
#include <coroutine>
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
    This class satisfies the `Executor` concept, providing:
    - `context()` - Returns the underlying execution context
    - `on_work_started()` / `on_work_finished()` - Work tracking
    - `dispatch(h)` - May run immediately if strand is idle
    - `post(h)` - Always queues for later execution

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

    @tparam E The type of the underlying executor. Must
        satisfy the `Executor` concept.

    @see make_strand, Executor
*/
template<typename Ex>
class strand
{
    detail::strand_impl* impl_;
    Ex ex_;

public:
    /** The type of the underlying executor.
    */
    using inner_executor_type = Ex;

    /** Construct a strand for the specified executor.

        Obtains a strand implementation from the service associated
        with the executor's context. The implementation is selected
        from a fixed pool using a hash function.

        @param ex The inner executor to wrap. Coroutines will
            ultimately be dispatched through this executor.

        @note This constructor is disabled if the argument is a
            strand type, to prevent strand-of-strand wrapping.
    */
    template<typename Ex1,
        typename = std::enable_if_t<
            !std::is_same_v<std::decay_t<Ex1>, strand> &&
            !detail::is_strand<std::decay_t<Ex1>>::value &&
            std::is_convertible_v<Ex1, Ex>>>
    explicit
    strand(Ex1&& ex)
        : impl_(detail::get_strand_service(ex.context())
            .get_implementation())
        , ex_(std::forward<Ex1>(ex))
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
    Ex const&
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
    post(std::coroutine_handle<> h) const
    {
        detail::strand_service::post(*impl_, executor_ref(ex_), h);
    }

    /** Dispatch a coroutine through the strand.

        Returns a handle for symmetric transfer. If the calling
        thread is already executing within this strand, returns `h`.
        Otherwise, the coroutine is queued and
        `std::noop_coroutine()` is returned.

        @par Ordering
        Callers requiring strict FIFO ordering should use post()
        instead, which always queues the coroutine.

        @param h The coroutine handle to dispatch.

        @return A handle for symmetric transfer or `std::noop_coroutine()`.
    */
    std::coroutine_handle<>
    dispatch(std::coroutine_handle<> h) const
    {
        return detail::strand_service::dispatch(*impl_, executor_ref(ex_), h);
    }
};

// Deduction guide
template<typename Ex>
strand(Ex) -> strand<Ex>;

} // namespace capy
} // namespace boost

#endif
