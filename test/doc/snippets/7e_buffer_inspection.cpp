//
// Copyright (c) 2026 Steve Gerbino
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Compiled fragments shown in pages/7.testing/7e.buffer-inspection.adoc.

// The all-splits fragment binds both halves without using them; the
// page comment explains the loop body instead.

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

#include <boost/capy/buffers/make_buffer.hpp>
#include <boost/capy/task.hpp>
#include <boost/capy/test/buffer_to_string.hpp>
#include <boost/capy/test/bufgrind.hpp>
#include <boost/capy/test/fuse.hpp>
#include <boost/capy/test/read_stream.hpp>

#include <array>
#include <string>
#include <string_view>

#include "test_suite.hpp"

namespace {

// The #include directives inside the tags expand to nothing here (the
// headers are already included above); they are kept for the page text.

// tag::all_splits[]
#include <boost/capy/test/bufgrind.hpp>
#include <boost/capy/test/buffer_to_string.hpp>
#include <boost/capy/test/fuse.hpp>
#include <boost/capy/buffers/make_buffer.hpp>
#include <boost/capy/task.hpp>

using namespace boost::capy;
using namespace boost::capy::test;

void test_all_splits()
{
    std::string data = "hello";
    auto cb = make_buffer(data);

    fuse f;
    auto r = f.inert([&](fuse&) -> task<> {
        bufgrind bg(cb);
        while(bg)
        {
            auto [b1, b2] = co_await bg.next();
            BOOST_TEST_EQ(buffer_to_string(b1, b2), data);
        }
    });
    BOOST_TEST(r.success);
}
// end::all_splits[]

void test_step_size()
{
    fuse f;
    auto r = f.inert([&](fuse&) -> task<> {
        // tag::step_size[]
        std::string data = "0123456789";  // 10 bytes
        auto cb = make_buffer(data);

        bufgrind bg(cb, 3);
        // Visits positions: 0, 3, 6, 9, 10
        while(bg)
        {
            auto [b1, b2] = co_await bg.next();
            // exercise parser at each split point
        }
        // end::step_size[]
    });
    BOOST_TEST(r.success);
}

void test_mutability()
{
    fuse f;
    auto r = f.inert([&](fuse&) -> task<> {
        // tag::mutability[]
        char data[] = "hello";
        mutable_buffer mb(data, 5);

        bufgrind bg(mb);
        while(bg)
        {
            auto [b1, b2] = co_await bg.next();
            // each half is itself a buffer sequence; a mutable input
            // yields MutableBufferSequence halves callers may write into
            static_assert(MutableBufferSequence<decltype(b1)>);
        }
        // end::mutability[]
    });
    BOOST_TEST(r.success);
}

// tag::buffer_to_string[]
#include <boost/capy/test/buffer_to_string.hpp>
#include <boost/capy/buffers/make_buffer.hpp>

using namespace boost::capy;
using namespace boost::capy::test;

void test_buffer_to_string()
{
    // Single buffer sequence
    const_buffer cb(make_buffer(std::string_view("hello")));
    BOOST_TEST_EQ(buffer_to_string(cb), "hello");

    // Multiple buffer sequences concatenated in order
    const_buffer b1(make_buffer(std::string_view("hello")));
    const_buffer b2(make_buffer(std::string_view(" world")));
    BOOST_TEST_EQ(buffer_to_string(b1, b2), "hello world");
}
// end::buffer_to_string[]

void test_reconstruct()
{
    // tag::reconstruct[]
    std::string original = "hello world";
    auto cb = make_buffer(original);

    fuse f;
    auto r = f.inert([&](fuse&) -> task<> {
        bufgrind bg(cb);
        while(bg)
        {
            auto [b1, b2] = co_await bg.next();
            BOOST_TEST_EQ(buffer_to_string(b1, b2), original);
        }
    });
    BOOST_TEST(r.success);
    // end::reconstruct[]
}

// tag::parser_all_splits[]
#include <boost/capy/test/bufgrind.hpp>
#include <boost/capy/test/buffer_to_string.hpp>
#include <boost/capy/test/fuse.hpp>
#include <boost/capy/test/read_stream.hpp>
#include <boost/capy/buffers/make_buffer.hpp>
#include <boost/capy/task.hpp>

#include <array>
#include <string>

using namespace boost::capy;
using namespace boost::capy::test;

// Hypothetical parser: reads all bytes from a ReadStream
task<std::string> read_all(read_stream& rs)
{
    std::string out;
    std::array<char, 64> buf;
    for(;;)
    {
        auto [ec, n] = co_await rs.read_some(make_buffer(buf));
        if(ec)
            co_return out;
        out.append(buf.data(), n);
    }
}

void test_parser_all_splits()
{
    std::string input = "GET / HTTP/1.1\r\n";
    auto cb = make_buffer(input);

    fuse f;
    auto r = f.inert([&](fuse&) -> task<> {
        bufgrind bg(cb);
        while(bg)
        {
            auto [b1, b2] = co_await bg.next();

            // Feed the split as two discrete reads
            read_stream rs(f);
            rs.provide(buffer_to_string(b1));
            rs.provide(buffer_to_string(b2));

            std::string got = co_await read_all(rs);
            BOOST_TEST_EQ(got, input);
        }
    });
    BOOST_TEST(r.success);
}
// end::parser_all_splits[]

struct buffer_inspection_test
{
    void
    run()
    {
        test_all_splits();
        test_step_size();
        test_mutability();
        test_buffer_to_string();
        test_reconstruct();
        test_parser_all_splits();
    }
};

} // namespace

TEST_SUITE(buffer_inspection_test, "boost.capy.doc.7e_buffer_inspection");
