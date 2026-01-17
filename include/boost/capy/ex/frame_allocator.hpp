//
// Copyright (c) 2025 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_FRAME_ALLOCATOR_HPP
#define BOOST_CAPY_FRAME_ALLOCATOR_HPP

#include <boost/capy/detail/config.hpp>
#include <boost/capy/concept/frame_allocator.hpp>

#include <cstddef>
#include <new>
#include <utility>

namespace boost {
namespace capy {

//----------------------------------------------------------
// Public API
//----------------------------------------------------------

/** A frame allocator that passes through to global new/delete.

    This allocator provides no pooling or recycling—each allocation
    goes directly to `::operator new` and each deallocation goes to
    `::operator delete`. It serves as a baseline for comparison and
    as a fallback when pooling is not desired.
*/
struct default_frame_allocator
{
    void* allocate(std::size_t n)
    {
        return ::operator new(n);
    }

    void deallocate(void* p, std::size_t)
    {
        ::operator delete(p);
    }
};

static_assert(frame_allocator<default_frame_allocator>);

//----------------------------------------------------------
// Implementation details
//----------------------------------------------------------

namespace detail {

/** Abstract base class for internal frame allocator wrappers.

    This class provides a polymorphic interface used internally
    by the frame allocation machinery. User-defined allocators
    do not inherit from this class.
*/
class frame_allocator_base
{
public:
    virtual ~frame_allocator_base() {}

    /** Allocate memory for a coroutine frame.

        @param n The number of bytes to allocate.

        @return A pointer to the allocated memory.
    */
    virtual void* allocate(std::size_t n) = 0;

    /** Deallocate memory for a coroutine frame.

        @param p Pointer to the memory to deallocate.
        @param n The user-requested size (not total allocation).
    */
    virtual void deallocate(void* p, std::size_t n) = 0;
};

/** Frame allocator wrapper that lives in the launcher frame.

    This wrapper is stored in the run_async launcher's promise and
    handles all coroutine frame allocations. Because the launcher
    frame is destroyed LAST (after all inner coroutines), this
    wrapper is guaranteed to outlive all frames that reference it.

    All allocated frames have the layout: [frame | ptr]
    where ptr points back to this wrapper for deallocation.

    @tparam Allocator The underlying allocator type satisfying frame_allocator.
*/
template<frame_allocator Allocator>
class frame_allocator_wrapper : public frame_allocator_base
{
    Allocator alloc_;

    static constexpr std::size_t alignment = alignof(void*);

    static std::size_t
    aligned_offset(std::size_t n) noexcept
    {
        return (n + alignment - 1) & ~(alignment - 1);
    }

public:
    explicit frame_allocator_wrapper(Allocator a)
        : alloc_(std::move(a))
    {
    }

    void*
    allocate(std::size_t n) override
    {
        // Layout: [frame | ptr]
        std::size_t ptr_offset = aligned_offset(n);
        std::size_t total = ptr_offset + sizeof(frame_allocator_base*);

        void* raw = alloc_.allocate(total);

        // Store pointer to self at fixed offset
        auto* ptr_loc = reinterpret_cast<frame_allocator_base**>(
            static_cast<char*>(raw) + ptr_offset);
        *ptr_loc = this;

        return raw;
    }

    void
    deallocate(void* block, std::size_t user_size) override
    {
        std::size_t ptr_offset = aligned_offset(user_size);
        std::size_t total = ptr_offset + sizeof(frame_allocator_base*);
        alloc_.deallocate(block, total);
    }
};

} // namespace detail

/** Mixin base for promise types to support custom frame allocation.

    Derive your promise_type from this class to enable custom coroutine
    frame allocation via a thread-local allocator pointer.

    The allocation strategy:
    @li If a thread-local allocator is set, use it for allocation
    @li Otherwise, fall back to global `::operator new`/`::operator delete`

    A pointer is stored at the end of each allocation to enable correct
    deallocation regardless of which allocator was active at allocation time.

    @par Memory Layout

    All coroutine frames have the same layout:
    @code
    [coroutine frame | ptr]
    @endcode

    Where ptr points to the frame_allocator_wrapper in the launcher frame,
    or is nullptr if allocated with global new/delete.

    @see frame_allocator
*/
struct frame_allocating_base
{
private:
    static constexpr std::size_t alignment = alignof(void*);

    static std::size_t
    aligned_offset(std::size_t n) noexcept
    {
        return (n + alignment - 1) & ~(alignment - 1);
    }

    static detail::frame_allocator_base*&
    current_allocator() noexcept
    {
        static thread_local detail::frame_allocator_base* alloc = nullptr;
        return alloc;
    }

public:
    /** Set the thread-local frame allocator.

        The allocator will be used for subsequent coroutine frame
        allocations on this thread until changed or cleared.

        @param alloc The allocator to use. Must outlive all coroutines
                     allocated with it.
    */
    static void
    set_frame_allocator(detail::frame_allocator_base& alloc) noexcept
    {
        current_allocator() = &alloc;
    }

    /** Clear the thread-local frame allocator.

        Subsequent allocations will use global `::operator new`.
    */
    static void
    clear_frame_allocator() noexcept
    {
        current_allocator() = nullptr;
    }

    /** Get the current thread-local frame allocator.

        @return Pointer to current allocator, or nullptr if none set.
    */
    static detail::frame_allocator_base*
    get_frame_allocator() noexcept
    {
        return current_allocator();
    }

    // GCC 11+ emits -Wmismatched-new-delete because it tracks that
    // operator new returns a pointer, and operator delete should be
    // the one to free it. Our design intentionally over-allocates to
    // store a pointer at the end of each frame. The deallocation is
    // correct: we recalculate the total size and free the full block.
    // This warning is suppressed for these two functions only.
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmismatched-new-delete"
#endif

    /** Allocate a coroutine frame.

        If a thread-local allocator is set, delegates to it.
        Otherwise, allocates with extra space for a null pointer marker.
    */
    static void*
    operator new(std::size_t size)
    {
        auto* alloc = current_allocator();
        if(!alloc)
        {
            // No allocator: allocate extra space for null pointer marker
            std::size_t ptr_offset = aligned_offset(size);
            std::size_t total = ptr_offset + sizeof(detail::frame_allocator_base*);
            void* raw = ::operator new(total);

            // Store nullptr to indicate global new/delete
            auto* ptr_loc = reinterpret_cast<detail::frame_allocator_base**>(
                static_cast<char*>(raw) + ptr_offset);
            *ptr_loc = nullptr;

            return raw;
        }
        return alloc->allocate(size);
    }

    /** Deallocate a coroutine frame.

        Reads the pointer stored at the end of the frame to find
        the allocator wrapper. A null pointer indicates the frame
        was allocated with global new/delete (no custom allocator
        was active).
    */
    static void
    operator delete(void* ptr, std::size_t size)
    {
        // Pointer is always at aligned_offset(size)
        std::size_t ptr_offset = aligned_offset(size);
        auto* ptr_loc = reinterpret_cast<detail::frame_allocator_base**>(
            static_cast<char*>(ptr) + ptr_offset);
        auto* wrapper = *ptr_loc;

        // Null pointer means global new/delete
        if(!wrapper)
        {
            ::operator delete(ptr);
            return;
        }

        wrapper->deallocate(ptr, size);
    }

#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
};

} // namespace capy
} // namespace boost

#endif
