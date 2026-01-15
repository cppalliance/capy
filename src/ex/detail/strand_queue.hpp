//
// Copyright (c) 2025 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_SRC_EX_DETAIL_STRAND_QUEUE_HPP
#define BOOST_CAPY_SRC_EX_DETAIL_STRAND_QUEUE_HPP

#include <boost/capy/detail/config.hpp>

#include <coroutine>
#include <cstddef>
#include <exception>

namespace boost {
namespace capy {
namespace detail {

class strand_queue;

//----------------------------------------------------------

// Metadata stored before the coroutine frame
struct frame_prefix
{
    frame_prefix* next;
    strand_queue* queue;
    std::size_t alloc_size;
};

//----------------------------------------------------------

/** Wrapper coroutine for strand queue dispatch operations.

    This coroutine wraps a target coroutine handle and resumes
    it when dispatched. The wrapper ensures control returns to
    the dispatch loop after the target suspends or completes.

    The promise contains an intrusive list node for queue
    storage and supports a custom allocator that recycles
    coroutine frames via a free list.
*/
struct strand_op
{
    struct promise_type
    {
        promise_type* next = nullptr;

        void*
        operator new(
            std::size_t size,
            strand_queue& q,
            std::coroutine_handle<void>);

        void
        operator delete(void* p, std::size_t);

        strand_op
        get_return_object() noexcept
        {
            return {std::coroutine_handle<promise_type>::from_promise(*this)};
        }

        std::suspend_always
        initial_suspend() noexcept
        {
            return {};
        }

        std::suspend_always
        final_suspend() noexcept
        {
            return {};
        }

        void
        return_void() noexcept
        {
        }

        void
        unhandled_exception()
        {
            std::terminate();
        }
    };

    std::coroutine_handle<promise_type> h_;
};

//----------------------------------------------------------

/** Single-threaded dispatch queue for coroutine handles.

    This queue stores coroutine handles and resumes them
    sequentially when dispatch() is called. Each pushed
    handle is wrapped in a strand_op coroutine that ensures
    control returns to the dispatch loop after the target
    suspends or completes.

    The queue uses an intrusive singly-linked list through
    the promise type to avoid separate node allocations.
    A free list recycles wrapper coroutine frames to reduce
    allocation overhead during repeated push/dispatch cycles.

    @par Thread Safety
    This class is not thread-safe. All operations must be
    called from a single thread.
*/
class strand_queue
{
    using promise_type = strand_op::promise_type;

    promise_type* head_ = nullptr;
    promise_type* tail_ = nullptr;
    frame_prefix* free_list_ = nullptr;

    friend struct strand_op::promise_type;

    static
    strand_op
    make_strand_op(
        strand_queue& q,
        std::coroutine_handle<void> target)
    {
        (void)q;
        target.resume();
        co_return;
    }

public:
    strand_queue() = default;

    strand_queue(strand_queue const&) = delete;
    strand_queue& operator=(strand_queue const&) = delete;

    /** Destructor.

        Destroys any pending wrappers without resuming them,
        then frees all memory in the free list.
    */
    ~strand_queue()
    {
        // Destroy pending wrappers
        while(head_)
        {
            promise_type* p = head_;
            head_ = p->next;

            auto h = std::coroutine_handle<promise_type>::from_promise(*p);
            h.destroy();
        }

        // Free the free list memory
        while(free_list_)
        {
            frame_prefix* prefix = free_list_;
            free_list_ = prefix->next;
            ::operator delete(prefix);
        }
    }

    /** Returns true if there are no pending operations.
    */
    bool
    empty() const noexcept
    {
        return head_ == nullptr;
    }

    /** Push a coroutine handle to the queue.

        Creates a wrapper coroutine and appends it to the
        queue. The wrapper will resume the target handle
        when dispatch() processes it.

        @param h The coroutine handle to dispatch.
    */
    void
    push(std::coroutine_handle<void> h)
    {
        strand_op op = make_strand_op(*this, h);

        promise_type* p = &op.h_.promise();
        p->next = nullptr;

        if(tail_)
            tail_->next = p;
        else
            head_ = p;
        tail_ = p;
    }

    /** Resume all queued coroutines in sequence.

        Processes each wrapper in FIFO order, resuming its
        target coroutine. After each target suspends or
        completes, the wrapper is destroyed and its frame
        is added to the free list for reuse.

        Coroutines resumed during dispatch may push new
        handles, which will also be processed in the same
        dispatch call.
    */
    void
    dispatch()
    {
        while(head_)
        {
            promise_type* p = head_;
            head_ = p->next;
            if(!head_)
                tail_ = nullptr;

            auto h = std::coroutine_handle<promise_type>::from_promise(*p);
            h.resume();
            h.destroy();
        }
    }
};

//----------------------------------------------------------

inline
void*
strand_op::promise_type::operator new(
    std::size_t size,
    strand_queue& q,
    std::coroutine_handle<void>)
{
    // Total size includes prefix
    std::size_t alloc_size = size + sizeof(frame_prefix);
    void* raw;
    
    // Try to reuse from free list
    if(q.free_list_)
    {
        frame_prefix* prefix = q.free_list_;
        q.free_list_ = prefix->next;
        raw = prefix;
    }
    else
    {
        raw = ::operator new(alloc_size);
    }

    // Initialize prefix
    frame_prefix* prefix = static_cast<frame_prefix*>(raw);
    prefix->next = nullptr;
    prefix->queue = &q;
    prefix->alloc_size = alloc_size;

    // Return pointer AFTER the prefix (this is where coroutine frame goes)
    return prefix + 1;
}

inline
void
strand_op::promise_type::operator delete(void* p, std::size_t)
{
    // Calculate back to get the prefix
    frame_prefix* prefix = static_cast<frame_prefix*>(p) - 1;

    // Add to free list
    prefix->next = prefix->queue->free_list_;
    prefix->queue->free_list_ = prefix;
}

} // namespace detail
} // namespace capy
} // namespace boost

#endif
