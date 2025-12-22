//
// Copyright (c) 2022 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Test that header file is self-contained.
#include <boost/capy/detail/file_win32.hpp>

#if BOOST_CAPY_USE_WIN32_FILE

#include "test/unit/file_test.hpp"

namespace boost {
namespace capy {
namespace detail {

class file_win32_test
{
public:
    void
    run()
    {
        test_file<file_win32, true>();
    }
};

TEST_SUITE(
    file_win32_test,
    "boost.capy.detail.file_win32");

} // detail
} // capy
} // boost

#endif // BOOST_CAPY_USE_WIN32_FILE
