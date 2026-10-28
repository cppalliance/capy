//
// Copyright (c) 2026 Steve Gerbino
// Copyright (c) 2026 Michael Vandeberg
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Test that header file is self-contained.
#include <boost/capy/detail/frame_memory_resource.hpp>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <memory_resource>
#include <new>

#include "test_suite.hpp"

namespace boost {
namespace capy {
namespace detail {

class frame_memory_resource_test
{
public:
    void
    run()
    {
        frame_memory_resource<std::allocator<int>> fmr{
            std::allocator<int>{}};

        std::pmr::memory_resource* mr = fmr.get();
        BOOST_TEST(mr == &fmr);

        // do_allocate / do_deallocate round trip through the resource.
        void* p = mr->allocate(64, alignof(std::max_align_t));
        BOOST_TEST(p != nullptr);
        mr->deallocate(p, 64, alignof(std::max_align_t));

        // do_is_equal: identity only.
        frame_memory_resource<std::allocator<int>> other{
            std::allocator<int>{}};
        BOOST_TEST(mr->is_equal(*mr));
        BOOST_TEST(! mr->is_equal(*other.get()));

        testAlignmentHonored();
        testHugeRequestThrows();
        testOverAlignedRequestThrows();
    }

    // allocate() is declared __alloc_align__, so a compiler may fold
    // a check of its result to true; read the address back through a
    // volatile.
    static std::uintptr_t
    misalignment(void* p) noexcept
    {
        volatile std::uintptr_t const address =
            reinterpret_cast<std::uintptr_t>(p);
        return address % frame_alignment;
    }

    // A byte allocator over a bump resource packs allocations at
    // byte alignment; frames still need frame_alignment. The 1-byte
    // block leaves the arena off any alignment.
    void
    testAlignmentHonored()
    {
        std::pmr::monotonic_buffer_resource upstream;
        frame_memory_resource<std::pmr::polymorphic_allocator<std::byte>>
            fmr{&upstream};
        std::pmr::memory_resource* mr = fmr.get();

        constexpr std::size_t align = frame_alignment;
        void* p1 = mr->allocate(1, align);
        void* p2 = mr->allocate(align + 8, align);
        BOOST_TEST_EQ(misalignment(p1), 0u);
        BOOST_TEST_EQ(misalignment(p2), 0u);
        mr->deallocate(p2, align + 8, align);
        mr->deallocate(p1, 1, align);
    }

    // Rounding a request near SIZE_MAX up to whole units must not
    // wrap to a small count and hand back a tiny block.
    void
    testHugeRequestThrows()
    {
        frame_memory_resource<std::allocator<int>> fmr{
            std::allocator<int>{}};
        std::pmr::memory_resource* mr = fmr.get();

        BOOST_TEST_THROWS(
            mr->allocate(
                std::numeric_limits<std::size_t>::max() - 3,
                alignof(std::max_align_t)),
            std::bad_alloc);
    }

    // A block is only aligned to frame_alignment, so a request for
    // more must fail rather than return misaligned memory.
    void
    testOverAlignedRequestThrows()
    {
        frame_memory_resource<std::allocator<int>> fmr{
            std::allocator<int>{}};
        std::pmr::memory_resource* mr = fmr.get();

        BOOST_TEST_THROWS(
            mr->allocate(1, 2 * frame_alignment),
            std::bad_alloc);
    }
};

TEST_SUITE(frame_memory_resource_test, "boost.capy.detail.frame_memory_resource");

} // detail
} // capy
} // boost
