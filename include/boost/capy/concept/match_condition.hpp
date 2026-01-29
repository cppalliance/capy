//
// Copyright (c) 2025 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_CONCEPT_MATCH_CONDITION_HPP
#define BOOST_CAPY_CONCEPT_MATCH_CONDITION_HPP

#include <boost/capy/detail/config.hpp>
#include <concepts>
#include <cstddef>
#include <string_view>

namespace boost {
namespace capy {

/** Concept for match condition callables.

    A type satisfies `MatchCondition` if it is callable with
    `std::string_view` and a `std::size_t*` hint parameter,
    returning a value convertible to `std::size_t`.

    The callable receives the accumulated buffer data and returns:
    - `std::string_view::npos` if no match is found yet
    - Position after the match (bytes to consume) on success

    The optional `hint` out-parameter allows the matcher to indicate
    how many bytes from the end might be part of a partial match.
    This enables efficient searching across read boundaries. When
    `hint` is null, the matcher should ignore it. When non-null and
    no match is found, the matcher may write the overlap hint.

    @par Example
    @code
    // Simple matcher (ignores hint)
    auto simple = [](std::string_view data, std::size_t*) {
        auto pos = data.find("\r\n");
        return pos != std::string_view::npos
            ? pos + 2 : std::string_view::npos;
    };

    // Matcher with overlap hint for HTTP header end
    struct http_header_matcher {
        std::size_t operator()(
            std::string_view data,
            std::size_t* hint) const noexcept
        {
            auto pos = data.find("\r\n\r\n");
            if(pos != std::string_view::npos)
                return pos + 4;
            if(hint)
                *hint = 3;  // Partial "\r\n\r" possible
            return std::string_view::npos;
        }
    };
    static_assert(MatchCondition<http_header_matcher>);
    @endcode

    @see read_until
*/
template<class F>
concept MatchCondition = requires(F f, std::string_view data, std::size_t* hint) {
    { f(data, hint) } -> std::convertible_to<std::size_t>;
};

} // namespace capy
} // namespace boost

#endif
