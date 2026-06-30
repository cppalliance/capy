//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
// Copyright (c) 2026 Michael Vandeberg
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_SRC_EX_DETAIL_STRAND_QUEUE_HPP
#define BOOST_CAPY_SRC_EX_DETAIL_STRAND_QUEUE_HPP

#include <boost/capy/continuation.hpp>
#include <boost/capy/detail/config.hpp>
#include <boost/capy/ex/frame_allocator.hpp>

namespace boost {
namespace capy {
namespace detail {

/** Single-threaded intrusive FIFO of pending continuations.

    Links continuations directly through `continuation::reserved` (a
    typed `continuation*` round-tripped through the `void*` slot), so
    push() carries no per-item allocation.

    @par Thread Safety
    Not thread-safe. Caller must externally synchronize push() and
    take_all(). dispatch_batch() does not touch queue state and may
    run unlocked once the batch has been taken.
*/
class strand_queue
{
    continuation* head_ = nullptr;
    continuation* tail_ = nullptr;

public:
    strand_queue() = default;
    strand_queue(strand_queue const&) = delete;
    strand_queue& operator=(strand_queue const&) = delete;

    /** Returns true if there are no pending continuations. */
    bool
    empty() const noexcept
    {
        return head_ == nullptr;
    }

    /** Push a continuation to the queue.

        @param c The continuation to enqueue; see `continuation`
            for lifetime and aliasing requirements.
    */
    void
    push(continuation& c) noexcept
    {
        c.reserved = nullptr;
        if(tail_)
            tail_->reserved = &c;
        else
            head_ = &c;
        tail_ = &c;
    }

    /** Batch of taken items for thread-safe dispatch. */
    struct taken_batch
    {
        continuation* head = nullptr;
        continuation* tail = nullptr;
    };

    /** Take all pending items atomically.

        Removes all items from the queue and returns them as a
        batch. The queue is left empty.

        @return The batch of taken items.
    */
    taken_batch
    take_all() noexcept
    {
        taken_batch batch{head_, tail_};
        head_ = tail_ = nullptr;
        return batch;
    }

    /** Resume each continuation in a taken batch.

        Advances past each node before resuming, since the
        resumed coroutine may destroy the awaitable (and thus
        the continuation) before control returns here.

        @param batch The batch to dispatch.

        @note Thread-safe with respect to push() because the queue
            itself is not touched.
    */
    static
    void
    dispatch_batch(taken_batch& batch)
    {
        while(batch.head)
        {
            continuation* c = batch.head;
            batch.head = static_cast<continuation*>(c->reserved);
            safe_resume(c->h);
        }
        batch.tail = nullptr;
    }
};

} // namespace detail
} // namespace capy
} // namespace boost

#endif
