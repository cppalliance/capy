//
// Copyright (c) 2026 Steve Gerbino
// Copyright (c) 2026 Michael Vandeberg
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Compiled fragments shown in pages/7.testing/7e.buffer-inspection.adoc.

// The all-splits fragment binds both halves without using them; the
// page comment explains the loop body instead.

#include "../doc_warnings.hpp"

// GCC gives false positive -Wmaybe-uninitialized on structured bindings
// via the tuple protocol inside coroutine frames.
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif

#include <boost/capy/buffers/make_buffer.hpp>
#include <boost/capy/concept/read_stream.hpp>
#include <boost/capy/cond.hpp>
#include <boost/capy/results.hpp>
#include <boost/capy/task.hpp>
#include <boost/capy/test/buffer_to_string.hpp>
#include <boost/capy/test/bufgrind.hpp>
#include <boost/capy/test/fuse.hpp>
#include <boost/capy/test/stream.hpp>
#include <boost/capy/when_all.hpp>
#include <boost/capy/write.hpp>

#include <array>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include "test_suite.hpp"

namespace {

// The #include directives inside the tags expand to nothing here (the
// headers are already included above); they are kept for the page text.

// tag::all_splits[]
#include <boost/capy/test/bufgrind.hpp>
#include <boost/capy/test/buffer_to_string.hpp>
#include <boost/capy/test/fuse.hpp>
#include <boost/capy/buffers/make_buffer.hpp>

namespace capy = boost::capy;

void test_all_splits()
{
    std::string data = "hello";
    auto cb = capy::make_buffer(data);

    capy::test::fuse f;
    auto r = f.inert([&](capy::test::fuse&) -> capy::task<> {
        capy::test::bufgrind bg(cb);
        while(bg)
        {
            auto [b1, b2] = co_await bg.next();
            BOOST_TEST_EQ(capy::test::buffer_to_string(b1, b2), data);
        }
    });
    BOOST_TEST(r.success);
}
// end::all_splits[]

void test_step_size()
{
    capy::test::fuse f;
    auto r = f.inert([&](capy::test::fuse&) -> capy::task<> {
        // tag::step_size[]
        std::string data = "0123456789";  // 10 bytes
        auto cb = capy::make_buffer(data);

        capy::test::bufgrind bg(cb, 3);
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
    capy::test::fuse f;
    auto r = f.inert([&](capy::test::fuse&) -> capy::task<> {
        // tag::mutability[]
        char data[] = "hello";
        capy::mutable_buffer mb(data, 5);

        capy::test::bufgrind bg(mb);
        while(bg)
        {
            auto [b1, b2] = co_await bg.next();
            // each half is itself a buffer sequence; a mutable input
            // yields MutableBufferSequence halves callers may write into
            static_assert(capy::MutableBufferSequence<decltype(b1)>);
        }
        // end::mutability[]
    });
    BOOST_TEST(r.success);
}

// tag::buffer_to_string[]
#include <boost/capy/test/buffer_to_string.hpp>
#include <boost/capy/buffers/make_buffer.hpp>

namespace capy = boost::capy;

void test_buffer_to_string()
{
    // Single buffer sequence
    capy::const_buffer cb(capy::make_buffer(std::string_view("hello")));
    BOOST_TEST_EQ(capy::test::buffer_to_string(cb), "hello");

    // Multiple buffer sequences concatenated in order
    capy::const_buffer b1(capy::make_buffer(std::string_view("hello")));
    capy::const_buffer b2(capy::make_buffer(std::string_view(" world")));
    BOOST_TEST_EQ(capy::test::buffer_to_string(b1, b2), "hello world");
}
// end::buffer_to_string[]

void test_reconstruct()
{
    // tag::reconstruct[]
    std::string original = "hello world";
    auto cb = capy::make_buffer(original);

    capy::test::fuse f;
    auto r = f.inert([&](capy::test::fuse&) -> capy::task<> {
        capy::test::bufgrind bg(cb);
        while(bg)
        {
            auto [b1, b2] = co_await bg.next();
            BOOST_TEST_EQ(capy::test::buffer_to_string(b1, b2), original);
        }
    });
    BOOST_TEST(r.success);
    // end::reconstruct[]
}

// tag::parser_all_splits[]
#include <boost/capy/test/bufgrind.hpp>
#include <boost/capy/test/buffer_to_string.hpp>
#include <boost/capy/test/fuse.hpp>
#include <boost/capy/test/stream.hpp>
#include <boost/capy/buffers/make_buffer.hpp>
#include <boost/capy/concept/read_stream.hpp>
#include <boost/capy/cond.hpp>
#include <boost/capy/results.hpp>
#include <boost/capy/task.hpp>
#include <boost/capy/when_all.hpp>
#include <boost/capy/write.hpp>

#include <array>
#include <string>
#include <system_error>
#include <utility>

namespace capy = boost::capy;

// Hypothetical parser: reads all bytes from a ReadStream
template<capy::ReadStream Stream>
capy::task<capy::results<std::string>> read_all(Stream& stream)
{
    std::string out;
    std::array<char, 64> buf;
    for(;;)
    {
        auto [ec, n] = co_await stream.read_some(capy::make_buffer(buf));
        out.append(buf.data(), n);
        if(ec == capy::cond::eof)
            co_return capy::results<std::string>{std::error_code(), std::move(out)};
        if(ec)
            co_return capy::results<std::string>{ec, std::move(out)};
    }
}

// Sends one piece to the peer, closing the stream after the last piece
capy::task<capy::results<>> send_piece(
    capy::test::stream& s, std::string piece, bool last)
{
    auto [ec, _] = co_await capy::write(s, capy::make_buffer(piece));
    if(last)
        s.close();
    co_return capy::results<>{ec};
}

void test_parser_all_splits()
{
    std::string input = "GET / HTTP/1.1\r\n";
    auto cb = capy::make_buffer(input);

    capy::test::fuse f;
    auto r = f.inert([&](capy::test::fuse&) -> capy::task<> {
        capy::test::bufgrind bg(cb);
        while(bg)
        {
            auto [b1, b2] = co_await bg.next();
            // A named pair, because GCC 15 can skip destroying a
            // structured binding's object in a coroutine loop
            auto pair = capy::test::make_stream_pair(f);
            auto& [a, b] = pair;

            // when_all posts its children in argument order and
            // run_blocking runs them in turn: b1 arrives, the parser
            // reads it, and only then does b2 arrive.
            auto [ec, sent1, got, sent2] = co_await capy::when_all(
                send_piece(a, capy::test::buffer_to_string(b1), false),
                read_all(b),
                send_piece(a, capy::test::buffer_to_string(b2), true));

            BOOST_TEST(!ec);
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
