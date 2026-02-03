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

/** Concept for callables that detect delimiters in streamed data.

    A type satisfies `MatchCondition` if it is callable with
    `std::string_view` and a `std::size_t*` hint parameter,
    returning the position after a match or `npos` if not found.
    Used by `read_until` to scan accumulated data for delimiters.

    @tparam F The callable type.

    @par Syntactic Requirements

    @li `f(data, hint)` must be a valid expression where:
        - `data` is `std::string_view`
        - `hint` is `std::size_t*` (may be null)
    @li The return type must be convertible to `std::size_t`

    @par Semantic Requirements

    The callable scans `data` for a delimiter or pattern:

    @li On match: returns position after the match (bytes to consume)
    @li On no match: returns `std::string_view::npos`

    The `hint` parameter enables efficient cross-boundary searching:

    @li If `hint` is null, the matcher ignores it
    @li If `hint` is non-null and no match is found, the matcher may
        write how many bytes from the end might be part of a partial
        match (e.g., 3 for "\r\n\r" when searching for "\r\n\r\n")
    @li The hint allows the caller to retain only necessary bytes
        when the buffer must be compacted

    @par Conforming Signatures

    @code
    std::size_t operator()( std::string_view data, std::size_t* hint ) const;
    @endcode

    @par Example

    @code
    // Simple line matcher (ignores hint)
    auto line_matcher = []( std::string_view data, std::size_t* ) {
        auto pos = data.find( "\r\n" );
        return pos != std::string_view::npos ? pos + 2 : pos;
    };

    // HTTP header end matcher with overlap hint
    struct http_header_matcher
    {
        std::size_t operator()(
            std::string_view data,
            std::size_t* hint ) const noexcept
        {
            auto pos = data.find( "\r\n\r\n" );
            if( pos != std::string_view::npos )
                return pos + 4;
            if( hint )
                *hint = 3;  // "\r\n\r" might span reads
            return std::string_view::npos;
        }
    };

    static_assert( MatchCondition<http_header_matcher> );
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
