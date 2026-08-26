//
// Copyright (c) 2026 Steve Gerbino
// Copyright (c) 2026 Michael Vandeberg
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Compiled fragments shown in pages/9.design/9f.WriteStream.adoc.

#include "../doc_warnings.hpp"

#include <boost/capy/buffers.hpp>
#include <boost/capy/concept/read_stream.hpp>
#include <boost/capy/concept/write_stream.hpp>
#include <boost/capy/cond.hpp>
#include <boost/capy/io/write_now.hpp>
#include <boost/capy/io_task.hpp>
#include <boost/capy/task.hpp>
#include <boost/capy/test/read_stream.hpp>
#include <boost/capy/test/run_blocking.hpp>
#include <boost/capy/test/write_stream.hpp>
#include <boost/capy/write.hpp>

#include <concepts>
#include <cstddef>
#include <system_error>

#include "test_suite.hpp"

namespace capy = boost::capy;

namespace {


static_assert(capy::WriteStream<capy::test::write_stream>);
static_assert(!capy::WriteStream<capy::test::read_stream>);

// The real algorithms live in <boost/capy/write.hpp> and
// <boost/capy/io/write_now.hpp>; these sketches mirror the interface
// the page presents and are checked against the real API below.
namespace api_sketch {

// tag::write_signature[]
auto write(capy::WriteStream auto& stream,
           capy::ConstBufferSequence auto buffers)
    -> capy::io_task<std::size_t>;
// end::write_signature[]

// tag::write_now_sketch[]
template<capy::WriteStream Stream>
class write_now
{
public:
    explicit write_now(Stream& s) noexcept;

    capy::IoAwaitable auto operator()(capy::ConstBufferSequence auto buffers);
};
// end::write_now_sketch[]

} // namespace api_sketch

static_assert(requires(capy::test::write_stream& s, capy::const_buffer b) {
    { capy::write(s, b) } -> std::same_as<capy::io_task<std::size_t>>;
});
static_assert(
    std::constructible_from<
        capy::write_now<capy::test::write_stream>, capy::test::write_stream&>);

// tag::relay_with_write_now[]
template<capy::ReadStream Source, capy::WriteStream Stream>
capy::task<> relay_with_write_now(Source& src, Stream& dest)
{
    char buf[65536];
    capy::write_now wn(dest);

    for(;;)
    {
        // Read a chunk from the source
        auto [rec, nr] = co_await src.read_some(
            capy::mutable_buffer(buf, sizeof(buf)));
        if(rec == capy::cond::eof && nr == 0)
            co_return;

        // write_now drains the chunk to completion.
        // If the kernel accepts 40KB of 64KB, write_now
        // internally calls write_some(24KB) for the
        // remainder -- a small write that wastes a
        // syscall. The caller cannot top up between
        // write_now's internal iterations.
        auto [wec, nw] = co_await wn(
            capy::const_buffer(buf, nr));
        if(wec)
            co_return;

        if(rec == capy::cond::eof)
            co_return;
    }
}
// end::relay_with_write_now[]

struct write_stream_test
{
    void
    testRelayWithWriteNow()
    {
        capy::test::fuse f;
        capy::test::read_stream src(f);
        src.provide("relayed data");
        capy::test::write_stream dest(f);
        capy::test::run_blocking()(relay_with_write_now(src, dest));
        BOOST_TEST_EQ(dest.data(), "relayed data");
    }

    void
    testRelayChunked()
    {
        // Partial writes force write_now's internal drain loop
        capy::test::fuse f;
        capy::test::read_stream src(f);
        src.provide("relayed data");
        capy::test::write_stream dest(f, 3);
        capy::test::run_blocking()(relay_with_write_now(src, dest));
        BOOST_TEST_EQ(dest.data(), "relayed data");
    }

    void
    run()
    {
        testRelayWithWriteNow();
        testRelayChunked();
    }
};

} // namespace

TEST_SUITE(write_stream_test, "boost.capy.doc.9f_write_stream");
