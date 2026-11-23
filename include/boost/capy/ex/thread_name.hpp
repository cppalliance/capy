//
// Copyright (c) 2026 Michael Vandeberg
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_EX_THREAD_NAME_HPP
#define BOOST_CAPY_EX_THREAD_NAME_HPP

#include <boost/capy/detail/config.hpp>

namespace boost {
namespace capy {

/** Set the name of the calling thread.

    Debuggers (GDB, LLDB, Visual Studio), core dumps, and system tools
    such as `htop` show the name in place of a numeric thread ID. An
    execution context that owns worker threads can name them so a
    stack trace identifies which context a thread belongs to;
    @ref thread_pool names its workers this way.

    The name is truncated to the platform limit: 15 characters on
    Linux and FreeBSD, 63 on macOS, and `PTHREAD_MAX_NAMELEN_NP - 1`
    on NetBSD. Windows has no practical limit. On other platforms the
    call does nothing.

    Naming is best-effort: if the platform rejects the name, the
    thread keeps its current name and no error is reported.

    @par Thread Safety
    Affects only the calling thread.

    @param name The name to set, as a null-terminated UTF-8 string.
*/
BOOST_CAPY_DECL
void
set_current_thread_name(char const* name) noexcept;

} // capy
} // boost

#endif
