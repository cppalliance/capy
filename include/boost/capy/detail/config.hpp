//
// Copyright (c) 2023 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_DETAIL_CONFIG_HPP
#define BOOST_CAPY_DETAIL_CONFIG_HPP

#ifndef BOOST_CAPY_ASSERT
# define BOOST_CAPY_ASSERT(expr) assert(expr)
#endif

// Efficient thread-local storage keyword for POD types
#if !defined(BOOST_CAPY_TLS_KEYWORD)
# if defined(_MSC_VER)
#  define BOOST_CAPY_TLS_KEYWORD __declspec(thread)
# elif defined(__GNUC__) || defined(__clang__)
#  define BOOST_CAPY_TLS_KEYWORD __thread
# endif
#endif

// Symbol export/import for shared libraries
#if defined(_WIN32) || defined(__CYGWIN__)
# define BOOST_CAPY_SYMBOL_EXPORT __declspec(dllexport)
# define BOOST_CAPY_SYMBOL_IMPORT __declspec(dllimport)
# define BOOST_CAPY_SYMBOL_VISIBLE
#elif defined(__GNUC__) && __GNUC__ >= 4
# define BOOST_CAPY_SYMBOL_EXPORT __attribute__((visibility("default")))
# define BOOST_CAPY_SYMBOL_IMPORT __attribute__((visibility("default")))
# define BOOST_CAPY_SYMBOL_VISIBLE __attribute__((visibility("default")))
#else
# define BOOST_CAPY_SYMBOL_EXPORT
# define BOOST_CAPY_SYMBOL_IMPORT
# define BOOST_CAPY_SYMBOL_VISIBLE
#endif

namespace boost {
namespace capy {

//------------------------------------------------

#if (defined(BOOST_CAPY_DYN_LINK) || defined(BOOST_ALL_DYN_LINK)) && !defined(BOOST_CAPY_STATIC_LINK)
# if defined(BOOST_CAPY_SOURCE)
#  define BOOST_CAPY_DECL        BOOST_CAPY_SYMBOL_EXPORT
#  define BOOST_CAPY_BUILD_DLL
# else
#  define BOOST_CAPY_DECL        BOOST_CAPY_SYMBOL_IMPORT
# endif
#endif // shared lib

#ifndef  BOOST_CAPY_DECL
# define BOOST_CAPY_DECL
#endif

namespace detail {
inline constexpr unsigned max_iovec_ = 16;
} // detail

} // capy
} // boost

#endif
