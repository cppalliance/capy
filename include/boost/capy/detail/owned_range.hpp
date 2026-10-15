//
// Copyright (c) 2026 Steve Gerbino
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_DETAIL_OWNED_RANGE_HPP
#define BOOST_CAPY_DETAIL_OWNED_RANGE_HPP

#include <boost/capy/detail/config.hpp>

#include <ranges>
#include <type_traits>
#include <utility>
#include <vector>

namespace boost {
namespace capy {
namespace detail {

// A join's runners refer to their awaitables in place until they start,
// so the range it owns must yield lvalues that outlive the iteration.
// Only a forward range guarantees that: an input iterator may hold the
// element it yields and replace it on the next step. Any other range is
// moved into a vector instead.
template<class R>
using owned_range_t = std::conditional_t<
    std::ranges::forward_range<std::remove_cvref_t<R>&> &&
    std::is_lvalue_reference_v<
        std::ranges::range_reference_t<std::remove_cvref_t<R>&>>,
    std::remove_cvref_t<R>,
    std::vector<std::ranges::range_value_t<R>>>;

template<class R>
owned_range_t<R>
own_range(R&& r)
{
    if constexpr(std::is_same_v<owned_range_t<R>, std::remove_cvref_t<R>>)
    {
        return std::forward<R>(r);
    }
    else
    {
        owned_range_t<R> v;
        v.reserve(std::ranges::size(r));
        for(auto&& a : r)
            v.push_back(std::move(a));
        return v;
    }
}

} // namespace detail
} // namespace capy
} // namespace boost

#endif
