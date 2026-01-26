//
// Copyright (c) 2025 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_DETAIL_HOLDER_HPP
#define BOOST_CAPY_DETAIL_HOLDER_HPP

#include <boost/capy/detail/config.hpp>

#include <new>
#include <type_traits>
#include <utility>

namespace boost {
namespace capy {
namespace detail {

/** Base class that holds an object of type T.

    Used as a base class to ensure the held object is
    constructed before other base classes in the
    inheritance list.
*/
template<typename T>
struct holder
{
    T obj_;

    template<typename... Args>
    holder(Args&&... args)
        : obj_(std::forward<Args>(args)...)
    {
    }

    holder(holder const&) = delete;
    holder& operator=(holder const&) = delete;

    holder(holder&& other)
        noexcept(std::is_nothrow_move_constructible_v<T>)
        : obj_(std::move(other.obj_))
    {
    }

    holder& operator=(holder&& other)
        noexcept(std::is_nothrow_move_constructible_v<T>)
    {
        obj_.~T();
        ::new(static_cast<void*>(&obj_)) T(std::move(other.obj_));
        return *this;
    }
};

} // detail
} // capy
} // boost

#endif
