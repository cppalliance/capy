//
// Copyright (c) 2026 Michael Vandeberg
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_DETAIL_SERVICE_SLOT_HPP
#define BOOST_CAPY_DETAIL_SERVICE_SLOT_HPP

#include <atomic>
#include <cstddef>

namespace boost {
namespace capy {
namespace detail {

/* Slot ID infrastructure for O(1) service lookup.

   Each distinct service type T gets a unique integer index via
   service_slot<T>(). The index is assigned on first call from a
   global atomic counter and cached in a function-local static.
   Cross-DLL safety relies on COMDAT deduplication (same mechanism
   as type_id_impl<T>::tag).
*/

inline std::atomic<std::size_t> next_service_slot{0};

template<class T>
std::size_t
service_slot() noexcept
{
    static const std::size_t id =
        next_service_slot.fetch_add(1, std::memory_order_relaxed);
    return id;
}

} // namespace detail
} // namespace capy
} // namespace boost

#endif
