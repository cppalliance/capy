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
#include <system_error>

namespace boost {
namespace capy {

/** Portable error conditions for capy I/O operations.

    These are the conditions callers should compare against when
    handling errors from capy operations. The @ref error enum values
    map to these conditions, as do platform-specific error codes
    (e.g., `ECANCELED`, SSL EOF errors).

    @par Example

    @code
    auto [ec, n] = co_await stream.read_some( bufs );
    if( ec == cond::canceled )
        // handle cancellation
    else if( ec == cond::eof )
        // handle end of stream
    else if( ec )
        // handle other errors
    @endcode

    @see error
*/
enum class cond
{
    /** End-of-stream condition.

        An `error_code` compares equal to `eof` when the stream
        reached its natural end, such as when a peer sends TCP FIN
        or a file reaches EOF.
    */
    eof = 1,

    /** Operation cancelled condition.

        An `error_code` compares equal to `canceled` when the
        operation's stop token was activated, the I/O object's
        `cancel()` was called, or a platform cancellation error
        occurred.
    */
    canceled = 2,

    /** Stream truncated condition.

        An `error_code` compares equal to `stream_truncated` when
        a TLS peer closed the connection without sending a proper
        shutdown alert.
    */
    stream_truncated = 3,

    /** Operation timed out condition.

        An `error_code` compares equal to `timeout` when an
        operation exceeded its allowed duration.
    */
    timeout = 4
};

} // capy
} // boost

namespace std {
template<>
struct is_error_condition_enum<
    ::boost::capy::cond>
    : std::true_type {};
} // std

namespace boost {
namespace capy {

namespace detail {

struct BOOST_CAPY_SYMBOL_VISIBLE
    cond_cat_type
    : std::error_category
{
    BOOST_CAPY_DECL const char* name(
        ) const noexcept override;
    BOOST_CAPY_DECL std::string message(
        int) const override;
    BOOST_CAPY_DECL bool equivalent(
        std::error_code const& ec,
        int condition) const noexcept override;
    constexpr cond_cat_type() noexcept = default;
};

BOOST_CAPY_DECL extern cond_cat_type cond_cat;

} // detail

/// Create an error_condition from a cond value.
inline
std::error_condition
make_error_condition(
    cond ev) noexcept
{
    return std::error_condition{
        static_cast<std::underlying_type<
            cond>::type>(ev),
        detail::cond_cat};
}

} // capy
} // boost

#endif
