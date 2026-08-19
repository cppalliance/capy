//
// Copyright (c) 2026 Michael Vandeberg
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_CONTINUATION_HPP
#define BOOST_CAPY_CONTINUATION_HPP

#include <boost/capy/detail/config.hpp>

#include <coroutine>

namespace boost {
namespace capy {

/** Pairs a coroutine handle with a scratch slot so executors can queue it without heap allocation.

    Wraps a `std::coroutine_handle<>` with a single
    pointer-sized scratch slot so executors can queue
    continuations without per-post heap allocation.

    @par Fields

    @li `h` — the coroutine handle to resume. Set by the
        code that creates or reuses the continuation (typically
        an I/O awaitable or combinator). Read by the executor
        when it dequeues the continuation.

    @li `reserved` — a pointer-sized scratch slot. Ordinary
        users must not touch it. Authors of awaitable algorithms
        (e.g. `async_mutex`, `async_semaphore`) may commandeer
        it for their own node-based data structure. They may do
        so **only before** the continuation is submitted to an
        executor.
        On submission the executor **clobbers** `reserved` to
        link the continuation into its internal queue; the value
        carries **no meaning** afterward. Once submitted, the
        caller must not read or modify the continuation at all.

    @par Ownership and Lifetime

    The continuation is owned by the site that embeds it (an
    I/O awaitable, combinator state, or trampoline promise).
    The executor borrows it by reference for the duration of
    the queue residency.

    A continuation must have a **stable address** while it is
    linked into an executor's queue. It must not be moved,
    destroyed, or enqueued in more than one queue concurrently.

    An author who needs a doubly-linked (or otherwise richer)
    structure should hold a `continuation` as a member. Deriving
    from it also works, because `continuation` is an aggregate.
    Such an author should manage their own links, because
    `reserved` is only a single pre-submission scratch slot. It
    is no longer available once the continuation is submitted.

    @par Copy and Move

    Trivially copyable and movable (aggregate of a handle and
    a pointer). However, copying or moving a queued
    continuation produces a second object whose `reserved` slot
    is stale — the executor still links the original. Copy and
    move are safe only when the continuation is not enqueued.

    @par Thread Safety

    A single continuation must not be accessed concurrently
    without external synchronization. In practice, the
    creating thread sets `h` and calls `executor.post(c)`;
    the executor's worker thread later reads `h` and calls
    `h.resume()`. The executor's internal locking provides
    the necessary synchronization between these two accesses.

    @see Executor, executor_ref
*/
struct continuation
{
    /** The coroutine handle to resume.

        Set by the code that creates or reuses the continuation, and read
        by the executor when it dequeues it.
    */
    std::coroutine_handle<> h;

    /** Pointer-sized scratch slot, available only before submission.

        Authors of awaitable algorithms may commandeer it for their own
        node links until the continuation is submitted to an executor. On
        submission the executor clobbers it to link the continuation into
        its own queue, after which the value carries no meaning. See the
        class description for the full contract.
    */
    void* reserved = nullptr;
};

} // namespace capy
} // namespace boost

#endif
