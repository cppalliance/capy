//
// Copyright (c) 2025 Mohammad Nejati
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#include <boost/capy/core/polystore.hpp>
#include <boost/capy/zlib.hpp>

#include "test_helpers.hpp"

namespace boost {
namespace capy {

struct zlib_test
{
    void
    test_error_code()
    {
        // TODO
        boost::system::error_code ec{ zlib::error::buf_err };
    }

    void
    test_deflate()
    {
        // TODO
        polystore ctx;
        zlib::install_deflate_service(ctx);
    }

    void
    test_inflate()
    {
        // TODO
        polystore ctx;
        zlib::install_inflate_service(ctx);
    }

    void
    run()
    {
        test_error_code();
    #ifdef BOOST_CAPY_HAS_ZLIB
        test_deflate();
        test_inflate();
    #endif
    }
};

TEST_SUITE(zlib_test, "boost.capy.zlib");

} // namespace capy
} // namespace boost
