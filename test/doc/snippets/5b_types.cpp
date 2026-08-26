//
// Copyright (c) 2026 Steve Gerbino
// Copyright (c) 2026 Michael Vandeberg
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Compiled fragments shown in pages/5.buffers/5b.types.adoc.

#include "../doc_warnings.hpp"

#include <boost/capy/buffers.hpp>
// tag::make_buffer_include[]
#include <boost/capy/buffers/make_buffer.hpp>
// end::make_buffer_include[]

#include <array>
#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "test_suite.hpp"

namespace capy = boost::capy;

namespace {

// Records the size seen so the conversion fragment is observable.
std::size_t processed_size = 0;

void process(capy::const_buffer buf)
{
    processed_size = buf.size();
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

struct types_test
{
    void testConstruction()
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

    void testAccessors()
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

    void testPrefixRemoval()
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

    void testConversion()
    {
        char data[8] = {};
        std::size_t size = sizeof(data);
        // tag::mutable_to_const[]
        void process(capy::const_buffer buf);

        capy::mutable_buffer mbuf(data, size);
        process(mbuf);  // OK: implicit conversion
        // end::mutable_to_const[]
        BOOST_TEST(processed_size == size);
    }

    void testMakeBuffer()
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

    void testSingleAsSequence()
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

    void testBeginEnd()
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

    void run()
    {
        testConstruction();
        testAccessors();
        testPrefixRemoval();
        testConversion();
        testMakeBuffer();
        testSingleAsSequence();
        testBeginEnd();
    }
};

} // namespace

TEST_SUITE(types_test, "boost.capy.doc.5b_types");
