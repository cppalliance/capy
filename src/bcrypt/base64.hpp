//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_SRC_BCRYPT_BASE64_HPP
#define BOOST_CAPY_SRC_BCRYPT_BASE64_HPP

#include <cstddef>
#include <cstdint>

namespace boost {
namespace capy {
namespace bcrypt {
namespace detail {

// bcrypt uses a non-standard base64 alphabet:
// ./ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789

// Encode binary data to bcrypt base64.
// Returns number of characters written to dest.
// dest must have space for ((n * 4) + 2) / 3 characters.
std::size_t
base64_encode(
    char* dest,
    std::uint8_t const* src,
    std::size_t n);

// Decode bcrypt base64 to binary.
// Returns number of bytes written to dest, or -1 on error.
// dest must have space for (n * 3) / 4 bytes.
int
base64_decode(
    std::uint8_t* dest,
    char const* src,
    std::size_t n);

} // detail
} // bcrypt
} // capy
} // boost

#endif

