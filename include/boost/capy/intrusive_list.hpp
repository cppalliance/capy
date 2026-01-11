//
// Copyright (c) 2025 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_INTRUSIVE_LIST_HPP
#define BOOST_CAPY_INTRUSIVE_LIST_HPP

#include <boost/capy/detail/config.hpp>

namespace boost {
namespace capy {

/** An intrusive doubly linked list.

    This container provides O(1) push and pop operations for
    elements that derive from @ref node. Elements are not
    copied or moved; they are linked directly into the list.

    @par Usage
    @code
    struct my_item : intrusive_list<my_item>::node
    {
        // user data
    };

    using item_list = intrusive_list<my_item>;

    my_item item;
    item_list q;
    q.push_back(&item);
    my_item* p = q.pop_front();  // p == &item
    @endcode

    @tparam T The element type. Must derive from `intrusive_list<T>::node`.
*/
template<class T>
class intrusive_list
{
public:
    /** Base class for list elements.

        Derive from this class to make a type usable with
        @ref intrusive_list. The `next_` and `prev_` pointers
        are private and accessible only to the list.
    */
    class node
    {
        friend class intrusive_list;

    private:
        T* next_;
        T* prev_;
    };

private:
    T* head_ = nullptr;
    T* tail_ = nullptr;

public:
    /** Default constructor.

        Creates an empty list.

        @post `empty() == true`
    */
    intrusive_list() = default;

    /** Move constructor.

        Takes ownership of all elements from `other`,
        leaving `other` empty.

        @param other The list to move from.

        @post `other.empty() == true`
    */
    intrusive_list(intrusive_list&& other) noexcept
        : head_(other.head_)
        , tail_(other.tail_)
    {
        other.head_ = nullptr;
        other.tail_ = nullptr;
    }

    intrusive_list(intrusive_list const&) = delete;
    intrusive_list& operator=(intrusive_list const&) = delete;
    intrusive_list& operator=(intrusive_list&&) = delete;

    /** Return true if the list is empty.

        @return `true` if the list contains no elements.
    */
    bool
    empty() const noexcept
    {
        return head_ == nullptr;
    }

    /** Add an element to the back of the list.

        @param w Pointer to the element to add.

        @pre `w` is not null and not already in a list.
    */
    void
    push_back(T* w) noexcept
    {
        w->next_ = nullptr;
        w->prev_ = tail_;
        if(tail_)
            tail_->next_ = w;
        else
            head_ = w;
        tail_ = w;
    }

    /** Splice all elements from another list to the back.

        All elements from `other` are moved to the back of this
        list. After this call, `other` is empty.

        @param other The list to splice from.

        @post `other.empty() == true`
    */
    void
    splice_back(intrusive_list& other) noexcept
    {
        if(other.empty())
            return;
        if(tail_)
        {
            tail_->next_ = other.head_;
            other.head_->prev_ = tail_;
            tail_ = other.tail_;
        }
        else
        {
            head_ = other.head_;
            tail_ = other.tail_;
        }
        other.head_ = nullptr;
        other.tail_ = nullptr;
    }

    /** Remove and return the front element.

        @return Pointer to the front element, or `nullptr`
            if the list is empty.
    */
    T*
    pop_front() noexcept
    {
        if(!head_)
            return nullptr;
        T* w = head_;
        head_ = head_->next_;
        if(head_)
            head_->prev_ = nullptr;
        else
            tail_ = nullptr;
        return w;
    }

    /** Remove a specific element from the list.

        Unlinks the given element from its current position
        in the list. The element must be a member of this list.

        @param w Pointer to the element to remove.

        @pre `w` is not null and is currently in this list.
    */
    void
    remove(T* w) noexcept
    {
        if(w->prev_)
            w->prev_->next_ = w->next_;
        else
            head_ = w->next_;
        if(w->next_)
            w->next_->prev_ = w->prev_;
        else
            tail_ = w->prev_;
    }
};

} // capy
} // boost

#endif
