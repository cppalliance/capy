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
    switch(static_cast<cond>(code))
    {
    case cond::eof: return "end of file";
    case cond::canceled: return "operation canceled";
    case cond::stream_truncated: return "stream truncated";
    case cond::timeout: return "operation timed out";
    default:
        return "unknown";
    }
}

bool
cond_cat_type::
equivalent(
    std::error_code const& ec,
    int condition) const noexcept
{
    switch(static_cast<cond>(condition))
    {
    case cond::eof:
        return ec == capy::error::eof;

    case cond::canceled:
        if(ec == capy::error::canceled)
            return true;
        if(ec == std::errc::operation_canceled)
            return true;
        return false;

    case cond::stream_truncated:
        return ec == capy::error::stream_truncated;

    case cond::timeout:
        if(ec == capy::error::timeout)
            return true;
        if(ec == std::errc::timed_out)
            return true;
        return false;

    default:
        return false;
    }
}

//-----------------------------------------------

// msvc 14.0 has a bug that warns about inability
// to use constexpr construction here, even though
// there's no constexpr construction
#if BOOST_CAPY_WORKAROUND(_MSC_VER, <= 1900)
BOOST_CAPY_MSVC_WARNING_PUSH
BOOST_CAPY_MSVC_WARNING_DISABLE(4592)
#endif

#if defined(__cpp_constinit) && __cpp_constinit >= 201907L
constinit cond_cat_type cond_cat;
#else
cond_cat_type cond_cat;
#endif

#if BOOST_CAPY_WORKAROUND(_MSC_VER, <= 1900)
BOOST_CAPY_MSVC_WARNING_POP
#endif

} // detail

} // capy
} // boost
