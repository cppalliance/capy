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
#include <boost/capy/buffers/buffer_copy.hpp>
#include <boost/capy/concept/read_stream.hpp>
#include <boost/capy/concept/write_stream.hpp>
#include <boost/capy/io_task.hpp>
#include <boost/capy/io/any_write_stream.hpp>
#include <boost/capy/task.hpp>

#include <array>
#include <cassert>
#include <cstddef>
#include <cstring>
#include <iterator>
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
// Buffer types
// ---------------------------------------------------------------------

// Records the size seen so the conversion fragment is observable.
std::size_t handled_size = 0;

void handle_buffer(capy::const_buffer buf)
{
    handled_size = buf.size();
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
// comment explains the loop body instead.

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

struct composite_buffers_test
{
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
    testBeginEnd()
    {
        // tag::begin_end_uniform[]
        capy::const_buffer single;
        auto it = capy::begin(single);  // Returns pointer to `single`
        auto e = capy::end(single);     // Returns pointer past `single`
        assert(std::distance(it, e) == 1);

        std::array<capy::const_buffer, 3> multi;
        auto it2 = capy::begin(multi);  // Returns multi.begin()
        auto e2 = capy::end(multi);     // Returns multi.end()
        assert(std::distance(it2, e2) == 3);
        // end::begin_end_uniform[]
        BOOST_TEST(it == &single);
        BOOST_TEST(e == &single + 1);
        BOOST_TEST(it2 == multi.begin());
        BOOST_TEST(e2 == multi.end());
    }

    void
    testLength()
    {
      auto test_case = [](capy::ConstBufferSequence auto buf)
      {
        
        std::size_t len = 
        // tag::length_equivalent[]
        std::distance(capy::begin(buf), capy::end(buf))
        // end::length_equivalent[]
        ;
        BOOST_TEST(capy::buffer_length(buf) == len);
      };

      test_case(capy::const_buffer{});
      test_case(std::array<capy::const_buffer, 1>{});
      test_case(std::array<capy::const_buffer, 3>{});
      test_case(std::vector<capy::const_buffer>(2));
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
    run()
    {
        testConstruction();
        testConversion();
        testBeginEnd();
        testLength();
        testHeterogeneous();
        testIterate();
    }
};

} // namespace

TEST_SUITE(composite_buffers_test, "boost.capy.doc.5b_composite_buffers");
