//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#include <boost/capy/cond.hpp>
#include <boost/capy/error.hpp>
#include <boost/system/errc.hpp>
#include <system_error>

namespace boost {
namespace capy {

namespace detail {

const char*
cond_cat_type::
name() const noexcept
{
    return "boost.capy";
}

std::string
cond_cat_type::
message(int code) const
{
    return message(code, nullptr, 0);
}

char const*
cond_cat_type::
message(
    int code,
    char*,
    std::size_t) const noexcept
{
    switch(static_cast<cond>(code))
    {
    case cond::eof: return "end of file";
    case cond::canceled: return "operation canceled";
    case cond::stream_truncated: return "stream truncated";
    default:
        return "unknown";
    }
}

bool
cond_cat_type::
equivalent(
    system::error_code const& ec,
    int condition) const noexcept
{
    switch(static_cast<cond>(condition))
    {
    case cond::eof:
        return ec == capy::error::eof;

    case cond::canceled:
        // Check capy::error::canceled
        if(ec == capy::error::canceled)
            return true;
        // Check boost::system::errc
        if(ec == boost::system::errc::operation_canceled)
            return true;
        // Check std::errc
        if(ec == std::errc::operation_canceled)
            return true;
        return false;

    case cond::stream_truncated:
        return ec == capy::error::stream_truncated;

    default:
        return false;
    }
}

//-----------------------------------------------

// msvc 14.0 has a bug that warns about inability
// to use constexpr construction here, even though
// there's no constexpr construction
#if defined(_MSC_VER) && _MSC_VER <= 1900
# pragma warning( push )
# pragma warning( disable : 4592 )
#endif

#if defined(__cpp_constinit) && __cpp_constinit >= 201907L
constinit cond_cat_type cond_cat;
#else
cond_cat_type cond_cat;
#endif

#if defined(_MSC_VER) && _MSC_VER <= 1900
# pragma warning( pop )
#endif

} // detail

} // capy
} // boost
