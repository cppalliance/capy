//
// Copyright (c) 2026 Michael Vandeberg
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#include <boost/capy/test/thread_name.hpp>

#if defined(_WIN32)

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <string>

#elif defined(__APPLE__)

#include <pthread.h>
#include <cstring>

#elif defined(__linux__) || defined(__FreeBSD__) || defined(__NetBSD__)

#include <pthread.h>
#include <cstring>

#endif

/*
    Platform-specific thread naming implementation.

    Each platform has a different API and name length limit:
    - Windows: SetThreadDescription with UTF-8 to UTF-16 conversion (no limit)
    - macOS: pthread_setname_np(name) with 63-char limit
    - Linux/BSD: pthread_setname_np(thread, name) with 15-char limit

    All operations are best-effort and silently fail on error, since thread
    naming is purely for debugging visibility and should never affect program
    correctness. The noexcept guarantee is maintained by catching exceptions
    from std::wstring allocation on Windows.
*/

namespace boost {
namespace capy {

void
set_current_thread_name(char const* name) noexcept
{
#if defined(_WIN32)
    // SetThreadDescription requires Windows 10 1607+. Older Windows versions
    // are unsupported; the program may fail to link on those systems.

    // Query required buffer size for UTF-8 to wide conversion.
    int required = MultiByteToWideChar(CP_UTF8, 0, name, -1, nullptr, 0);
    if(required <= 0)
        return;

    // Allocate and convert; catch exceptions to maintain noexcept.
    std::wstring wname;
    try
    {
        wname.resize(static_cast<std::size_t>(required));
    }
    catch(...)
    {
        return;
    }

    if(MultiByteToWideChar(CP_UTF8, 0, name, -1, wname.data(), required) <= 0)
        return;

    // Ignore return value: thread naming is best-effort for debugging.
    (void)SetThreadDescription(GetCurrentThread(), wname.c_str());
#elif defined(__APPLE__)
    // macOS pthread_setname_np takes only the name (no thread handle)
    // and has a 64 char limit (63 + null terminator)
    char truncated[64];
    std::strncpy(truncated, name, 63);
    truncated[63] = '\0';

    // Ignore return value: thread naming is best-effort for debugging.
    (void)pthread_setname_np(truncated);
#elif defined(__linux__) || defined(__FreeBSD__) || defined(__NetBSD__)
    // pthread_setname_np has 16 char limit (15 + null terminator)
    char truncated[16];
    std::strncpy(truncated, name, 15);
    truncated[15] = '\0';

    // Ignore return value: thread naming is best-effort for debugging.
    (void)pthread_setname_np(pthread_self(), truncated);
#else
    (void)name;
#endif
}

} // capy
} // boost
