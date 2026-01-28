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

#include <boost/config.hpp>

#include <cassert>
#include <cstddef>

#ifndef BOOST_CAPY_ASSERT
# define BOOST_CAPY_ASSERT(expr) assert(expr)
#endif

#if __has_include(<version>)
# include <version>
#endif

// Detect thread-local storage mechanism
// Cascade: compiler keyword > thread_local > OS API
#if !defined(BOOST_CAPY_TLS_KEYWORD)
# if defined(_MSC_VER)
#  define BOOST_CAPY_TLS_KEYWORD __declspec(thread)
# elif defined(__GNUC__) || defined(__clang__)
#  define BOOST_CAPY_TLS_KEYWORD __thread
# endif
#endif

#if !defined(BOOST_CAPY_HAS_THREAD_LOCAL)
# if defined(_MSC_VER) && _MSC_VER >= 1900
#  define BOOST_CAPY_HAS_THREAD_LOCAL 1
# elif defined(__has_feature)
#  if __has_feature(cxx_thread_local)
#   define BOOST_CAPY_HAS_THREAD_LOCAL 1
#  elif defined(__GNUC__) && __GNUC__ >= 5
#   define BOOST_CAPY_HAS_THREAD_LOCAL 1
#  else
#   define BOOST_CAPY_HAS_THREAD_LOCAL 0
#  endif
# elif defined(__GNUC__) && __GNUC__ >= 5
#  define BOOST_CAPY_HAS_THREAD_LOCAL 1
# else
#  define BOOST_CAPY_HAS_THREAD_LOCAL 0
# endif
#endif

namespace boost {
namespace capy {

//------------------------------------------------

# if (defined(BOOST_CAPY_DYN_LINK) || defined(BOOST_ALL_DYN_LINK)) && !defined(BOOST_CAPY_STATIC_LINK)
#  if defined(BOOST_CAPY_SOURCE)
#   define BOOST_CAPY_DECL        BOOST_SYMBOL_EXPORT
#   define BOOST_CAPY_BUILD_DLL
#  else
#   define BOOST_CAPY_DECL        BOOST_SYMBOL_IMPORT
#  endif
# endif // shared lib

# ifndef  BOOST_CAPY_DECL
#  define BOOST_CAPY_DECL
# endif

# if !defined(BOOST_CAPY_SOURCE) && !defined(BOOST_ALL_NO_LIB) && !defined(BOOST_CAPY_NO_LIB)
#  define BOOST_LIB_NAME boost_capy
#  if defined(BOOST_ALL_DYN_LINK) || defined(BOOST_CAPY_DYN_LINK)
#   define BOOST_DYN_LINK
#  endif
#  include <boost/config/auto_link.hpp>
# endif

//------------------------------------------------

#if defined(BOOST_NO_CXX14_AGGREGATE_NSDMI) || defined(BOOST_MSVC)
# define BOOST_CAPY_AGGREGATE_WORKAROUND
#endif

// Add source location to error codes
#ifdef BOOST_CAPY_NO_SOURCE_LOCATION
# define BOOST_CAPY_ERR(ev) (::boost::system::error_code(ev))
# define BOOST_CAPY_RETURN_EC(ev) return (ev)
#else
# define BOOST_CAPY_ERR(ev) ( \
    ::boost::system::error_code( (ev), [] { \
    static constexpr auto loc((BOOST_CURRENT_LOCATION)); \
    return &loc; }()))
# define BOOST_CAPY_RETURN_EC(ev) \
    static constexpr auto loc ## __LINE__((BOOST_CURRENT_LOCATION)); \
    return ::boost::system::error_code((ev), &loc ## __LINE__)
#endif

namespace detail {
inline constexpr std::size_t max_iovec_ = 16;
} // detail

} // capy
} // boost

#endif
