//
// Copyright (c) 2026 Michael Vandeberg
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_DETAIL_STOP_REQUESTED_EXCEPTION_HPP
#define BOOST_CAPY_DETAIL_STOP_REQUESTED_EXCEPTION_HPP

namespace boost {
namespace capy {
namespace detail {

/* Lightweight sentinel thrown inside quitter<T> when the stop token
   is triggered. Not derived from std::exception. Never escapes the
   coroutine — unhandled_exception() catches it and sets the stopped
   flag. The cost is one throw+catch per cancellation per coroutine
   lifetime. */
struct stop_requested_exception {};

} // namespace detail
} // namespace capy
} // namespace boost

#endif
