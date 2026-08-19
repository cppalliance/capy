//
// Copyright (c) 2026 Michael Vandeberg
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_TEST_HPP
#define BOOST_CAPY_TEST_HPP

/** @file
    @brief Single-include header for the public capy testing toolkit.

    Including this header provides access to the complete
    @ref boost::capy::test toolkit: mock streams, the fuse fail-point
    machinery, blocking task runners, buffer inspection helpers, and
    thread naming. It is a convenience for test code, and the main
    <boost/capy.hpp> umbrella does not pull it in. Normal consumers are
    therefore never forced to depend on the testing utilities.
*/

#include <boost/capy/test/buffer_to_string.hpp>
#include <boost/capy/test/bufgrind.hpp>
#include <boost/capy/test/fuse.hpp>
#include <boost/capy/test/read_stream.hpp>
#include <boost/capy/test/run_blocking.hpp>
#include <boost/capy/test/stream.hpp>
#include <boost/capy/test/thread_name.hpp>
#include <boost/capy/test/write_stream.hpp>

#endif
