//
// Copyright (c) 2019 Vinnie Falco (vinnie.falco@gmail.com)
// Copyright (c) 2022 Alan de Freitas (alandefreitas@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#include "test_suite.hpp"

#if defined(_MSC_VER) && !defined(__clang__)
#include <crtdbg.h>
#endif

int
main(int argc, char const* const* argv)
{
#if defined(_MSC_VER) && !defined(__clang__)
    // Enable memory leak checking in debug mode on MSVC (not Clang)
    int flags = _CrtSetDbgFlag(_CRTDBG_REPORT_FLAG);
    flags |= _CRTDBG_LEAK_CHECK_DF;
    _CrtSetDbgFlag(flags);
#endif
    return ::test_suite::run(argc, argv);
}
