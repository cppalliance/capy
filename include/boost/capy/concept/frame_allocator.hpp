//
// Copyright (c) 2025 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_CONCEPT_FRAME_ALLOCATOR_HPP
#define BOOST_CAPY_CONCEPT_FRAME_ALLOCATOR_HPP

#include <boost/capy/detail/config.hpp>

#include <concepts>
#include <cstddef>

namespace boost {
namespace capy {

/** A concept for types that can allocate and deallocate coroutine frames.

    Frame allocators must be cheaply copyable handles to an underlying
    memory resource (e.g., a pointer to a pool). The framework copies
    the allocator into the first coroutine frame for lifetime safety.

    @par Requirements

    Given:
    @li `a` a reference to type `A`
    @li `p` a `void*`
    @li `n` a `std::size_t`

    The following expressions must be valid:
    @li `a.allocate(n)` - Returns `void*`
    @li `a.deallocate(p, n)` - Returns void

    @tparam A The type to check for frame allocator conformance.
*/
template<class A>
concept FrameAllocator =
    std::copy_constructible<A> &&
    requires(A& a, void* p, std::size_t n) {
        { a.allocate(n) } -> std::same_as<void*>;
        { a.deallocate(p, n) };
    };

} // capy
} // boost

#endif
