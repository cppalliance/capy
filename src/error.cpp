//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
// Copyright (c) 2026 Michael Vandeberg
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#include <boost/capy/error.hpp>
#include <boost/capy/cond.hpp>

namespace boost {
namespace capy {

namespace detail {

const char*
error_cat_type::
name() const noexcept
{
    return "boost.capy";
}

std::string
error_cat_type::
message(int code) const
{
    switch(static_cast<error>(code))
    {
    case error::eof: return "eof";
    case error::canceled: return "operation canceled";
    case error::test_failure: return "test failure";
    case error::stream_truncated: return "stream truncated";
    case error::timeout: return "timeout";
    default:
        return "unknown";
    }
}

// Map each capy error code to its canonical portable condition.
// canceled and timeout have standard equivalents, so they map to the
// generic conditions rather than capy's own cond enumerators; this is
// what lets, e.g., error::canceled compare equal to
// std::errc::operation_canceled.
std::error_condition
error_cat_type::
default_error_condition(int code) const noexcept
{
    switch(static_cast<error>(code))
    {
    case error::eof:              return make_error_condition(cond::eof);
    case error::canceled:         return std::make_error_condition(std::errc::operation_canceled);
    case error::stream_truncated: return make_error_condition(cond::stream_truncated);
    case error::timeout:          return std::make_error_condition(std::errc::timed_out);
    default:                      return std::error_condition(code, *this);
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
constinit error_cat_type error_cat;
#else
error_cat_type error_cat;
#endif

#if BOOST_CAPY_WORKAROUND(_MSC_VER, <= 1900)
BOOST_CAPY_MSVC_WARNING_POP
#endif

} // detail

} // capy
} // boost
