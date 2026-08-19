//
// Copyright (c) 2026 Steve Gerbino
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Compiled fragments shown in pages/9.design/9a.CapyLayering.adoc.

// Fragments deliberately leave results and bindings unused; the pages
// explain the values in prose instead.
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wunused-value"
#pragma GCC diagnostic ignored "-Wunused-result"
#pragma GCC diagnostic ignored "-Wunused-function"
// gcc 15 with sanitizers misattributes coroutine frame delete paths
#pragma GCC diagnostic ignored "-Wmismatched-new-delete"
#endif
#if defined(__clang__)
#pragma clang diagnostic ignored "-Wunused-lambda-capture"
#pragma clang diagnostic ignored "-Wunused-private-field"
#endif
#if defined(_MSC_VER)
#pragma warning(disable: 4834) // discarding [[nodiscard]] return value
#pragma warning(disable: 4189) // local variable initialized but not referenced
#pragma warning(disable: 4100) // unreferenced formal parameter
#pragma warning(disable: 4101) // unreferenced local variable
#pragma warning(disable: 4456) // declaration hides previous local declaration
#pragma warning(disable: 4457) // declaration hides function parameter
#pragma warning(disable: 4458) // declaration hides class member
#pragma warning(disable: 4459) // declaration hides global declaration
#endif

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

using namespace boost::capy;

// tag::any_stream_echo[]
task<> echo(any_stream& stream)
{
    char buf[1024];
    for(;;)
    {
        auto [ec, n] = co_await stream.read_some(
            make_buffer(buf));
        if(ec)
            co_return;
        co_await write(stream, const_buffer(buf, n));
    }
}
// end::any_stream_echo[]

namespace coroutine_layer {

// tag::connection_base[]
class connection_base {
public:
    task<> run();
protected:
    virtual task<> do_handshake() = 0;
    virtual task<> do_shutdown() = 0;
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
