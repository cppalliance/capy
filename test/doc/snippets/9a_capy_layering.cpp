//
// Copyright (c) 2026 Steve Gerbino
// Copyright (c) 2026 Michael Vandeberg
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Compiled fragments shown in pages/9.design/9a.CapyLayering.adoc.

#include "../doc_warnings.hpp"

#include <boost/capy/buffers.hpp>
#include <boost/capy/buffers/make_buffer.hpp>
#include <boost/capy/concept/write_stream.hpp>
#include <boost/capy/io/any_stream.hpp>
#include <boost/capy/task.hpp>
#include <boost/capy/test/fuse.hpp>
#include <boost/capy/test/run_blocking.hpp>
#include <boost/capy/test/stream.hpp>
#include <boost/capy/write.hpp>

#include "test_suite.hpp"

namespace capy = boost::capy;

namespace {


// tag::any_stream_echo[]
capy::task<> echo(capy::any_stream& stream)
{
    char buf[1024];
    for(;;)
    {
        auto [ec, n] = co_await stream.read_some(
            capy::make_buffer(buf));
        if(ec)
            co_return;
        co_await capy::write(stream, capy::const_buffer(buf, n));
    }
}
// end::any_stream_echo[]

namespace coroutine_layer {

// tag::connection_base[]
class connection_base {
public:
    capy::task<> run();
protected:
    virtual capy::task<> do_handshake() = 0;
    virtual capy::task<> do_shutdown() = 0;
};
// end::connection_base[]

} // namespace coroutine_layer

struct capy_layering_test
{
    void
    testAnyStreamEcho()
    {
        capy::test::fuse f;
        auto [a, b] = capy::test::make_stream_pair(f);
        b.provide("hi");
        // Close a's read direction so echo observes eof after
        // draining the buffered bytes.
        b.close();

        capy::any_stream stream{&a};
        capy::test::run_blocking()(echo(stream));
        BOOST_TEST(b.data() == "hi");
    }

    void
    run()
    {
        testAnyStreamEcho();
    }
};

} // namespace

TEST_SUITE(capy_layering_test, "boost.capy.doc.9a_capy_layering");
