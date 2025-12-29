//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/boostorg/capy
//

#include "work_allocator.hpp"
#include <cstdlib>
#include <functional>
#include <new>

namespace boost {
namespace capy {

//------------------------------------------------------------------------------
//
// work_allocator::arena
//
//------------------------------------------------------------------------------

work_allocator::arena::
~arena()
{
    std::free(base_);
}

work_allocator::arena::
arena(std::size_t capacity)
    : prev_(nullptr)
    , next_(nullptr)
    , base_(std::malloc(capacity))
    , capacity_(capacity)
    , offset_(capacity)
    , count_(0)
{
    if(!base_)
        throw std::bad_alloc();
}

bool
work_allocator::arena::
owns(void* p) const noexcept
{
    std::less<char const*> cmp;
    auto* cp = static_cast<char const*>(p);
    auto* base = static_cast<char const*>(base_);
    // cp >= base && cp < base + capacity_
    return !cmp(cp, base) && cmp(cp, base + capacity_);
}

void*
work_allocator::arena::
allocate(std::size_t size, std::size_t align) noexcept
{
    if(offset_ < size)
        return nullptr;

    std::size_t aligned = (offset_ - size) & ~(align - 1);
    
    if(aligned > offset_)
        return nullptr;

    offset_ = aligned;
    ++count_;
    return static_cast<char*>(base_) + offset_;
}

void
work_allocator::arena::
deallocate(void* /*p*/, std::size_t /*size*/, std::size_t /*align*/) noexcept
{
    --count_;
}

void
work_allocator::arena::
reset() noexcept
{
    offset_ = capacity_;
    count_ = 0;
}

//------------------------------------------------------------------------------
//
// work_allocator
//
//------------------------------------------------------------------------------

work_allocator::
~work_allocator()
{
    arena* a = head_;
    while(a)
    {
        arena* next = a->next_;
        delete a;
        a = next;
    }
}

work_allocator::
work_allocator(
    std::size_t min_size,
    std::size_t max_size,
    std::size_t keep_empty)
    : head_(nullptr)
    , tail_(nullptr)
    , arena_count_(0)
    , next_size_(min_size)
    , min_size_(min_size)
    , max_size_(max_size)
    , keep_empty_(keep_empty)
{
}

void*
work_allocator::
allocate(std::size_t size, std::size_t align)
{
    // Always allocate from tail (active arena)
    if(tail_)
    {
        if(void* p = tail_->allocate(size, align))
            return p;
    }

    // Active arena full or none exists.
    // Try recycling a parked arena to preserve allocation order.
    arena* fresh = find_parked();
    if(fresh)
    {
        unlink(fresh);
        fresh->reset();
        link_at_tail(fresh);
    }
    else
    {
        std::size_t arena_size = next_size_;
        if(arena_size < size + align)
            arena_size = size + align;

        fresh = new arena(arena_size);
        link_at_tail(fresh);

        if(next_size_ < max_size_)
        {
            next_size_ *= 2;
            if(next_size_ > max_size_)
                next_size_ = max_size_;
        }
    }

    void* p = tail_->allocate(size, align);
    if(!p)
        throw std::bad_alloc();
    return p;
}

void
work_allocator::
deallocate(void* p, std::size_t size, std::size_t align) noexcept
{
    arena* a = find_arena(p);
    if(!a)
        return;

    a->deallocate(p, size, align);

    // Prune when a non-active arena empties
    if(a->empty() && a != tail_)
        prune();
}

void
work_allocator::
link_at_tail(arena* a) noexcept
{
    a->prev_ = tail_;
    a->next_ = nullptr;
    if(tail_)
        tail_->next_ = a;
    else
        head_ = a;
    tail_ = a;
    ++arena_count_;
}

void
work_allocator::
unlink(arena* a) noexcept
{
    if(a->prev_)
        a->prev_->next_ = a->next_;
    else
        head_ = a->next_;

    if(a->next_)
        a->next_->prev_ = a->prev_;
    else
        tail_ = a->prev_;

    a->prev_ = nullptr;
    a->next_ = nullptr;
    --arena_count_;
}

work_allocator::arena*
work_allocator::
find_arena(void* p) noexcept
{
    for(arena* a = head_; a; a = a->next_)
    {
        if(a->owns(p))
            return a;
    }
    return nullptr;
}

work_allocator::arena*
work_allocator::
find_parked() noexcept
{
    // Search from oldest, skip the active arena (tail)
    for(arena* a = head_; a && a != tail_; a = a->next_)
    {
        if(a->empty())
            return a;
    }
    return nullptr;
}

void
work_allocator::
prune() noexcept
{
    // Count parked (empty non-active) arenas
    std::size_t parked_count = 0;
    for(arena* a = head_; a && a != tail_; a = a->next_)
    {
        if(a->empty())
            ++parked_count;
    }

    // Delete excess parked arenas from the front
    arena* a = head_;
    while(a && a != tail_ && parked_count > keep_empty_)
    {
        arena* next = a->next_;
        if(a->empty())
        {
            unlink(a);
            delete a;
            --parked_count;
        }
        a = next;
    }

    // Shrink next_size if we're back to minimal state
    if(arena_count_ <= 1)
        next_size_ = min_size_;
}

} // capy
} // boost

