//
// Copyright (c) 2026 Michael Vandeberg
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_DETAIL_VOID_TO_MONOSTATE_HPP
#define BOOST_CAPY_DETAIL_VOID_TO_MONOSTATE_HPP

#include <type_traits>
#include <variant>

namespace boost {
namespace capy {

/** Map void to std::monostate, pass other types through unchanged.

    std::variant<void, ...> and std::tuple members of type void are
    ill-formed, so void-returning tasks contribute std::monostate
    instead. This preserves task-index-to-result-index mapping in
    both when_all and when_any.
*/
template<typename T>
using void_to_monostate_t = std::conditional_t<std::is_void_v<T>, std::monostate, T>;

} // namespace capy
} // namespace boost

#endif
