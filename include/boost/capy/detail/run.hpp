//
// Copyright (c) 2025 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_DETAIL_RUN_HPP
#define BOOST_CAPY_DETAIL_RUN_HPP

#include <boost/capy/detail/config.hpp>
#include <boost/capy/detail/frame_memory_resource.hpp>

#include <cstddef>
#include <memory_resource>

namespace boost {
namespace capy {
namespace detail {

/// Concept for standard Allocator types.
template<class A>
concept Allocator = requires(A a, std::size_t n) {
    typename A::value_type;
    { a.allocate(n) } -> std::same_as<typename A::value_type*>;
};

/// Specialization for memory_resource* - stores pointer directly.
template<>
class frame_memory_resource<std::pmr::memory_resource*>
{
    std::pmr::memory_resource* mr_;

public:
    explicit frame_memory_resource(std::pmr::memory_resource* mr) noexcept
        : mr_(mr)
    {
    }

    std::pmr::memory_resource* get() noexcept { return mr_; }
};

/// Specialization for void - no allocator (empty).
template<>
class frame_memory_resource<void>
{
public:
    frame_memory_resource() = default;

    std::pmr::memory_resource* get() noexcept { return nullptr; }
};

} // namespace detail
} // namespace capy
} // namespace boost

#endif
