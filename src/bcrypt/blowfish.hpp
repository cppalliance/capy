//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//
// Based on the OpenBSD implementation by Niels Provos.
//

#ifndef BOOST_CAPY_SRC_BCRYPT_BLOWFISH_HPP
#define BOOST_CAPY_SRC_BCRYPT_BLOWFISH_HPP

#include <cstdint>
#include <cstddef>

namespace boost {
namespace capy {
namespace bcrypt {
namespace detail {

// Blowfish context
struct blowfish_ctx
{
    std::uint32_t P[18];
    std::uint32_t S[4][256];
};

// Initialize context with default P and S boxes
void blowfish_init(blowfish_ctx& ctx);

// Expand key into context
void blowfish_expand_key(
    blowfish_ctx& ctx,
    std::uint8_t const* key,
    std::size_t key_len);

// Expand key with salt (for eksblowfish)
void blowfish_expand_key_salt(
    blowfish_ctx& ctx,
    std::uint8_t const* key,
    std::size_t key_len,
    std::uint8_t const* salt,
    std::size_t salt_len);

// Encrypt 8 bytes in place (big-endian)
void blowfish_encrypt(
    blowfish_ctx const& ctx,
    std::uint32_t& L,
    std::uint32_t& R);

// Encrypt data (must be multiple of 8 bytes)
void blowfish_encrypt_ecb(
    blowfish_ctx const& ctx,
    std::uint8_t* data,
    std::size_t len);

} // detail
} // bcrypt
} // capy
} // boost

#endif

