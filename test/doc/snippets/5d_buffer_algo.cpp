//
// Copyright (c) 2026 Steve Gerbino
// Copyright (c) 2026 Michael Vandeberg
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Compiled fragments shown in pages/5.buffers/5d.buffer-algo.adoc.
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
#include <boost/capy/io_task.hpp>
#include <boost/capy/task.hpp>
#include <boost/capy/test/run_blocking.hpp>
#include <boost/capy/test/stream.hpp>
#include <boost/capy/write.hpp>

#include <algorithm>
#include <array>
#include <cassert>
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
// Buffer algorithms
// ---------------------------------------------------------------------

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

// tag::read_loop[]
template<capy::ReadStream Stream, capy::MutableBufferSequence Buffers>
capy::task<std::size_t> read_full(Stream& stream, Buffers buffers)
{
    capy::consuming_buffers remaining(buffers);
    std::size_t const total_size = capy::buffer_size(buffers);
    std::size_t total = 0;

    while (total < total_size)
    {
        auto [ec, n] = co_await stream.read_some(remaining.data());
        remaining.consume(n);
        total += n;
        if (ec)
            co_return total;
    }

    co_return total;
}
// end::read_loop[]

// tag::write_loop[]
template<capy::WriteStream Stream, capy::ConstBufferSequence Buffers>
capy::task<std::size_t> write_full(Stream& stream, Buffers buffers)
{
    capy::consuming_buffers remaining(buffers);
    std::size_t const total_size = capy::buffer_size(buffers);
    std::size_t total = 0;

    while (total < total_size)
    {
        auto [ec, n] = co_await stream.write_some(remaining.data());
        remaining.consume(n);
        total += n;
        if (ec)
            co_return total;
    }

    co_return total;
}
// end::write_loop[]

std::string
build_header()
{
    return "header: ";
}

std::vector<char>
load_body()
{
    return {'b', 'o', 'd', 'y'};
}

[[maybe_unused]] capy::task<>
zero_copy_write(capy::test::stream& stream)
{
    // tag::zero_copy[]
    std::string header = build_header();
    std::vector<char> body = load_body();

    // No copyingâ€”header and body are written directly
    std::array buffers = {capy::make_buffer(header), capy::make_buffer(body)};
    co_await capy::write(stream, buffers);
    // end::zero_copy[]
}

[[maybe_unused]] capy::task<>
scatter_gather_write(
    capy::test::stream& stream,
    capy::const_buffer header_buf, capy::const_buffer separator_buf,
    capy::const_buffer body_buf, capy::const_buffer footer_buf)
{
    // tag::scatter_gather[]
    std::array buffers = {header_buf, separator_buf, body_buf, footer_buf};
    co_await capy::write(stream, buffers);  // Single system call
    // end::scatter_gather[]
}

#if __has_include(<sys/mman.h>)

[[maybe_unused]] capy::task<>
send_mapped_file(capy::test::stream& socket, int fd, std::size_t file_size)
{
    // tag::mmap_buffer[]
    // Memory-mapped file
    void* mapped = mmap(nullptr, file_size, PROT_READ, MAP_PRIVATE, fd, 0);
    capy::const_buffer file_buf(mapped, file_size);
    co_await capy::write(socket, file_buf);  // Zero-copy network transmission
    // end::mmap_buffer[]
}

#endif // __has_include(<sys/mman.h>)

// Bidirectional iterator presenting each chunk as a const_buffer. Kept
// out of the page fragment, which focuses on the sequence shape.
class chunk_iterator
{
    std::vector<std::vector<char>>::const_iterator it_;

public:
    using value_type = capy::const_buffer;
    using difference_type = std::ptrdiff_t;

    chunk_iterator() = default;

    explicit chunk_iterator(
        std::vector<std::vector<char>>::const_iterator it)
        : it_(it)
    {
    }

    capy::const_buffer operator*() const
    {
        return capy::make_buffer(*it_);
    }

    chunk_iterator& operator++()
    {
        ++it_;
        return *this;
    }

    chunk_iterator operator++(int)
    {
        auto tmp = *this;
        ++it_;
        return tmp;
    }

    chunk_iterator& operator--()
    {
        --it_;
        return *this;
    }

    chunk_iterator operator--(int)
    {
        auto tmp = *this;
        --it_;
        return tmp;
    }

    bool operator==(chunk_iterator const&) const = default;
};

// tag::custom_sequence[]
class chunked_buffer_sequence
{
    std::vector<std::vector<char>> chunks_;

public:
    auto begin() const { return chunk_iterator(chunks_.begin()); }
    auto end() const { return chunk_iterator(chunks_.end()); }
};
// Satisfies ConstBufferSequenceâ€”works with all algorithms
// end::custom_sequence[]

static_assert(capy::ConstBufferSequence<chunked_buffer_sequence>);

capy::task<>
read_full_driver(
    capy::test::stream& wr, capy::test::stream& rd,
    std::array<capy::mutable_buffer, 2> bufs, std::size_t& got)
{
    co_await capy::write(wr, capy::make_buffer("hello world"sv));
    got = co_await read_full(rd, bufs);
}

capy::task<>
write_full_driver(
    capy::test::stream& wr, std::array<capy::const_buffer, 2> bufs,
    std::size_t& put)
{
    put = co_await write_full(wr, bufs);
}

struct buffer_algo_test
{
    void
    testBufferSize()
    {
        // tag::buffer_size_example[]
        auto buf1 = capy::make_buffer("hello"sv);  // 5 bytes
        auto buf2 = capy::make_buffer("world"sv);  // 5 bytes
        auto buf3 = capy::make_buffer(""sv);       // 0 bytes
        auto combined = std::array{buf1, buf2, buf3};

        assert(capy::buffer_size(combined) == 10);
        // end::buffer_size_example[]
        BOOST_TEST(capy::buffer_size(combined) == 10);
    }

    void
    testBufferCopy()
    {
        // tag::buffer_copy_example[]
        char source_data[] = "hello world";
        char dest_data[20];

        capy::const_buffer src(source_data, 11);
        capy::mutable_buffer dst(dest_data, 20);

        std::size_t copied = capy::buffer_copy(dst, src);  // 11
        // end::buffer_copy_example[]
        BOOST_TEST(copied == 11);
        BOOST_TEST(std::memcmp(dest_data, "hello world", 11) == 0);
    }

    void
    testPartialCopy()
    {
        char source_data[] = "hello world";
        char dest_data[20] = {};
        capy::const_buffer src(source_data, 11);
        capy::mutable_buffer dst(dest_data, 20);
        // tag::buffer_copy_at_most[]
        // Copy at most 5 bytes
        std::size_t copied = capy::buffer_copy(dst, src, 5);
        // end::buffer_copy_at_most[]
        BOOST_TEST(copied == 5);
        BOOST_TEST(std::memcmp(dest_data, "hello", 5) == 0);
    }

    void
    testCrossSequenceCopy()
    {
        capy::const_buffer buf1("abc", 3);
        capy::const_buffer buf2("defg", 4);
        capy::const_buffer buf3("hi", 2);
        char big_data[6] = {};
        char small_data[2] = {};
        capy::mutable_buffer large_buf(big_data, 6);
        capy::mutable_buffer small_buf(small_data, 2);
        // tag::buffer_copy_cross[]
        // Source: 3 buffers
        std::array<capy::const_buffer, 3> src = {buf1, buf2, buf3};

        // Target: 2 buffers with different sizes
        std::array<capy::mutable_buffer, 2> dst = {large_buf, small_buf};

        // Copies across buffer boundaries as needed
        std::size_t copied = capy::buffer_copy(dst, src);
        // end::buffer_copy_cross[]
        BOOST_TEST(copied == 8);
        BOOST_TEST(std::memcmp(big_data, "abcdef", 6) == 0);
        BOOST_TEST(std::memcmp(small_data, "gh", 2) == 0);
    }

    void
    testReadFull()
    {
        auto [a, b] = capy::test::make_stream_pair();
        char storage1[6] = {};
        char storage2[5] = {};
        std::array<capy::mutable_buffer, 2> bufs = {
            capy::mutable_buffer(storage1, 6),
            capy::mutable_buffer(storage2, 5)};
        std::size_t got = 0;
        capy::test::run_blocking()(read_full_driver(a, b, bufs, got));
        BOOST_TEST(got == 11);
        BOOST_TEST(std::memcmp(storage1, "hello ", 6) == 0);
        BOOST_TEST(std::memcmp(storage2, "world", 5) == 0);
    }

    void
    testWriteFull()
    {
        auto [a, b] = capy::test::make_stream_pair();
        std::array<capy::const_buffer, 2> bufs = {
            capy::make_buffer("hello "sv),
            capy::make_buffer("world"sv)};
        std::size_t put = 0;
        capy::test::run_blocking()(write_full_driver(a, bufs, put));
        BOOST_TEST(put == 11);
        BOOST_TEST(b.data() == "hello world");
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
        testBufferSize();
        testBufferCopy();
        testPartialCopy();
        testCrossSequenceCopy();
        testReadFull();
        testWriteFull();
        testReadAll();
        testBufferSlice();
    }
};

} // namespace

TEST_SUITE(buffer_algo_test, "boost.capy.doc.5d_buffer_algo");
