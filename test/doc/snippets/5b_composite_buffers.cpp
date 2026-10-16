//
// Copyright (c) 2026 Steve Gerbino
// Copyright (c) 2026 Michael Vandeberg
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Compiled fragments shown in pages/5.buffers/5b.composite-buffers.adoc.
//
//

#include "../doc_warnings.hpp"

// tag::include_buffers[]
#include <boost/capy/buffers.hpp>
// end::include_buffers[]
// tag::make_buffer_include[]
#include <boost/capy/buffers/make_buffer.hpp>
// end::make_buffer_include[]
// tag::buffer_slice_include[]
#include <boost/capy/buffers/buffer_slice.hpp>
// end::buffer_slice_include[]
// tag::consuming_buffers_include[]
#include <boost/capy/buffers/consuming_buffers.hpp>
// end::consuming_buffers_include[]

#include <boost/capy/buffers/buffer_copy.hpp>
#include <boost/capy/concept/read_stream.hpp>
#include <boost/capy/concept/write_stream.hpp>
#include <boost/capy/results.hpp>
#include <boost/capy/task.hpp>
#include <boost/capy/io/any_write_stream.hpp>
#include <boost/capy/test/run_blocking.hpp>
#include <boost/capy/test/stream.hpp>
#include <boost/capy/write.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#if __has_include(<sys/uio.h>)
#include <sys/uio.h>
#include <unistd.h>
#define BOOST_CAPY_DOC_HAS_POSIX_IO
#endif

#if __has_include(<liburing.h>)
#include <liburing.h>
#endif

#if __has_include(<sys/mman.h>)
#include <sys/mman.h>
#endif

#include "test_suite.hpp"

namespace capy = boost::capy;

namespace {

using namespace std::string_view_literals;

// ---------------------------------------------------------------------
// Why a concept rather than one span type
// ---------------------------------------------------------------------

// tag::span_signatures[]
void write_data(std::span<std::byte const> data);
void read_data(std::span<std::byte> buffer);
// end::span_signatures[]

// tag::span_of_spans[]
void write_data(std::span<std::span<std::byte const> const> buffers);
// end::span_of_spans[]

// tag::span_aliases[]
using HeaderBuffers = std::array<std::span<std::byte const>, 2>;  // 2 buffers
using BodyBuffers = std::array<std::span<std::byte const>, 3>;    // 3 buffers
// end::span_aliases[]

// Definitions for the declared signatures; the fragments only show
// the declarations.
[[maybe_unused]] void write_data(std::span<std::byte const>)
{
}

[[maybe_unused]] void read_data(std::span<std::byte>)
{
}

// Records the buffer count so the combining fragment is observable.
std::size_t last_write_count = 0;

void write_data(std::span<std::span<std::byte const> const> buffers)
{
    last_write_count = buffers.size();
}

// ---------------------------------------------------------------------
// Buffer types
// ---------------------------------------------------------------------

// Records the size seen so the conversion fragment is observable.
std::size_t handled_size = 0;

void handle_buffer(capy::const_buffer buf)
{
    handled_size = buf.size();
}

// tag::write_data_signature[]
template<capy::ConstBufferSequence Buffers>
void write_data(Buffers const& buffers);
// end::write_data_signature[]

// Logs the element count of every call so all three calls are observable.
std::vector<std::size_t> write_data_lengths;

template<capy::ConstBufferSequence Buffers>
void write_data(Buffers const& buffers)
{
    write_data_lengths.push_back(capy::buffer_length(buffers));
}

// The custom sequence used by the calls fragment.
struct composite_buffers
{
    std::array<capy::const_buffer, 2> parts;
    auto begin() const noexcept { return parts.begin(); }
    auto end() const noexcept { return parts.end(); }
};

// ---------------------------------------------------------------------
// Buffer sequences
// ---------------------------------------------------------------------

static_assert(capy::ConstBufferSequence<capy::const_buffer>);
static_assert(capy::ConstBufferSequence<std::vector<capy::const_buffer>>);
static_assert(!capy::ConstBufferSequence<int>);
static_assert(capy::MutableBufferSequence<capy::mutable_buffer>);
static_assert(!capy::MutableBufferSequence<capy::const_buffer>);

// tag::test_mutable_buffer[]
static_assert(capy::MutableBufferSequence<capy::mutable_buffer>);
static_assert(capy::MutableBufferSequence<std::span<capy::mutable_buffer>>);
static_assert(capy::MutableBufferSequence<std::vector<capy::mutable_buffer>>);
static_assert(capy::MutableBufferSequence<std::array<capy::mutable_buffer, 4>>);
// end::test_mutable_buffer[]

// tag::test_const_buffer[]
static_assert(capy::ConstBufferSequence<capy::const_buffer>);
static_assert(capy::ConstBufferSequence<capy::mutable_buffer>);
static_assert(capy::ConstBufferSequence<std::span<capy::const_buffer>>);
static_assert(capy::ConstBufferSequence<std::span<capy::mutable_buffer>>);
static_assert(capy::ConstBufferSequence<std::array<capy::const_buffer, 4>>);
static_assert(capy::ConstBufferSequence<std::array<capy::mutable_buffer, 4>>);
// end::test_const_buffer[]

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

// The iteration fragment binds each buffer without using it; the page
// comment explains the loop body instead. The slice fragment discards
// results values the same way.

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

struct composite_buffers_test
{
    void
    testManualCombine()
    {
        // tag::span_combine[]
        HeaderBuffers headers{ /* ... */ };
        BodyBuffers body{ /* ... */ };

        // To combine, you MUST allocate a new array:
        std::array<std::span<std::byte const>, 5> combined;
        std::copy(headers.begin(), headers.end(), combined.begin());
        std::copy(body.begin(), body.end(), combined.begin() + 2);

        write_data(combined);
        // end::span_combine[]
        BOOST_TEST(last_write_count == 5);
    }

    void
    testConstruction()
    {
        // tag::const_buffer_construct[]
        // From pointer and size
        char data[] = "hello";
        capy::const_buffer buf(data, 5);

        // From mutable_buffer (implicit)
        capy::mutable_buffer mbuf(data, 5);
        capy::const_buffer cbuf = mbuf;  // OK: mutable -> const
        // end::const_buffer_construct[]
        BOOST_TEST(buf.data() == data);
        BOOST_TEST(buf.size() == 5);
        BOOST_TEST(cbuf.data() == mbuf.data());
        BOOST_TEST(cbuf.size() == 5);
    }

    void
    testAccessors()
    {
        char data[] = "hello";
        // tag::const_buffer_accessors[]
        capy::const_buffer buf(data, 5);

        void const* ptr = buf.data();  // Pointer to first byte
        std::size_t len = buf.size();  // Number of bytes
        // end::const_buffer_accessors[]
        BOOST_TEST(ptr == data);
        BOOST_TEST(len == 5);
    }

    void
    testPrefixRemoval()
    {
        char data[] = "0123456789";
        // tag::const_buffer_prefix[]
        capy::const_buffer buf(data, 10);

        buf += 3;  // Remove first 3 bytes
        // buf.data() now points 3 bytes later
        // buf.size() is now 7
        // end::const_buffer_prefix[]
        BOOST_TEST(buf.data() == data + 3);
        BOOST_TEST(buf.size() == 7);
    }

    void
    testConversion()
    {
        char data[8] = {};
        std::size_t size = sizeof(data);
        // tag::mutable_to_const[]
        void handle_buffer(capy::const_buffer buf);

        capy::mutable_buffer mbuf(data, size);
        handle_buffer(mbuf);  // OK: implicit conversion
        // end::mutable_to_const[]
        BOOST_TEST(handled_size == size);
    }

    std::string make_headers() { return {}; }
    std::string make_body() { return {}; }
    std::string stamp(const std::string&, const std::string&) { return {}; }

    capy::task<>
    testRangesOfUnits(capy::any_write_stream& stream)
    {
        // tag::range_of_units[]
        const std::string headers = make_headers();
        const std::string body = make_body();
        const std::string checksum = stamp(headers, body);

        std::array buf {capy::make_buffer(headers), 
                        capy::make_buffer(body), 
                        capy::make_buffer(checksum)};

        co_await stream.write_some(buf);
        // end::range_of_units[]
    }
    void
    testMakeBuffer()
    {
        char storage[64];
        void* ptr = storage;
        std::size_t size = sizeof(storage);
        // tag::make_buffer_sources[]
        // From pointer and size
        auto buf = capy::make_buffer(ptr, size);

        // From C array
        char arr[10];
        auto arr_buf = capy::make_buffer(arr);

        // From std::array
        std::array<char, 10> std_arr;
        auto std_arr_buf = capy::make_buffer(std_arr);

        // From std::vector
        std::vector<char> vec(100);
        auto vec_buf = capy::make_buffer(vec);

        // From std::string
        std::string str = "hello";
        auto str_buf = capy::make_buffer(str);

        // From std::string_view
        std::string_view sv = "hello";
        auto sv_buf = capy::make_buffer(sv);

        // From a span (std::span or boost::span)
        std::span<char> sp(arr);
        auto sp_buf = capy::make_buffer(sp);
        // end::make_buffer_sources[]
        BOOST_TEST(buf.data() == storage);
        BOOST_TEST(buf.size() == 64);
        BOOST_TEST(arr_buf.size() == 10);
        BOOST_TEST(std_arr_buf.size() == 10);
        BOOST_TEST(vec_buf.size() == 100);
        BOOST_TEST(str_buf.size() == 5);
        BOOST_TEST(sv_buf.size() == 5);
        BOOST_TEST(sp_buf.data() == arr);
        BOOST_TEST(sp_buf.size() == 10);
    }

    void
    testSingleAsSequence()
    {
        capy::const_buffer buf1, buf2, buf3;
        composite_buffers my_composite{};
        write_data_lengths.clear();
        // tag::write_data_calls[]
        // All of these work:
        write_data(capy::make_buffer("hello"));         // Single buffer
        write_data(std::array{buf1, buf2, buf3}); // Multiple buffers
        write_data(my_composite);                 // Custom sequence
        // end::write_data_calls[]
        BOOST_TEST(write_data_lengths ==
            (std::vector<std::size_t>{1, 3, 2}));
    }

    void
    testBeginEnd()
    {
        // tag::begin_end_uniform[]
        capy::const_buffer single;
        auto it = capy::begin(single);  // Returns pointer to single
        auto e = capy::end(single);     // Returns pointer past single

        std::array<capy::const_buffer, 3> multi;
        auto it2 = capy::begin(multi);  // Returns multi.begin()
        auto e2 = capy::end(multi);     // Returns multi.end()
        // end::begin_end_uniform[]
        BOOST_TEST(it == &single);
        BOOST_TEST(e == &single + 1);
        BOOST_TEST(it2 == multi.begin());
        BOOST_TEST(e2 == multi.end());
    }

    void
    testModels()
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

    void
    testHeterogeneous()
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

    void
    testIterate()
    {
        char data[4] = {};
        process(capy::make_buffer(data));
        process(std::array{
            capy::const_buffer(data, 2), capy::const_buffer(data + 2, 2)});
    }

    void
    testReadAll()
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

    void
    testBufferSlice()
    {
        auto [a, b] = capy::test::make_stream_pair();
        std::string part1(10000, 'x');
        std::string part2(10000, 'y');
        std::array<capy::const_buffer, 2> bufs{
            capy::make_buffer(part1), capy::make_buffer(part2)};

        capy::test::run_blocking()(send_sliced(a, bufs));

        BOOST_TEST(b.data() == part1 + part2);
    }

    void
    run()
    {
        testManualCombine();
        testConstruction();
        testAccessors();
        testPrefixRemoval();
        testConversion();
        testMakeBuffer();
        testSingleAsSequence();
        testBeginEnd();
        testModels();
        testHeterogeneous();
        testIterate();
        testReadAll();
        testBufferSlice();
    }
};

} // namespace

TEST_SUITE(composite_buffers_test, "boost.capy.doc.5b_composite_buffers");
