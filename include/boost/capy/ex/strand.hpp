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
#include <boost/capy/continuation.hpp>
#include <coroutine>
#include <boost/capy/ex/detail/strand_service.hpp>

#include <type_traits>

namespace boost {
namespace capy {

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
    Each strand allocates a private serialization state. Strands
    constructed from the same execution context share a small pool
    of mutexes (193 entries) selected by hash; mutex sharing causes
    only brief contention on the push/pop critical section, never
    cross-strand state sharing. Construction cost: one
    `std::make_shared` per strand.

    @par Executor Concept
    This class satisfies the `Executor` concept, providing:
    - `context()` - Returns the underlying execution context
    - `on_work_started()` / `on_work_finished()` - Work tracking
    - `dispatch(continuation&)` - May run immediately if already executing in this strand
    - `post(continuation&)` - Always queues for later execution

    @par Preconditions
    A strand holds only a non-owning reference to its inner executor's
    execution context (for example a `thread_pool`). That context must
    outlive every post() and dispatch() call; posting or dispatching
    concurrently with, or after, the context's destruction is undefined
    behavior. To guarantee this, submit work through @ref run_async or
    @ref run — whose operations are work-tracked, so the context's
    `join()` waits for them — and call `join()` on the context before
    destroying it, rather than posting to a strand from an external
    thread the context does not track. Destroying the strand handle
    itself is always safe, including after the context has been
    destroyed.

    @par Thread Safety
    Distinct objects: Safe.
    Shared objects: Safe.

    @par Example
    @code
    thread_pool pool(4);
    strand strand(pool.get_executor());  // CTAD deduces the executor type

    // Continuations are linked intrusively into the strand's queue,
    // so each one must outlive its time there. Storage is typically
    // owned by the awaitable or operation state that posted it.
    continuation c1{h1}, c2{h2}, c3{h3};
    strand.post(c1);
    strand.post(c2);
    strand.post(c3);
    @endcode

    @tparam Ex The type of the underlying executor. Must
        satisfy the `Executor` concept.

    @see Executor
*/
template<typename Ex>
class strand
{
    std::shared_ptr<detail::strand_impl> impl_;
    Ex ex_;

    friend struct strand_test;

public:
    /** The type of the underlying executor.
    */
    using inner_executor_type = Ex;

    /** Construct a strand for the specified executor.

        Allocates a fresh strand implementation from the service
        associated with the executor's context.

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
            .create_implementation())
        , ex_(std::forward<Ex1>(ex))
    {
    }

    /** Construct a copy.

        Creates a strand that shares serialization state with
        the original. Coroutines dispatched through either strand
        will be serialized with respect to each other.
    */
    strand(strand const&) = default;

    /** Construct by moving.

        @note A moved-from strand is only safe to destroy
            or reassign.
    */
    strand(strand&&) = default;

    /** Assign by copying.
    */
    strand& operator=(strand const&) = default;

    /** Assign by moving.

        @note A moved-from strand is only safe to destroy
            or reassign.
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
        return impl_.get() == other.impl_.get();
    }

    /** Post a continuation to the strand.

        The continuation is always queued for execution, never resumed
        immediately. When the strand becomes available, queued
        work executes in FIFO order on the underlying executor.

        @par Ordering
        Guarantees strict FIFO ordering relative to other post() calls.
        Use this instead of dispatch() when ordering matters.

        @param c The continuation to post. The caller retains
            ownership; the continuation must remain valid until
            it is dequeued and resumed.

        @par Preconditions
        The strand's execution context must outlive this call. Posting
        concurrently with, or after, that context's destruction is
        undefined behavior.
    */
    void
    post(continuation& c) const
    {
        detail::strand_service::post(impl_, executor_ref(ex_), c);
    }

    /** Dispatch a continuation through the strand.

        Returns a handle for symmetric transfer. If the calling
        thread is already executing within this strand, returns `c.h`.
        Otherwise, the continuation is queued and
        `std::noop_coroutine()` is returned.

        @par Ordering
        Callers requiring strict FIFO ordering should use post()
        instead, which always queues the continuation.

        @param c The continuation to dispatch. The caller retains
            ownership; the continuation must remain valid until
            it is dequeued and resumed.

        @return A handle for symmetric transfer or `std::noop_coroutine()`.

        @par Preconditions
        The strand's execution context must outlive this call.
        Dispatching concurrently with, or after, that context's
        destruction is undefined behavior.
    */
    std::coroutine_handle<>
    dispatch(continuation& c) const
    {
        return detail::strand_service::dispatch(impl_, executor_ref(ex_), c);
    }
};

// Deduction guide
template<typename Ex>
strand(Ex) -> strand<Ex>;

} // namespace capy
} // namespace boost

#endif
