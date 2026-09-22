//
// Copyright (c) 2023 Vinnie Falco (vinnie.falco@gmail.com)
// Copyright (c) 2026 Michael Vandeberg
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_DETAIL_CONFIG_HPP
#define BOOST_CAPY_DETAIL_CONFIG_HPP

#include <cassert>
#ifndef BOOST_CAPY_ASSERT
# define BOOST_CAPY_ASSERT(expr) assert(expr)
#endif

//------------------------------------------------
//
// Compiler bug workarounds
//
//------------------------------------------------

/* Standalone workaround macro modeled on Boost.Config's BOOST_WORKAROUND.

   Guard mechanism: when a compiler symbol is not defined, the
   corresponding _WORKAROUND_GUARD macro is 1, which makes
   BOOST_CAPY_WORKAROUND evaluate to 0 on that compiler.

   BOOST_CAPY_MSVC is _MSC_VER on Microsoft's compiler only. Clang
   targeting Windows (clang-cl, and clang++ with an MSVC target)
   also defines _MSC_VER for ABI compatibility but does not share
   MSVC's codegen bugs, so an MSVC bug workaround tests
   BOOST_CAPY_MSVC rather than _MSC_VER.

   Usage:
     #if BOOST_CAPY_WORKAROUND(BOOST_CAPY_MSVC, >= 1)    // any MSVC, not Clang
     #if BOOST_CAPY_WORKAROUND(BOOST_CAPY_MSVC, < 1950)  // MSVC before 19.50
     #if BOOST_CAPY_WORKAROUND(_MSC_VER, <= 1900)        // MSVC 14.0 and earlier
     #if BOOST_CAPY_WORKAROUND(__GNUC__, < 12)           // GCC before 12
*/

#if defined(_MSC_VER) && !defined(__clang__)
# define BOOST_CAPY_MSVC _MSC_VER
# define BOOST_CAPY_MSVC_WORKAROUND_GUARD 0
#else
# define BOOST_CAPY_MSVC_WORKAROUND_GUARD 1
#endif

#ifndef _MSC_VER
# define _MSC_VER_WORKAROUND_GUARD 1
#else
# define _MSC_VER_WORKAROUND_GUARD 0
#endif

#ifndef __GNUC__
# define __GNUC___WORKAROUND_GUARD 1
#else
# define __GNUC___WORKAROUND_GUARD 0
#endif

#ifndef __clang_major__
# define __clang_major___WORKAROUND_GUARD 1
#else
# define __clang_major___WORKAROUND_GUARD 0
#endif

#define BOOST_CAPY_WORKAROUND(symbol, test)        \
    ((symbol ## _WORKAROUND_GUARD + 0 == 0) &&     \
     (symbol != 0) && (1 % (( (symbol test) ) + 1)))

/* Selects the void-returning symmetric_transfer on MSVC toolsets
   whose real symmetric transfer is miscompiled. Two unrelated
   defects, both described on detail::symmetric_transfer
   in detail/await_suspend_helper.hpp:

   - MSVC 19.34 through 19.44, every target: the returned handle
     is stored on the coroutine frame.
   - MSVC 19.50 and later, ARM64 and ARM64EC, release builds: a
     try region spanning the suspend point loses its handler.

   Not a configuration option: translation units that disagree on it
   get different definitions of symmetric_transfer. To check whether
   a newer toolset still needs it, edit this gate locally; the
   regression tests pass trivially while the workaround is on.
*/
#if BOOST_CAPY_WORKAROUND(BOOST_CAPY_MSVC, < 1950) || \
    (defined(BOOST_CAPY_MSVC) && \
     (defined(_M_ARM64) || defined(_M_ARM64EC)))
# define BOOST_CAPY_SYMMETRIC_TRANSFER_WORKAROUND 1
#else
# define BOOST_CAPY_SYMMETRIC_TRANSFER_WORKAROUND 0
#endif

// MSVC warning suppression helpers.
// On MSVC these expand to __pragma(); elsewhere they are empty.
#ifdef _MSC_VER
# define BOOST_CAPY_MSVC_WARNING_PUSH       __pragma(warning(push))
# define BOOST_CAPY_MSVC_WARNING_DISABLE(x) __pragma(warning(disable: x))
# define BOOST_CAPY_MSVC_WARNING_POP        __pragma(warning(pop))
#else
# define BOOST_CAPY_MSVC_WARNING_PUSH
# define BOOST_CAPY_MSVC_WARNING_DISABLE(x)
# define BOOST_CAPY_MSVC_WARNING_POP
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

// Force inline hint
#if defined(_MSC_VER)
# define BOOST_CAPY_FORCEINLINE __forceinline
#elif defined(__GNUC__) || defined(__clang__)
# define BOOST_CAPY_FORCEINLINE inline __attribute__((always_inline))
#else
# define BOOST_CAPY_FORCEINLINE inline
#endif

// RTTI detection (user may predefine BOOST_CAPY_NO_RTTI)
//
// _MSC_VER must be checked before __clang__ because Clang-CL defines
// both __clang__ and _MSC_VER, but uses the MSVC-style _CPPRTTI macro
// (not the GCC-style __GXX_RTTI) to signal RTTI availability.
#ifndef BOOST_CAPY_NO_RTTI
# if defined(_MSC_VER)
#  ifndef _CPPRTTI
#   define BOOST_CAPY_NO_RTTI 1
#  endif
# elif defined(__GNUC__) || defined(__clang__)
#  ifndef __GXX_RTTI
#   define BOOST_CAPY_NO_RTTI 1
#  endif
# endif
#endif

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

// Heap elision: compiler may allocate elided coroutine frames on the caller's frame
#if __has_cpp_attribute(clang::coro_await_elidable)
#define BOOST_CAPY_CORO_AWAIT_ELIDABLE [[clang::coro_await_elidable]]
#else
#define BOOST_CAPY_CORO_AWAIT_ELIDABLE
#endif

#if defined(_MSC_VER) && !defined(__clang__)
# define BOOST_CAPY_NOINLINE __declspec(noinline)
# define BOOST_CAPY_COLD
#elif defined(__GNUC__) || defined(__clang__)
# define BOOST_CAPY_NOINLINE __attribute__((noinline))
# define BOOST_CAPY_COLD __attribute__((cold))
#else
# define BOOST_CAPY_NOINLINE
# define BOOST_CAPY_COLD
#endif

namespace boost::capy::detail {
inline constexpr unsigned max_iovec_ = 16;
}

#endif
