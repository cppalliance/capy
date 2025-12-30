//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_SRC_BCRYPT_RANDOM_HPP
#define BOOST_CAPY_SRC_BCRYPT_RANDOM_HPP

#include <boost/system/error_code.hpp>
#include <cstddef>

namespace boost {
namespace capy {
namespace bcrypt {
namespace detail {

// Fill buffer with cryptographically secure random bytes.
// Throws system_error on failure.
void
fill_random(void* buf, std::size_t n);

} // detail
} // bcrypt
} // capy
} // boost

#endif

