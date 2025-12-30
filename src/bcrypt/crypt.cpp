//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#include "crypt.hpp"
#include "base64.hpp"
#include "blowfish.hpp"
#include "random.hpp"
#include <cstring>
#include <algorithm>

namespace boost {
namespace capy {
namespace bcrypt {
namespace detail {

namespace {

// "OrpheanBeholderScryDoubt" - magic string for bcrypt
constexpr std::uint8_t magic_text[24] = {
    'O', 'r', 'p', 'h', 'e', 'a', 'n', 'B',
    'e', 'h', 'o', 'l', 'd', 'e', 'r', 'S',
    'c', 'r', 'y', 'D', 'o', 'u', 'b', 't'
};

char const* version_prefix(version ver)
{
    switch (ver)
    {
    case version::v2a: return "$2a$";
    case version::v2b: return "$2b$";
    default: return "$2b$";
    }
}

} // namespace

void generate_salt_bytes(std::uint8_t* salt)
{
    fill_random(salt, BCRYPT_SALT_LEN);
}

std::size_t format_salt(
    char* output,
    std::uint8_t const* salt_bytes,
    unsigned rounds,
    version ver)
{
    char* p = output;

    // Version prefix
    char const* prefix = version_prefix(ver);
    std::size_t prefix_len = 4;
    std::memcpy(p, prefix, prefix_len);
    p += prefix_len;

    // Rounds (2 digits, zero-padded)
    *p++ = static_cast<char>('0' + (rounds / 10));
    *p++ = static_cast<char>('0' + (rounds % 10));
    *p++ = '$';

    // Salt (22 base64 characters)
    std::size_t encoded = base64_encode(p, salt_bytes, BCRYPT_SALT_LEN);
    p += encoded;

    return static_cast<std::size_t>(p - output);
}

bool parse_salt(
    core::string_view salt_str,
    version& ver,
    unsigned& rounds,
    std::uint8_t* salt_bytes)
{
    // Minimum: "$2a$XX$" + 22 chars = 29
    if (salt_str.size() < 29)
        return false;

    char const* s = salt_str.data();

    // Check prefix
    if (s[0] != '$' || s[1] != '2')
        return false;

    // Parse version
    if (s[2] == 'a' && s[3] == '$')
        ver = version::v2a;
    else if (s[2] == 'b' && s[3] == '$')
        ver = version::v2b;
    else if (s[2] == 'y' && s[3] == '$')
        ver = version::v2b;  // treat $2y$ as $2b$
    else
        return false;

    // Parse rounds
    if (s[4] < '0' || s[4] > '9')
        return false;
    if (s[5] < '0' || s[5] > '9')
        return false;

    rounds = static_cast<unsigned>((s[4] - '0') * 10 + (s[5] - '0'));
    if (rounds < 4 || rounds > 31)
        return false;

    if (s[6] != '$')
        return false;

    // Decode salt (22 base64 chars -> 16 bytes)
    int decoded = base64_decode(salt_bytes, s + 7, 22);
    if (decoded != 16)
        return false;

    return true;
}

void bcrypt_hash(
    char const* password,
    std::size_t password_len,
    std::uint8_t const* salt,
    unsigned rounds,
    std::uint8_t* hash)
{
    blowfish_ctx ctx;

    // Truncate password to 72 bytes (bcrypt limit)
    // Include null terminator in hash
    std::size_t key_len = std::min(password_len, std::size_t(72));

    // Create key with null terminator
    std::uint8_t key[73];
    std::memcpy(key, password, key_len);
    key[key_len] = 0;
    key_len++;

    // Initialize with default P and S boxes
    blowfish_init(ctx);

    // Expensive key setup (eksblowfish)
    blowfish_expand_key_salt(ctx, key, key_len, salt, BCRYPT_SALT_LEN);

    // 2^rounds iterations
    std::uint64_t iterations = 1ULL << rounds;
    for (std::uint64_t i = 0; i < iterations; ++i)
    {
        blowfish_expand_key(ctx, key, key_len);
        blowfish_expand_key(ctx, salt, BCRYPT_SALT_LEN);
    }

    // Encrypt magic text 64 times
    std::uint8_t ctext[24];
    std::memcpy(ctext, magic_text, 24);

    for (int i = 0; i < 64; ++i)
    {
        blowfish_encrypt_ecb(ctx, ctext, 24);
    }

    // Copy result (only 23 bytes are used in the final encoding)
    std::memcpy(hash, ctext, 24);

    // Clear sensitive data
    std::memset(&ctx, 0, sizeof(ctx));
    std::memset(key, 0, sizeof(key));
}

std::size_t format_hash(
    char* output,
    std::uint8_t const* salt_bytes,
    std::uint8_t const* hash_bytes,
    unsigned rounds,
    version ver)
{
    char* p = output;

    // Format salt portion (29 chars)
    p += format_salt(p, salt_bytes, rounds, ver);

    // Encode hash (23 bytes -> 31 base64 chars)
    // Note: bcrypt only uses 23 of the 24 hash bytes
    p += base64_encode(p, hash_bytes, 23);

    return static_cast<std::size_t>(p - output);
}

bool secure_compare(
    std::uint8_t const* a,
    std::uint8_t const* b,
    std::size_t len)
{
    volatile std::uint8_t result = 0;
    for (std::size_t i = 0; i < len; ++i)
    {
        result = static_cast<std::uint8_t>(result | (a[i] ^ b[i]));
    }
    return result == 0;
}

} // detail
} // bcrypt
} // capy
} // boost

