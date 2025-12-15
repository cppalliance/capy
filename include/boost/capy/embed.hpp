//
// Copyright (c) 2025 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_EMBED_HPP
#define BOOST_CAPY_EMBED_HPP

#include <boost/capy/detail/config.hpp>
#include <boost/core/detail/string_view.hpp>

#if __cpp_lib_string_view >= 201606L
# include <string_view>
#endif

namespace boost {
namespace capy {

/** Embed a string literal as a string_view
    
    The embed class template is used to embed a string literal
    in source code as a string_view. The first character of
    the string literal will be removed, typically this is a
    newline, allowing the string to be formatted nicely in code.

    @par Example
    @code
    embed text(R"(
    Hello "world"
    This has quotes and )
    )");
    core::string_view sv = text.get();
    @endcode
    The resulting string_view `sv` will contain:
    ```
    Hello "world"
    This has quotes and )
    ```
*/
struct embed
{
    /** Constructor
        The string literal `s` should be a raw string literal.
        The first character (typically a newline) will be
        removed from the resulting string_view.
        @param s The string literal
    */
    template<std::size_t N>
    constexpr
    embed(
        const char (&s)[N]) noexcept
        : s_(s + 1, N - 2)
    {
    }

    /** Conversion to string_view
    */
    operator core::string_view() const noexcept
    {
        return s_;
    }

#if __cpp_lib_string_view >= 201606L
    /** Conversion to std::string_view
    */
    operator std::string_view() const noexcept
    {
        return std::string_view(s_.data(), s_.size());
    }
#endif

    /** Return the string_view
    */
    core::string_view
    get() const noexcept
    {
        return s_;
    }

    /** Dereference operator
    */
    core::string_view
    operator*() const noexcept
    {
        return s_;
    }

    /** Member access operator
    */
    core::string_view const*
    operator->() const noexcept
    {
        return &s_;
    }

private:
    core::string_view s_;
};

} // capy
} // boost

#endif
