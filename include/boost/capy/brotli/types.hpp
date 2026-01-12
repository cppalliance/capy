//
// Copyright (c) 2025 Mohammad Nejati
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_BROTLI_TYPES_HPP
#define BOOST_CAPY_BROTLI_TYPES_HPP

#include <boost/capy/detail/config.hpp>

#include <cstddef>

namespace boost {
namespace capy {
namespace brotli {

/** Allocating function pointer type. */
using alloc_func = void* (*)(void* opaque, std::size_t size);

/** Deallocating function pointer type. */
using free_func = void (*)(void* opaque, void* address);

} // brotli
} // capy
} // boost

#endif
