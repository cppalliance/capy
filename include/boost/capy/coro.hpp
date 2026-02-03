//
// Copyright (c) 2025 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_CORO_HPP
#define BOOST_CAPY_CORO_HPP

#include <boost/capy/detail/config.hpp>
#include <coroutine>

#if defined(__clang__) && !defined(__apple_build_version__) && __clang_major__ >= 20
#define BOOST_CAPY_CORO_AWAIT_ELIDABLE [[clang::coro_await_elidable]]
#else
#define BOOST_CAPY_CORO_AWAIT_ELIDABLE
#endif

namespace boost {
namespace capy {

/** Alias for a type-erased coroutine handle
*/
using coro = std::coroutine_handle<>;

} // capy
} // boost

#endif
