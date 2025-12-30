//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#include <boost/capy/bcrypt/hash.hpp>
#include <boost/capy/detail/except.hpp>
#include "base64.hpp"
#include "crypt.hpp"

namespace boost {
namespace capy {
namespace bcrypt {

result
gen_salt(
    unsigned rounds,
    version ver)
{
    // Validate preconditions
    if (rounds < 4 || rounds > 31)
        capy::detail::throw_invalid_argument("bcrypt rounds must be 4-31");

    // Generate random salt
    std::uint8_t salt_bytes[detail::BCRYPT_SALT_LEN];
    detail::generate_salt_bytes(salt_bytes);

    // Format salt string
    result r;
    std::size_t len = detail::format_salt(
        r.buf(),
        salt_bytes,
        rounds,
        ver);

    r.set_size(static_cast<unsigned char>(len));
    return r;
}

result
hash(
    core::string_view password,
    unsigned rounds,
    version ver)
{
    // Validate preconditions
    if (rounds < 4 || rounds > 31)
        capy::detail::throw_invalid_argument("bcrypt rounds must be 4-31");

    // Generate random salt
    std::uint8_t salt_bytes[detail::BCRYPT_SALT_LEN];
    detail::generate_salt_bytes(salt_bytes);

    // Hash password
    std::uint8_t hash_bytes[detail::BCRYPT_HASH_LEN];
    detail::bcrypt_hash(
        password.data(),
        password.size(),
        salt_bytes,
        rounds,
        hash_bytes);

    // Format output
    result r;
    std::size_t len = detail::format_hash(
        r.buf(),
        salt_bytes,
        hash_bytes,
        rounds,
        ver);

    r.set_size(static_cast<unsigned char>(len));
    return r;
}

result
hash(
    core::string_view password,
    core::string_view salt,
    system::error_code& ec)
{
    ec = {};

    // Parse salt
    version ver;
    unsigned rounds;
    std::uint8_t salt_bytes[detail::BCRYPT_SALT_LEN];

    if (!detail::parse_salt(salt, ver, rounds, salt_bytes))
    {
        ec = make_error_code(error::invalid_salt);
        return result{};
    }

    // Hash password
    std::uint8_t hash_bytes[detail::BCRYPT_HASH_LEN];
    detail::bcrypt_hash(
        password.data(),
        password.size(),
        salt_bytes,
        rounds,
        hash_bytes);

    // Format output
    result r;
    std::size_t len = detail::format_hash(
        r.buf(),
        salt_bytes,
        hash_bytes,
        rounds,
        ver);

    r.set_size(static_cast<unsigned char>(len));
    return r;
}

bool
compare(
    core::string_view password,
    core::string_view hash_str,
    system::error_code& ec)
{
    ec = {};

    // Parse hash to extract salt
    version ver;
    unsigned rounds;
    std::uint8_t salt_bytes[detail::BCRYPT_SALT_LEN];

    if (!detail::parse_salt(hash_str, ver, rounds, salt_bytes))
    {
        ec = make_error_code(error::invalid_hash);
        return false;
    }

    // Validate hash length
    if (hash_str.size() != detail::BCRYPT_HASH_OUTPUT_LEN)
    {
        ec = make_error_code(error::invalid_hash);
        return false;
    }

    // Decode stored hash (31 base64 chars starting at position 29)
    std::uint8_t stored_hash[detail::BCRYPT_HASH_LEN];
    int decoded = detail::base64_decode(
        stored_hash,
        hash_str.data() + 29,
        31);

    if (decoded < 0)
    {
        ec = make_error_code(error::invalid_hash);
        return false;
    }

    // Compute hash of provided password
    std::uint8_t computed_hash[detail::BCRYPT_HASH_LEN];
    detail::bcrypt_hash(
        password.data(),
        password.size(),
        salt_bytes,
        rounds,
        computed_hash);

    // Constant-time comparison (only first 23 bytes are used)
    return detail::secure_compare(stored_hash, computed_hash, 23);
}

unsigned
get_rounds(
    core::string_view hash_str,
    system::error_code& ec)
{
    ec = {};

    // Minimum length check
    if (hash_str.size() < 7)
    {
        ec = make_error_code(error::invalid_hash);
        return 0;
    }

    char const* s = hash_str.data();

    // Check prefix
    if (s[0] != '$' || s[1] != '2')
    {
        ec = make_error_code(error::invalid_hash);
        return 0;
    }

    // Check version character
    if ((s[2] != 'a' && s[2] != 'b' && s[2] != 'y') || s[3] != '$')
    {
        ec = make_error_code(error::invalid_hash);
        return 0;
    }

    // Parse rounds
    if (s[4] < '0' || s[4] > '9' || s[5] < '0' || s[5] > '9')
    {
        ec = make_error_code(error::invalid_hash);
        return 0;
    }

    unsigned rounds = static_cast<unsigned>((s[4] - '0') * 10 + (s[5] - '0'));
    if (rounds < 4 || rounds > 31)
    {
        ec = make_error_code(error::invalid_hash);
        return 0;
    }

    return rounds;
}

} // bcrypt
} // capy
} // boost

