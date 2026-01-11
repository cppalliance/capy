//
// Copyright (c) 2025 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_INTRUSIVE_QUEUE_HPP
#define BOOST_CAPY_INTRUSIVE_QUEUE_HPP

#include <boost/capy/detail/config.hpp>

namespace boost {
namespace capy {

/** An intrusive singly linked FIFO queue.

    This container provides O(1) push and pop operations for
    elements that derive from @ref node. Elements are not
    copied or moved; they are linked directly into the queue.

    Unlike @ref intrusive_list, this uses only a single `next_`
    pointer per node, saving memory at the cost of not supporting
    O(1) removal of arbitrary elements.

    @par Usage
    @code
    struct my_item : intrusive_queue<my_item>::node
    {
        // user data
    };

    using item_queue = intrusive_queue<my_item>;

    my_item item;
    item_queue q;
    q.push(&item);
    my_item* p = q.pop();  // p == &item
    @endcode

    @tparam T The element type. Must derive from `intrusive_queue<T>::node`.

    @see intrusive_list
*/
template<class T>
class intrusive_queue
{
public:
    /** Base class for queue elements.

        Derive from this class to make a type usable with
        @ref intrusive_queue. The `next_` pointer is private
        and accessible only to the queue.
    */
    class node
    {
        friend class intrusive_queue;

    private:
        T* next_;
    };

private:
    T* head_ = nullptr;
    T* tail_ = nullptr;

public:
    /** Default constructor.

        Creates an empty queue.

        @post `empty() == true`
    */
    intrusive_queue() = default;

    /** Move constructor.

        Takes ownership of all elements from `other`,
        leaving `other` empty.

        @param other The queue to move from.

        @post `other.empty() == true`
    */
    intrusive_queue(intrusive_queue&& other) noexcept
        : head_(other.head_)
        , tail_(other.tail_)
    {
        other.head_ = nullptr;
        other.tail_ = nullptr;
    }

    intrusive_queue(intrusive_queue const&) = delete;
    intrusive_queue& operator=(intrusive_queue const&) = delete;
    intrusive_queue& operator=(intrusive_queue&&) = delete;

    /** Return true if the queue is empty.

        @return `true` if the queue contains no elements.
    */
    bool
    empty() const noexcept
    {
        return head_ == nullptr;
    }

    /** Add an element to the back of the queue.

        @param w Pointer to the element to add.

        @pre `w` is not null and not already in a queue.
    */
    void
    push(T* w) noexcept
    {
        w->next_ = nullptr;
        if(tail_)
            tail_->next_ = w;
        else
            head_ = w;
        tail_ = w;
    }

    /** Splice all elements from another queue to the back.

        All elements from `other` are moved to the back of this
        queue. After this call, `other` is empty.

        @param other The queue to splice from.

        @post `other.empty() == true`
    */
    void
    splice(intrusive_queue& other) noexcept
    {
        if(other.empty())
            return;
        if(tail_)
            tail_->next_ = other.head_;
        else
            head_ = other.head_;
        tail_ = other.tail_;
        other.head_ = nullptr;
        other.tail_ = nullptr;
    }

    /** Remove and return the front element.

        @return Pointer to the front element, or `nullptr`
            if the queue is empty.
    */
    T*
    pop() noexcept
    {
        if(!head_)
            return nullptr;
        T* w = head_;
        head_ = head_->next_;
        if(!head_)
            tail_ = nullptr;
        return w;
    }
};

} // capy
} // boost

#endif
