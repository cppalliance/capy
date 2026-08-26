//
// Copyright (c) 2026 Steve Gerbino
// Copyright (c) 2026 Michael Vandeberg
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Compiled fragments shown in pages/5.buffers/5c.sequences.adoc.

#include "../doc_warnings.hpp"

#include <boost/capy/buffers.hpp>
// tag::buffer_slice_include[]
#include <boost/capy/buffers/buffer_slice.hpp>
// end::buffer_slice_include[]
// tag::consuming_buffers_include[]
#include <boost/capy/buffers/consuming_buffers.hpp>
// end::consuming_buffers_include[]

#include <boost/capy/buffers/make_buffer.hpp>
#include <boost/capy/task.hpp>
#include <boost/capy/test/run_blocking.hpp>
#include <boost/capy/test/stream.hpp>
#include <boost/capy/write.hpp>

#include <array>
#include <cstddef>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include "test_suite.hpp"

// The iteration fragment binds each buffer without using it; the page
// comment explains the loop body instead. The slice fragment discards
// io_result values the same way.

namespace capy = boost::capy;

namespace {

static_assert(capy::ConstBufferSequence<capy::const_buffer>);
static_assert(capy::ConstBufferSequence<std::vector<capy::const_buffer>>);
static_assert(!capy::ConstBufferSequence<int>);
static_assert(capy::MutableBufferSequence<capy::mutable_buffer>);
static_assert(!capy::MutableBufferSequence<capy::const_buffer>);

// tag::send_signature[]
template<capy::ConstBufferSequence Buffers>
void send(Buffers const& bufs);
// end::send_signature[]

// Logs the element count of every call so all four calls are observable.
std::vector<std::size_t> send_lengths;

template<capy::ConstBufferSequence Buffers>
void send(Buffers const& bufs)
{
    send_lengths.push_back(capy::buffer_length(bufs));
}

// The custom type used by the heterogeneous-composition fragment.
struct chained_buffers
{
    std::array<capy::const_buffer, 3> parts;
    auto begin() const noexcept { return parts.begin(); }
    auto end() const noexcept { return parts.end(); }
};

// tag::iterate[]
template<capy::ConstBufferSequence Buffers>
void process(Buffers const& bufs)
{
    for (auto it = capy::begin(bufs); it != capy::end(bufs); ++it)
    {
        capy::const_buffer buf = *it;
        // Process buf.data(), buf.size()
    }
}
// end::iterate[]

using Stream = capy::test::stream;

// tag::read_all[]
template<capy::MutableBufferSequence Buffers>
capy::task<std::size_t> read_all(Stream& stream, Buffers buffers)
{
    capy::consuming_buffers consuming(buffers);
    std::size_t const total_size = capy::buffer_size(buffers);
    std::size_t total = 0;

    while (total < total_size)
    {
        auto [ec, n] = co_await stream.read_some(consuming.data());
        consuming.consume(n);
        total += n;
        if (ec)
            break;
    }

    co_return total;
}
// end::read_all[]

capy::task<> send_sliced(
    Stream& stream,
    std::array<capy::const_buffer, 2> const& bufs)
{
    // tag::buffer_slice[]
    // send only the first 16 KB
    co_await capy::write(stream, capy::buffer_slice(bufs, 0, 16384));
    // everything after the first 16 KB
    auto rest = capy::buffer_slice(bufs, 16384);
    co_await capy::write(stream, rest);
    // end::buffer_slice[]
    BOOST_TEST(capy::buffer_size(rest) == capy::buffer_size(bufs) - 16384);
}

struct sequences_test
{
    void testModels()
    {
        // tag::concept_models[]
        // Single buffers
        capy::const_buffer cb;    // ConstBufferSequence
        capy::mutable_buffer mb;  // MutableBufferSequence (and ConstBufferSequence)

        // Standard containers of buffers
        std::vector<capy::const_buffer> v;        // ConstBufferSequence
        std::array<capy::mutable_buffer, 3> a;    // MutableBufferSequence

        // String types (wrap with make_buffer to get a single buffer)
        std::string str;                    // make_buffer(str) -> mutable_buffer
        std::string_view sv;                // make_buffer(sv) -> const_buffer
        // end::concept_models[]
        static_assert(capy::ConstBufferSequence<decltype(cb)>);
        static_assert(capy::MutableBufferSequence<decltype(mb)>);
        static_assert(capy::ConstBufferSequence<decltype(v)>);
        static_assert(capy::MutableBufferSequence<decltype(a)>);
        static_assert(!capy::ConstBufferSequence<decltype(str)>);
        static_assert(
            capy::MutableBufferSequence<decltype(capy::make_buffer(str))>);
        static_assert(
            capy::ConstBufferSequence<decltype(capy::make_buffer(sv))>);
        BOOST_TEST(capy::buffer_size(cb) == 0);
        BOOST_TEST(mb.size() == 0);
        BOOST_TEST(capy::buffer_size(v) == 0);
        BOOST_TEST(capy::buffer_size(a) == 0);
        BOOST_TEST(capy::make_buffer(str).size() == 0);
        BOOST_TEST(capy::make_buffer(sv).size() == 0);
    }

    void testHeterogeneous()
    {
        capy::const_buffer buf1, buf2;
        chained_buffers my_custom_buffer_sequence{};
        send_lengths.clear();
        // tag::send_calls[]
        // All of these work:
        send(capy::make_buffer("Hello"));                    // string literal
        send(capy::make_buffer(std::string_view{"Hello"}));  // string_view
        send(std::array{buf1, buf2});                  // array of buffers
        send(my_custom_buffer_sequence);               // custom type
        // end::send_calls[]
        BOOST_TEST(send_lengths ==
            (std::vector<std::size_t>{1, 1, 2, 3}));
    }

    void testIterate()
    {
        char data[4] = {};
        process(capy::make_buffer(data));
        process(std::array{
            capy::const_buffer(data, 2), capy::const_buffer(data + 2, 2)});
    }

    void testReadAll()
    {
        auto [a, b] = capy::test::make_stream_pair();
        b.provide("abcdefghijklmnopqrst");  // 20 bytes readable from a
        a.set_max_read_size(7);             // force several partial reads

        std::vector<char> head(8), tail(12);
        std::array<capy::mutable_buffer, 2> bufs{
            capy::make_buffer(head), capy::make_buffer(tail)};

        std::size_t got = 0;
        capy::test::run_blocking([&](std::size_t n) { got = n; })(
            read_all(a, bufs));

        BOOST_TEST(got == 20);
        BOOST_TEST(std::string(head.begin(), head.end()) == "abcdefgh");
        BOOST_TEST(std::string(tail.begin(), tail.end()) == "ijklmnopqrst");
    }

    void testBufferSlice()
    {
        auto [a, b] = capy::test::make_stream_pair();
        std::string part1(10000, 'x');
        std::string part2(10000, 'y');
        std::array<capy::const_buffer, 2> bufs{
            capy::make_buffer(part1), capy::make_buffer(part2)};

        capy::test::run_blocking()(send_sliced(a, bufs));

        BOOST_TEST(b.data() == part1 + part2);
    }

    void run()
    {
        testModels();
        testHeterogeneous();
        testIterate();
        testReadAll();
        testBufferSlice();
    }
};

} // namespace

TEST_SUITE(sequences_test, "boost.capy.doc.5c_sequences");
