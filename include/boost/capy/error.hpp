//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_ERROR_HPP
#define BOOST_CAPY_ERROR_HPP

#include <boost/capy/detail/config.hpp>
#include <system_error>

namespace boost {
namespace capy {

/** Error codes for capy I/O operations.

    These codes are produced by capy algorithms and I/O operations.

    @warning Callers must never compare received `error_code` values
    directly against this enum. Always compare against the portable
    @ref cond error conditions instead. These enum values are
    implementation details subject to change.

    @see cond
*/
enum class error
{
    /// End-of-stream reached. Compare with `cond::eof`.
    eof = 1,

    /// Operation was cancelled. Compare with `cond::canceled`.
    canceled,

    /// Internal test assertion failed.
    test_failure,

    /// Peer closed connection without proper TLS shutdown.
    /// Compare with `cond::stream_truncated`.
    stream_truncated,

    /// Requested item was not found. Compare with `cond::not_found`.
    not_found,

    /// Operation timed out. Compare with `cond::timeout`.
    timeout
};

} // capy
} // boost

namespace std {
template<>
struct is_error_code_enum<
    ::boost::capy::error>
    : std::true_type {};
} // std

namespace boost {
namespace capy {

namespace detail {
 
struct BOOST_CAPY_SYMBOL_VISIBLE
    error_cat_type
    : std::error_category
{
    BOOST_CAPY_DECL const char* name(
        ) const noexcept override;
    BOOST_CAPY_DECL std::string message(
        int) const override;
    constexpr error_cat_type() noexcept = default;
};

BOOST_CAPY_DECL extern error_cat_type error_cat;

} // detail

/// Create an error_code from an error value.
inline
std::error_code
make_error_code(
    error ev) noexcept
{
    return std::error_code{
        static_cast<std::underlying_type<
            error>::type>(ev),
        detail::error_cat};
}

} // capy
} // boost

#endif
