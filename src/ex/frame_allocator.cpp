//
// Copyright (c) 2026 Steve Gerbino
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#include <boost/capy/ex/frame_allocator.hpp>

#if (defined(BOOST_CAPY_DYN_LINK) || defined(BOOST_ALL_DYN_LINK)) && \
    !defined(BOOST_CAPY_STATIC_LINK)

namespace boost {
namespace capy {
namespace detail {

std::pmr::memory_resource*&
current_frame_allocator_ref() noexcept
{
    static thread_local std::pmr::memory_resource* mr = nullptr;
    return mr;
}

} // namespace detail
} // namespace capy
} // namespace boost

#endif
