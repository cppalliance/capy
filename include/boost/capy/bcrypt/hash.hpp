//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_BCRYPT_HASH_HPP
#define BOOST_CAPY_BCRYPT_HASH_HPP

#include <boost/capy/detail/config.hpp>
#include <boost/capy/bcrypt/error.hpp>
#include <boost/capy/bcrypt/result.hpp>
#include <boost/capy/bcrypt/version.hpp>
#include <boost/core/detail/string_view.hpp>
#include <boost/system/error_code.hpp>

namespace boost {
namespace capy {
namespace bcrypt {

/** Generate a random salt.

    Creates a bcrypt salt string suitable for use with
    the hash() function.

    @par Preconditions
    @code
    rounds >= 4 && rounds <= 31
    @endcode

    @par Exception Safety
    Strong guarantee.

    @par Complexity
    Constant.

    @param rounds Cost factor. Each increment doubles the work.
    Default is 10, which takes approximately 100ms on modern hardware.

    @param ver Hash version to use.

    @return A 29-character salt string.

    @throws std::invalid_argument if rounds is out of range.
    @throws system_error on RNG failure.
*/
BOOST_CAPY_DECL
result
gen_salt(
    unsigned rounds = 10,
    version ver = version::v2b);

/** Hash a password with auto-generated salt.

    Generates a random salt and hashes the password.

    @par Preconditions
    @code
    rounds >= 4 && rounds <= 31
    @endcode

    @par Exception Safety
    Strong guarantee.

    @par Complexity
    O(2^rounds).

    @param password The password to hash. Only the first 72 bytes
    are used (bcrypt limitation).

    @param rounds Cost factor. Each increment doubles the work.

    @param ver Hash version to use.

    @return A 60-character hash string.

    @throws std::invalid_argument if rounds is out of range.
    @throws system_error on RNG failure.
*/
BOOST_CAPY_DECL
result
hash(
    core::string_view password,
    unsigned rounds = 10,
    version ver = version::v2b);

/** Hash a password using a provided salt.

    Uses the given salt to hash the password. The salt should
    be a string previously returned by gen_salt() or extracted
    from a hash string.

    @par Exception Safety
    Strong guarantee.

    @par Complexity
    O(2^rounds).

    @param password The password to hash.

    @param salt The salt string (29 characters).

    @param ec Set to bcrypt::error::invalid_salt if the salt
    is malformed.

    @return A 60-character hash string, or empty result on error.
*/
BOOST_CAPY_DECL
result
hash(
    core::string_view password,
    core::string_view salt,
    system::error_code& ec);

/** Compare a password against a hash.

    Extracts the salt from the hash, re-hashes the password,
    and compares the result.

    @par Exception Safety
    Strong guarantee.

    @par Complexity
    O(2^rounds).

    @param password The plaintext password to check.

    @param hash The hash string to compare against.

    @param ec Set to bcrypt::error::invalid_hash if the hash
    is malformed.

    @return true if the password matches the hash, false if
    it does not match OR if an error occurred. Always check
    ec to distinguish between a mismatch and an error.
*/
BOOST_CAPY_DECL
bool
compare(
    core::string_view password,
    core::string_view hash,
    system::error_code& ec);

/** Extract the cost factor from a hash string.

    @par Exception Safety
    Strong guarantee.

    @par Complexity
    Constant.

    @param hash The hash string to parse.

    @param ec Set to bcrypt::error::invalid_hash if the hash
    is malformed.

    @return The cost factor (4-31) on success, or 0 if an
    error occurred.
*/
BOOST_CAPY_DECL
unsigned
get_rounds(
    core::string_view hash,
    system::error_code& ec);

} // bcrypt
} // capy
} // boost

#endif

