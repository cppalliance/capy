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

namespace {

using namespace boost::capy;

static_assert(ConstBufferSequence<const_buffer>);
static_assert(ConstBufferSequence<std::vector<const_buffer>>);
static_assert(!ConstBufferSequence<int>);
static_assert(MutableBufferSequence<mutable_buffer>);
static_assert(!MutableBufferSequence<const_buffer>);

// tag::send_signature[]
template<ConstBufferSequence Buffers>
void send(Buffers const& bufs);
// end::send_signature[]

// Logs the element count of every call so all four calls are observable.
std::vector<std::size_t> send_lengths;

template<ConstBufferSequence Buffers>
void send(Buffers const& bufs)
{
    send_lengths.push_back(buffer_length(bufs));
}

// The custom type used by the heterogeneous-composition fragment.
struct chained_buffers
{
    std::array<const_buffer, 3> parts;
    auto begin() const noexcept { return parts.begin(); }
    auto end() const noexcept { return parts.end(); }
};

// tag::iterate[]
template<ConstBufferSequence Buffers>
void process(Buffers const& bufs)
{
    for (auto it = begin(bufs); it != end(bufs); ++it)
    {
        const_buffer buf = *it;
        // Process buf.data(), buf.size()
    }
}
// end::iterate[]

using Stream = test::stream;

// tag::read_all[]
template<MutableBufferSequence Buffers>
task<std::size_t> read_all(Stream& stream, Buffers buffers)
{
    consuming_buffers consuming(buffers);
    std::size_t const total_size = buffer_size(buffers);
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

task<> send_sliced(
    Stream& stream,
    std::array<const_buffer, 2> const& bufs)
{
    // tag::buffer_slice[]
    co_await write(stream, buffer_slice(bufs, 0, 16384));  // send only the first 16 KB
    auto rest = buffer_slice(bufs, 16384);                 // everything after the first 16 KB
    co_await write(stream, rest);
    // end::buffer_slice[]
    BOOST_TEST(buffer_size(rest) == buffer_size(bufs) - 16384);
}

struct sequences_test
{
    void testModels()
    {
        // tag::concept_models[]
        // Single buffers
        const_buffer cb;                    // ConstBufferSequence
        mutable_buffer mb;                  // MutableBufferSequence (and ConstBufferSequence)

        // Standard containers of buffers
        std::vector<const_buffer> v;        // ConstBufferSequence
        std::array<mutable_buffer, 3> a;    // MutableBufferSequence

        // String types (wrap with make_buffer to get a single buffer)
        std::string str;                    // make_buffer(str) -> mutable_buffer
        std::string_view sv;                // make_buffer(sv) -> const_buffer
        // end::concept_models[]
        static_assert(ConstBufferSequence<decltype(cb)>);
        static_assert(MutableBufferSequence<decltype(mb)>);
        static_assert(ConstBufferSequence<decltype(v)>);
        static_assert(MutableBufferSequence<decltype(a)>);
        static_assert(!ConstBufferSequence<decltype(str)>);
        static_assert(MutableBufferSequence<decltype(make_buffer(str))>);
        static_assert(ConstBufferSequence<decltype(make_buffer(sv))>);
        BOOST_TEST(buffer_size(cb) == 0);
        BOOST_TEST(mb.size() == 0);
        BOOST_TEST(buffer_size(v) == 0);
        BOOST_TEST(buffer_size(a) == 0);
        BOOST_TEST(make_buffer(str).size() == 0);
        BOOST_TEST(make_buffer(sv).size() == 0);
    }

    void testHeterogeneous()
    {
        const_buffer buf1, buf2;
        chained_buffers my_custom_buffer_sequence{};
        send_lengths.clear();
        // tag::send_calls[]
        // All of these work:
        send(make_buffer("Hello"));                    // string literal
        send(make_buffer(std::string_view{"Hello"}));  // string_view
        send(std::array{buf1, buf2});                  // array of buffers
        send(my_custom_buffer_sequence);               // custom type
        // end::send_calls[]
        BOOST_TEST(send_lengths ==
            (std::vector<std::size_t>{1, 1, 2, 3}));
    }

    void testIterate()
    {
        char data[4] = {};
        process(make_buffer(data));
        process(std::array{const_buffer(data, 2), const_buffer(data + 2, 2)});
    }

    void testReadAll()
    {
        auto [a, b] = test::make_stream_pair();
        b.provide("abcdefghijklmnopqrst");  // 20 bytes readable from a
        a.set_max_read_size(7);             // force several partial reads

        std::vector<char> head(8), tail(12);
        std::array<mutable_buffer, 2> bufs{
            make_buffer(head), make_buffer(tail)};

        std::size_t got = 0;
        test::run_blocking([&](std::size_t n) { got = n; })(
            read_all(a, bufs));

        BOOST_TEST(got == 20);
        BOOST_TEST(std::string(head.begin(), head.end()) == "abcdefgh");
        BOOST_TEST(std::string(tail.begin(), tail.end()) == "ijklmnopqrst");
    }

    void testBufferSlice()
    {
        auto [a, b] = test::make_stream_pair();
        std::string part1(10000, 'x');
        std::string part2(10000, 'y');
        std::array<const_buffer, 2> bufs{
            make_buffer(part1), make_buffer(part2)};

        test::run_blocking()(send_sliced(a, bufs));

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
