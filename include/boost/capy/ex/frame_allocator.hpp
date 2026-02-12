//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_FRAME_ALLOCATOR_HPP
#define BOOST_CAPY_FRAME_ALLOCATOR_HPP

#include <boost/capy/detail/config.hpp>
#include <boost/capy/detail/frame_memory_resource.hpp>

#include <memory_resource>

/*  Design rationale (pdimov):
    This accessor is a thin wrapper over a thread-local pointer.
    It returns exactly what was stored, including nullptr. No
    dynamic initializer on the thread-local; a dynamic TLS
    initializer moves you into a costlier implementation bucket
    on some platforms - avoid it.

    Null handling is the caller's responsibility (e.g. in
    promise_type::operator new). The accessor must not substitute
    a default, because there are multiple valid choices
    (new_delete_resource, the default pmr resource, etc.). If
    the allocator is not set, it reports "not set" and the
    caller interprets that however it wants.                    */

namespace boost {
namespace capy {

/** Thread-local storage for the current frame allocator.

    This function returns a reference to the thread-local pointer
    that holds the current memory_resource for frame allocation.
    The pointer is set by run_async before creating any tasks.

    @return Reference to the thread-local memory_resource pointer.
*/
inline std::pmr::memory_resource*&
current_frame_allocator() noexcept
{
    static thread_local std::pmr::memory_resource* mr = nullptr;
    return mr;
}

// For backward compatibility
using detail::frame_memory_resource;

} // namespace capy
} // namespace boost

#endif
