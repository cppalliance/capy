//
// Copyright (c) 2019 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#include <boost/capy/detail/config.hpp>
#include <boost/capy/detail/except.hpp>
#include <system_error>
#include <stdexcept>
#include <typeinfo>

namespace boost {
namespace capy {
namespace detail {

void
throw_bad_typeid()
{
    throw std::bad_typeid();
}

void
throw_bad_alloc()
{
    throw std::bad_alloc();
}

void
throw_invalid_argument()
{
    throw std::invalid_argument(
        "invalid argument");
}

void
throw_invalid_argument(
    char const* what)
{
    throw std::invalid_argument(what);
}

void
throw_length_error()
{
    throw std::length_error(
        "length error");
}

void
throw_length_error(
    char const* what)
{
    throw std::length_error(what);
}

void
throw_logic_error()
{
    throw std::logic_error(
        "logic error");
}

void
throw_out_of_range()
{
    throw std::out_of_range("out of range");
}

void
throw_runtime_error(
    char const* what)
{
    throw std::runtime_error(what);
}

void
throw_system_error(
    std::error_code const& ec)
{
    throw std::system_error(ec);
}

} // detail
} // capy
} // boost
