//
// Copyright (c) 2026 Steve Gerbino
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Compiled fragments shown in pages/5.buffers/5e.algorithms.adoc.

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
#include <boost/capy/buffers/buffer_copy.hpp>
#include <boost/capy/buffers/consuming_buffers.hpp>
#include <boost/capy/buffers/make_buffer.hpp>
#include <boost/capy/concept/read_stream.hpp>
#include <boost/capy/concept/write_stream.hpp>
#include <boost/capy/task.hpp>
#include <boost/capy/test/run_blocking.hpp>
#include <boost/capy/test/stream.hpp>
#include <boost/capy/write.hpp>

#include <array>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#if __has_include(<sys/mman.h>)
#include <sys/mman.h>
#endif

#include "test_suite.hpp"

namespace {

using namespace boost::capy;
using namespace std::string_view_literals;

// tag::read_loop[]
template<ReadStream Stream, MutableBufferSequence Buffers>
task<std::size_t> read_full(Stream& stream, Buffers buffers)
{
    consuming_buffers remaining(buffers);
    std::size_t const total_size = buffer_size(buffers);
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
template<WriteStream Stream, ConstBufferSequence Buffers>
task<std::size_t> write_full(Stream& stream, Buffers buffers)
{
    consuming_buffers remaining(buffers);
    std::size_t const total_size = buffer_size(buffers);
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

[[maybe_unused]] task<>
zero_copy_write(test::stream& stream)
{
    // tag::zero_copy[]
    std::string header = build_header();
    std::vector<char> body = load_body();

    // No copying—header and body are written directly
    std::array buffers = {make_buffer(header), make_buffer(body)};
    co_await write(stream, buffers);
    // end::zero_copy[]
}

[[maybe_unused]] task<>
scatter_gather_write(
    test::stream& stream,
    const_buffer header_buf, const_buffer separator_buf,
    const_buffer body_buf, const_buffer footer_buf)
{
    // tag::scatter_gather[]
    std::array buffers = {header_buf, separator_buf, body_buf, footer_buf};
    co_await write(stream, buffers);  // Single system call
    // end::scatter_gather[]
}

#if __has_include(<sys/mman.h>)

[[maybe_unused]] task<>
send_mapped_file(test::stream& socket, int fd, std::size_t file_size)
{
    // tag::mmap_buffer[]
    // Memory-mapped file
    void* mapped = mmap(nullptr, file_size, PROT_READ, MAP_PRIVATE, fd, 0);
    const_buffer file_buf(mapped, file_size);
    co_await write(socket, file_buf);  // Zero-copy network transmission
    // end::mmap_buffer[]
}

#endif // __has_include(<sys/mman.h>)

// Bidirectional iterator presenting each chunk as a const_buffer. Kept
// out of the page fragment, which focuses on the sequence shape.
class chunk_iterator
{
    std::vector<std::vector<char>>::const_iterator it_;

public:
    using value_type = const_buffer;
    using difference_type = std::ptrdiff_t;

    chunk_iterator() = default;

    explicit chunk_iterator(
        std::vector<std::vector<char>>::const_iterator it)
        : it_(it)
    {
    }

    const_buffer operator*() const
    {
        return make_buffer(*it_);
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
// Satisfies ConstBufferSequence—works with all algorithms
// end::custom_sequence[]

static_assert(ConstBufferSequence<chunked_buffer_sequence>);

task<>
read_full_driver(
    test::stream& wr, test::stream& rd,
    std::array<mutable_buffer, 2> bufs, std::size_t& got)
{
    co_await write(wr, make_buffer("hello world"sv));
    got = co_await read_full(rd, bufs);
}

task<>
write_full_driver(
    test::stream& wr, std::array<const_buffer, 2> bufs,
    std::size_t& put)
{
    put = co_await write_full(wr, bufs);
}

struct algorithms_test
{
    void
    testBufferSize()
    {
        // tag::buffer_size_example[]
        auto buf1 = make_buffer("hello"sv);  // 5 bytes
        auto buf2 = make_buffer("world"sv);  // 5 bytes
        auto combined = std::array{buf1, buf2};

        std::size_t total = buffer_size(combined);  // 10
        // end::buffer_size_example[]
        BOOST_TEST(total == 10);
    }

    void
    testBufferEmpty()
    {
        // tag::buffer_empty_example[]
        const_buffer empty_buf;
        buffer_empty(empty_buf);  // true

        const_buffer non_empty("data", 4);
        buffer_empty(non_empty);  // false
        // end::buffer_empty_example[]
        BOOST_TEST(buffer_empty(empty_buf));
        BOOST_TEST(! buffer_empty(non_empty));
    }

    void
    testBufferLength()
    {
        const_buffer buf1("one", 3);
        const_buffer buf2("two", 3);
        const_buffer buf3("three", 5);
        // tag::buffer_length_example[]
        auto single = make_buffer("hello"sv);
        buffer_length(single);  // 1

        auto arr = std::array{buf1, buf2, buf3};
        buffer_length(arr);  // 3
        // end::buffer_length_example[]
        BOOST_TEST(buffer_length(single) == 1);
        BOOST_TEST(buffer_length(arr) == 3);
    }

    void
    testBufferCopy()
    {
        // tag::buffer_copy_example[]
        char source_data[] = "hello world";
        char dest_data[20];

        const_buffer src(source_data, 11);
        mutable_buffer dst(dest_data, 20);

        std::size_t copied = buffer_copy(dst, src);  // 11
        // end::buffer_copy_example[]
        BOOST_TEST(copied == 11);
        BOOST_TEST(std::memcmp(dest_data, "hello world", 11) == 0);
    }

    void
    testPartialCopy()
    {
        char source_data[] = "hello world";
        char dest_data[20] = {};
        const_buffer src(source_data, 11);
        mutable_buffer dst(dest_data, 20);
        // tag::buffer_copy_at_most[]
        std::size_t copied = buffer_copy(dst, src, 5);  // Copy at most 5 bytes
        // end::buffer_copy_at_most[]
        BOOST_TEST(copied == 5);
        BOOST_TEST(std::memcmp(dest_data, "hello", 5) == 0);
    }

    void
    testCrossSequenceCopy()
    {
        const_buffer buf1("abc", 3);
        const_buffer buf2("defg", 4);
        const_buffer buf3("hi", 2);
        char big_data[6] = {};
        char small_data[2] = {};
        mutable_buffer large_buf(big_data, 6);
        mutable_buffer small_buf(small_data, 2);
        // tag::buffer_copy_cross[]
        // Source: 3 buffers
        std::array<const_buffer, 3> src = {buf1, buf2, buf3};

        // Target: 2 buffers with different sizes
        std::array<mutable_buffer, 2> dst = {large_buf, small_buf};

        // Copies across buffer boundaries as needed
        std::size_t copied = buffer_copy(dst, src);
        // end::buffer_copy_cross[]
        BOOST_TEST(copied == 8);
        BOOST_TEST(std::memcmp(big_data, "abcdef", 6) == 0);
        BOOST_TEST(std::memcmp(small_data, "gh", 2) == 0);
    }

    void
    testReadFull()
    {
        auto [a, b] = test::make_stream_pair();
        char storage1[6] = {};
        char storage2[5] = {};
        std::array<mutable_buffer, 2> bufs = {
            mutable_buffer(storage1, 6),
            mutable_buffer(storage2, 5)};
        std::size_t got = 0;
        test::run_blocking()(read_full_driver(a, b, bufs, got));
        BOOST_TEST(got == 11);
        BOOST_TEST(std::memcmp(storage1, "hello ", 6) == 0);
        BOOST_TEST(std::memcmp(storage2, "world", 5) == 0);
    }

    void
    testWriteFull()
    {
        auto [a, b] = test::make_stream_pair();
        std::array<const_buffer, 2> bufs = {
            make_buffer("hello "sv),
            make_buffer("world"sv)};
        std::size_t put = 0;
        test::run_blocking()(write_full_driver(a, bufs, put));
        BOOST_TEST(put == 11);
        BOOST_TEST(b.data() == "hello world");
    }

    void
    run()
    {
        testBufferSize();
        testBufferEmpty();
        testBufferLength();
        testBufferCopy();
        testPartialCopy();
        testCrossSequenceCopy();
        testReadFull();
        testWriteFull();
    }
};

} // namespace

TEST_SUITE(algorithms_test, "boost.capy.doc.5e_algorithms");
