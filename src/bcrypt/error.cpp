//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#include <boost/capy/bcrypt/error.hpp>

namespace boost {
namespace capy {
namespace bcrypt {
namespace detail {

const char*
error_cat_type::
name() const noexcept
{
    return "boost.capy.bcrypt";
}

std::string
error_cat_type::
message(int ev) const
{
    return message(ev, nullptr, 0);
}

char const*
error_cat_type::
message(
    int ev,
    char*,
    std::size_t) const noexcept
{
    switch(static_cast<error>(ev))
    {
    case error::ok: return "success";
    case error::invalid_salt: return "invalid salt";
    case error::invalid_hash: return "invalid hash";
    default:
        return "unknown";
    }
}

#if defined(__cpp_constinit) && __cpp_constinit >= 201907L
constinit error_cat_type error_cat;
#else
error_cat_type error_cat;
#endif

} // detail
} // bcrypt
} // capy
} // boost

