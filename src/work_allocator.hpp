//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/boostorg/capy
//

#ifndef BOOST_CAPY_SRC_WORK_ALLOCATOR_HPP
#define BOOST_CAPY_SRC_WORK_ALLOCATOR_HPP

#include <cstddef>

namespace boost {
namespace capy {

/** A pool of arenas for dynamic allocation patterns.

    @par Allocation Order Invariant

    Allocations always come from the newest arena (tail).
    Once an arena is superseded by a newer one, it never
    receives new allocations. This ensures all allocations
    in arena N occurred before all allocations in arena N+1.

    Deallocations can occur in any order. When an older
    arena empties, it becomes "parked" — held for recycling
    rather than deleted immediately.

    @par Arena States

    - Active: The tail arena; receives all new allocations.
    - Draining: Older arenas with outstanding allocations.
    - Parked: Empty arenas awaiting recycling or deletion.

    @par Recycling

    When the active arena fills, a parked arena may be
    recycled as the new active arena. This avoids malloc/free
    churn under steady-state load. Recycled arenas are moved
    to the tail of the list, becoming the new active arena.

    This class is not thread-safe.
*/
class work_allocator
{
public:
    class arena;

private:
    arena* head_;
    arena* tail_;
    std::size_t arena_count_;
    std::size_t next_size_;
    std::size_t min_size_;
    std::size_t max_size_;
    std::size_t keep_empty_;

public:
    ~work_allocator();

    explicit
    work_allocator(
        std::size_t min_size = 4096,
        std::size_t max_size = 1048576,
        std::size_t keep_empty = 1);

    work_allocator(work_allocator const&) = delete;
    work_allocator& operator=(work_allocator const&) = delete;

    /** Return the number of arenas.
    */
    std::size_t
    arena_count() const noexcept
    {
        return arena_count_;
    }

    /** Return allocated memory.

        @throws std::bad_alloc on failure.
    */
    void* allocate(std::size_t size, std::size_t align);

    /** Release an allocation.
    */
    void deallocate(void* p, std::size_t size, std::size_t align) noexcept;

private:
    void link_at_tail(arena* a) noexcept;
    void unlink(arena* a) noexcept;
    arena* find_arena(void* p) noexcept;
    arena* find_parked() noexcept;
    void prune() noexcept;
};

//------------------------------------------------------------------------------

/** A fixed-size arena that allocates from high to low addresses.

    Memory is allocated from the top of the buffer downward.
    Deallocation only decrements a counter; actual memory is
    reused only when all allocations are released.

    Arenas are linked in a doubly-linked list managed by
    work_allocator.
*/
class work_allocator::arena
{
    friend class work_allocator;

    arena* prev_;
    arena* next_;
    void* base_;
    std::size_t capacity_;
    std::size_t offset_;
    std::size_t count_;

public:
    ~arena();

    explicit
    arena(std::size_t capacity);

    arena(arena const&) = delete;
    arena& operator=(arena const&) = delete;

    /** Return the total capacity in bytes.
    */
    std::size_t
    capacity() const noexcept
    {
        return capacity_;
    }

    /** Return the number of active allocations.
    */
    std::size_t
    count() const noexcept
    {
        return count_;
    }

    /** Return true if there are no active allocations.
    */
    bool
    empty() const noexcept
    {
        return count_ == 0;
    }

    /** Return true if the pointer is within this arena.
    */
    bool
    owns(void* p) const noexcept;

    /** Return allocated memory, or nullptr if full.
    */
    void*
    allocate(std::size_t size, std::size_t align) noexcept;

    /** Release an allocation.
    */
    void
    deallocate(void* p, std::size_t size, std::size_t align) noexcept;

    /** Reset the arena for reuse.
    */
    void
    reset() noexcept;
};

} // capy
} // boost

#endif
