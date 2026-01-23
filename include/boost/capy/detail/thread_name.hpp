//
// Copyright (c) 2026 Michael Vandeberg
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_DETAIL_THREAD_NAME_HPP
#define BOOST_CAPY_DETAIL_THREAD_NAME_HPP

#include <boost/capy/detail/config.hpp>

/*
    Thread naming abstraction for debugging purposes.

    Sets the current thread's name which appears in debuggers
    (GDB, LLDB, Visual Studio) and system tools (htop, Process Explorer).

    Platform support:
    - Windows: SetThreadDescription (Windows 10 1607+)
    - macOS: pthread_setname_np (truncated to 63 chars)
    - Linux/FreeBSD/NetBSD: pthread_setname_np (truncated to 15 chars)
    - Other platforms: no-op
*/

namespace boost {
namespace capy {
namespace detail {

/** Set the name of the current thread for debugging purposes.

    The name may be truncated to platform limits:
    - Linux/FreeBSD/NetBSD: 15 characters
    - macOS: 63 characters
    - Windows: no practical limit

    @param name The thread name to set (UTF-8 encoded on Windows).
*/
BOOST_CAPY_DECL
void
set_current_thread_name(char const* name) noexcept;

} // detail
} // capy
} // boost

#endif
