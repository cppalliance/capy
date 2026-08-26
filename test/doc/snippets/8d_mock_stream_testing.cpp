//
// Copyright (c) 2026 Steve Gerbino
// Copyright (c) 2026 Michael Vandeberg
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Compiled fragments shown in pages/8.examples/8d.mock-stream-testing.adoc.

#include "../doc_warnings.hpp"

#include <boost/capy/buffers.hpp>
#include <boost/capy/io/any_stream.hpp>
#include <boost/capy/task.hpp>
#include <boost/capy/test/fuse.hpp>
#include <boost/capy/test/run_blocking.hpp>
#include <boost/capy/test/stream.hpp>

#include <string>

#include "test_suite.hpp"

namespace capy = boost::capy;

namespace {

capy::task<>
read_line(capy::any_stream& stream, std::string& out)
{
    char buf[64];
    auto [ec, n] = co_await stream.read_some(capy::make_buffer(buf));
    (void)ec;
    out.assign(buf, n);
}

struct mock_stream_testing_test
{
    void
    testMockStreams()
    {
        // tag::mock_streams[]
        capy::test::fuse f;  // test::fuse
        auto [a, b] = capy::test::make_stream_pair(f);  // connected test::stream pair
        b.provide("hello\n");  // supply read input on one end
        // end::mock_streams[]

        capy::any_stream stream{&a};
        std::string received;
        capy::test::run_blocking()(read_line(stream, received));
        BOOST_TEST(received == "hello\n");
    }

    void
    testFuseArmed()
    {
        // tag::fuse_armed[]
        capy::test::fuse f;  // test::fuse
        auto r = f.armed([&](capy::test::fuse&) -> capy::task<> {
            auto [a, b] = capy::test::make_stream_pair(f);  // connected test::stream pair
            // ... run test ...
            co_return;
        });
        // end::fuse_armed[]

        BOOST_TEST(r.success);
    }

    void
    run()
    {
        testMockStreams();
        testFuseArmed();
    }
};

} // namespace

TEST_SUITE(mock_stream_testing_test, "boost.capy.doc.8d_mock_stream_testing");
