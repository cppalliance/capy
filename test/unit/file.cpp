//
// Copyright (c) 2022 Vinnie Falco (vinnie dot falco at gmail dot com)
// Copyright (c) 2025 Mohammad Nejati
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Test that header file is self-contained.
#include <boost/capy/file.hpp>

#include <boost/system/system_error.hpp>
#include "file_test.hpp"

namespace boost {
namespace capy {

struct file_test
{
    void
    testThrowingOverloads()
    {
        // constructor
        BOOST_TEST_THROWS(
            file("missing.txt", file_mode::scan),
            system::system_error);

        file f;
        char buf[1];

        BOOST_TEST_THROWS(
            f.open("missing.txt", file_mode::scan),
            system::system_error);
        // BOOST_TEST_THROWS(
        //     f.close(),
        //     system::system_error);
        BOOST_TEST_THROWS(
            f.size(),
            system::system_error);
        BOOST_TEST_THROWS(
            f.pos(),
            system::system_error);
        BOOST_TEST_THROWS(
            f.seek(1),
            system::system_error);
        BOOST_TEST_THROWS(
            f.read(buf, 1),
            system::system_error);
        BOOST_TEST_THROWS(
            f.write(buf, 1),
            system::system_error);
    }

    void
    run()
    {
        test_file<file, true>();
        testThrowingOverloads();
    }
};

TEST_SUITE(
    file_test,
    "boost.capy.file");

} // capy
} // boost
