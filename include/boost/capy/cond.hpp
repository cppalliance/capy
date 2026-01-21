//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_COND_HPP
#define BOOST_CAPY_COND_HPP

#include <boost/capy/detail/config.hpp>
#include <boost/system/error_category.hpp>
#include <boost/system/is_error_condition_enum.hpp>

namespace boost {
namespace capy {

/** Portable error conditions.

    Use these conditions for portable error comparisons:

  - Return `error::eof` when originating an eof error.
      Check `ec == cond::eof` to portably test for eof.

  - Return the platform canceled error when originating canceled.
      Check `ec == cond::canceled` to portably test for cancellation.

  - Return `error::stream_truncated` when peer closes without TLS shutdown.
      Check `ec == cond::stream_truncated` to portably test for truncation.
*/
enum class cond
{
    eof = 1,
    canceled = 2,
    stream_truncated = 3
};

//-----------------------------------------------

} // capy

namespace system {
template<>
struct is_error_condition_enum<
    ::boost::capy::cond>
{
    static bool const value = true;
};
} // system

namespace capy {

//-----------------------------------------------

namespace detail {

struct BOOST_SYMBOL_VISIBLE
    cond_cat_type
    : system::error_category
{
    BOOST_CAPY_DECL const char* name(
        ) const noexcept override;
    BOOST_CAPY_DECL std::string message(
        int) const override;
    BOOST_CAPY_DECL char const* message(
        int, char*, std::size_t
            ) const noexcept override;
    BOOST_CAPY_DECL bool equivalent(
        system::error_code const& ec,
        int condition) const noexcept override;
    BOOST_SYSTEM_CONSTEXPR cond_cat_type()
        : error_category(0x2f7a9b3c4e8d1a05)
    {
    }
};

BOOST_CAPY_DECL extern cond_cat_type cond_cat;

} // detail

//-----------------------------------------------

inline
BOOST_SYSTEM_CONSTEXPR
system::error_condition
make_error_condition(
    cond ev) noexcept
{
    return system::error_condition{
        static_cast<std::underlying_type<
            cond>::type>(ev),
        detail::cond_cat};
}

} // capy
} // boost

#endif
