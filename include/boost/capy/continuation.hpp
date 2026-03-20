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

/** Executor-facing schedulable unit.

    Wraps a `std::coroutine_handle<>` with an intrusive list
    pointer so executors can queue continuations without
    per-post heap allocation.

    @par Fields

    @li `h` — the coroutine handle to resume. Set by the
        code that creates or reuses the continuation (typically
        an I/O awaitable or combinator). Read by the executor
        when it dequeues the continuation.

    @li `next` — intrusive linked-list pointer, owned and
        managed exclusively by executor implementations. Users
        must not read or write `next` while the continuation
        is enqueued.

    @par Ownership and Lifetime

    The continuation is owned by the site that embeds it (an
    I/O awaitable, combinator state, or trampoline promise).
    The executor borrows it by reference for the duration of
    the queue residency.

    A continuation must have a **stable address** while it is
    linked into an executor's queue. It must not be moved,
    destroyed, or enqueued in more than one queue concurrently.

    @par Copy and Move

    Trivially copyable and movable (aggregate of a handle and
    a pointer). However, copying or moving a queued
    continuation produces a second object whose `next` is
    stale — the executor still points to the original. Copy
    and move are safe only when the continuation is not
    enqueued.

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
    std::coroutine_handle<> h;
    continuation* next = nullptr;
};

} // namespace capy
} // namespace boost

#endif
