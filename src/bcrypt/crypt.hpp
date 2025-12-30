//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_SRC_BCRYPT_CRYPT_HPP
#define BOOST_CAPY_SRC_BCRYPT_CRYPT_HPP

#include <boost/capy/bcrypt/version.hpp>
#include <boost/core/detail/string_view.hpp>
#include <cstdint>

namespace boost {
namespace capy {
namespace bcrypt {
namespace detail {

// bcrypt constants
constexpr std::size_t BCRYPT_SALT_LEN = 16;      // 128-bit salt
constexpr std::size_t BCRYPT_HASH_LEN = 24;      // 192-bit hash (23 bytes used)
constexpr std::size_t BCRYPT_SALT_OUTPUT_LEN = 29;  // "$2b$XX$" + 22 base64
constexpr std::size_t BCRYPT_HASH_OUTPUT_LEN = 60;  // salt + 31 base64

// Generate random salt bytes
void generate_salt_bytes(std::uint8_t* salt);

// Format salt string: "$2b$XX$<22 base64 chars>"
// Returns number of characters written (29)
std::size_t format_salt(
    char* output,
    std::uint8_t const* salt_bytes,
    unsigned rounds,
    version ver);

// Parse salt string, extract version, rounds, and salt bytes
// Returns true on success
bool parse_salt(
    core::string_view salt_str,
    version& ver,
    unsigned& rounds,
    std::uint8_t* salt_bytes);

// Core bcrypt hash function
// password: null-terminated password (max 72 bytes used)
// salt: 16 bytes
// rounds: cost factor (2^rounds iterations)
// hash: output buffer (24 bytes)
void bcrypt_hash(
    char const* password,
    std::size_t password_len,
    std::uint8_t const* salt,
    unsigned rounds,
    std::uint8_t* hash);

// Format complete hash string
// Returns number of characters written (60)
std::size_t format_hash(
    char* output,
    std::uint8_t const* salt_bytes,
    std::uint8_t const* hash_bytes,
    unsigned rounds,
    version ver);

// Constant-time comparison of hash bytes
bool secure_compare(
    std::uint8_t const* a,
    std::uint8_t const* b,
    std::size_t len);

} // detail
} // bcrypt
} // capy
} // boost

#endif

