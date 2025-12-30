//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#include "base64.hpp"

namespace boost {
namespace capy {
namespace bcrypt {
namespace detail {

namespace {

// bcrypt's non-standard base64 alphabet
constexpr char encode_table[] =
    "./ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";

// Decode table: maps ASCII char to 6-bit value, or 0xFF for invalid
constexpr std::uint8_t decode_table[256] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,  // 0-7
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,  // 8-15
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,  // 16-23
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,  // 24-31
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,  // 32-39
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x01,  // 40-47  (. /)
    0x36, 0x37, 0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D,  // 48-55  (0-7)
    0x3E, 0x3F, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,  // 56-63  (8-9)
    0xFF, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,  // 64-71  (A-G)
    0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10,  // 72-79  (H-O)
    0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18,  // 80-87  (P-W)
    0x19, 0x1A, 0x1B, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,  // 88-95  (X-Z)
    0xFF, 0x1C, 0x1D, 0x1E, 0x1F, 0x20, 0x21, 0x22,  // 96-103 (a-g)
    0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2A,  // 104-111 (h-o)
    0x2B, 0x2C, 0x2D, 0x2E, 0x2F, 0x30, 0x31, 0x32,  // 112-119 (p-w)
    0x33, 0x34, 0x35, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,  // 120-127 (x-z)
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
};

} // namespace

std::size_t
base64_encode(
    char* dest,
    std::uint8_t const* src,
    std::size_t n)
{
    char* out = dest;

    while (n >= 3)
    {
        std::uint32_t v =
            (static_cast<std::uint32_t>(src[0]) << 16) |
            (static_cast<std::uint32_t>(src[1]) << 8) |
            static_cast<std::uint32_t>(src[2]);

        *out++ = encode_table[(v >> 18) & 0x3F];
        *out++ = encode_table[(v >> 12) & 0x3F];
        *out++ = encode_table[(v >> 6) & 0x3F];
        *out++ = encode_table[v & 0x3F];

        src += 3;
        n -= 3;
    }

    if (n == 2)
    {
        std::uint32_t v =
            (static_cast<std::uint32_t>(src[0]) << 16) |
            (static_cast<std::uint32_t>(src[1]) << 8);

        *out++ = encode_table[(v >> 18) & 0x3F];
        *out++ = encode_table[(v >> 12) & 0x3F];
        *out++ = encode_table[(v >> 6) & 0x3F];
    }
    else if (n == 1)
    {
        std::uint32_t v =
            static_cast<std::uint32_t>(src[0]) << 16;

        *out++ = encode_table[(v >> 18) & 0x3F];
        *out++ = encode_table[(v >> 12) & 0x3F];
    }

    return static_cast<std::size_t>(out - dest);
}

int
base64_decode(
    std::uint8_t* dest,
    char const* src,
    std::size_t n)
{
    std::uint8_t* out = dest;
    std::size_t i = 0;

    while (i + 4 <= n)
    {
        std::uint8_t a = decode_table[static_cast<unsigned char>(src[i])];
        std::uint8_t b = decode_table[static_cast<unsigned char>(src[i + 1])];
        std::uint8_t c = decode_table[static_cast<unsigned char>(src[i + 2])];
        std::uint8_t d = decode_table[static_cast<unsigned char>(src[i + 3])];

        if ((a | b | c | d) & 0x80)
            return -1;

        std::uint32_t v =
            (static_cast<std::uint32_t>(a) << 18) |
            (static_cast<std::uint32_t>(b) << 12) |
            (static_cast<std::uint32_t>(c) << 6) |
            static_cast<std::uint32_t>(d);

        *out++ = static_cast<std::uint8_t>(v >> 16);
        *out++ = static_cast<std::uint8_t>(v >> 8);
        *out++ = static_cast<std::uint8_t>(v);

        i += 4;
    }

    // Handle remaining 2 or 3 characters
    if (i + 3 == n)
    {
        std::uint8_t a = decode_table[static_cast<unsigned char>(src[i])];
        std::uint8_t b = decode_table[static_cast<unsigned char>(src[i + 1])];
        std::uint8_t c = decode_table[static_cast<unsigned char>(src[i + 2])];

        if ((a | b | c) & 0x80)
            return -1;

        std::uint32_t v =
            (static_cast<std::uint32_t>(a) << 18) |
            (static_cast<std::uint32_t>(b) << 12) |
            (static_cast<std::uint32_t>(c) << 6);

        *out++ = static_cast<std::uint8_t>(v >> 16);
        *out++ = static_cast<std::uint8_t>(v >> 8);
    }
    else if (i + 2 == n)
    {
        std::uint8_t a = decode_table[static_cast<unsigned char>(src[i])];
        std::uint8_t b = decode_table[static_cast<unsigned char>(src[i + 1])];

        if ((a | b) & 0x80)
            return -1;

        std::uint32_t v =
            (static_cast<std::uint32_t>(a) << 18) |
            (static_cast<std::uint32_t>(b) << 12);

        *out++ = static_cast<std::uint8_t>(v >> 16);
    }
    else if (i + 1 == n)
    {
        // Single trailing character is invalid
        return -1;
    }

    return static_cast<int>(out - dest);
}

} // detail
} // bcrypt
} // capy
} // boost

