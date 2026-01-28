//
// Copyright (c) 2019 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_DETAIL_EXCEPT_HPP
#define BOOST_CAPY_DETAIL_EXCEPT_HPP

#include <boost/capy/detail/config.hpp>
#include <boost/system/error_code.hpp>

namespace boost {
namespace capy {
namespace detail {

[[noreturn]] BOOST_CAPY_DECL void throw_bad_typeid();

[[noreturn]] BOOST_CAPY_DECL void throw_bad_alloc();

[[noreturn]] BOOST_CAPY_DECL void throw_invalid_argument();

[[noreturn]] BOOST_CAPY_DECL void throw_invalid_argument(
    char const* what);

[[noreturn]] BOOST_CAPY_DECL void throw_length_error();

[[noreturn]] BOOST_CAPY_DECL void throw_length_error(
    char const* what);

[[noreturn]] BOOST_CAPY_DECL void throw_logic_error();

[[noreturn]] BOOST_CAPY_DECL void throw_out_of_range();

[[noreturn]] BOOST_CAPY_DECL void throw_runtime_error(
    char const* what);

[[noreturn]] BOOST_CAPY_DECL void throw_system_error(
    system::error_code const& ec);

} // detail
} // capy
} // boost

#endif
