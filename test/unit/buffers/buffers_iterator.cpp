//
// Copyright (c) 2026 Steve Gerbino
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Test that the header is self-contained.
#include <boost/capy/buffers/buffers_iterator.hpp>

#include <algorithm>
#include <array>
#include <concepts>
#include <iterator>
#include <list>
#include <ranges>
#include <string>
#include <string_view>
#include <vector>

#include "test_suite.hpp"

namespace boost {
namespace capy {

namespace {

using cseq = std::array<const_buffer, 5>;
using mseq = std::vector<mutable_buffer>;

static_assert(std::random_access_iterator<buffers_iterator<cseq>>);
static_assert(std::random_access_iterator<buffers_iterator<mseq>>);
static_assert(std::random_access_iterator<buffers_iterator<const_buffer>>);
static_assert(std::random_access_iterator<
    buffers_iterator<std::list<const_buffer>>>);
static_assert(std::same_as<
    std::iter_reference_t<buffers_iterator<cseq>>, char const&>);
static_assert(std::same_as<
    std::iter_reference_t<buffers_iterator<mseq>>, char&>);
static_assert(std::same_as<
    std::iter_reference_t<buffers_iterator<mseq, unsigned char>>,
    unsigned char&>);

// The iterator would point into a destroyed temporary
template<class T>
constexpr bool begin_accepts_rvalue =
    requires { buffers_begin(T{}); };
template<class T>
constexpr bool end_accepts_rvalue =
    requires { buffers_end(T{}); };
static_assert(! begin_accepts_rvalue<cseq>);
static_assert(! end_accepts_rvalue<cseq>);

// A range whose end is a sentinel is a buffer sequence, but the iterator
// needs the end to be an iterator it can walk back from
struct non_empty
{
    bool operator()(const_buffer b) const noexcept { return b.size() > 0; }
};
using sentinel_seq = decltype(std::views::take_while(
    std::declval<std::vector<const_buffer>&>(), non_empty{}));
static_assert(ConstBufferSequence<sentinel_seq>);
static_assert(! std::ranges::common_range<sentinel_seq const>);

template<class T>
constexpr bool has_byte_iterator =
    requires(T const& t) { buffers_begin(t); buffers_end(t); };
static_assert(! has_byte_iterator<sentinel_seq>);
static_assert(has_byte_iterator<cseq>);
static_assert(has_byte_iterator<const_buffer>);

struct buffers_iterator_test
{
    // "abc" | "" | "de" | "" | "fghij", with empty buffers to skip
    static constexpr std::string_view text = "abcdefghij";

    static
    cseq
    make_cseq()
    {
        char const* p = text.data();
        return {{
            const_buffer(p, 3),
            const_buffer(p + 3, 0),
            const_buffer(p + 3, 2),
            const_buffer(p + 5, 0),
            const_buffer(p + 5, 5) }};
    }

    void
    testForward()
    {
        auto const bs = make_cseq();
        std::string out(buffers_begin(bs), buffers_end(bs));
        BOOST_TEST_EQ(out, text);
        BOOST_TEST_EQ(buffers_end(bs) - buffers_begin(bs),
            static_cast<std::ptrdiff_t>(text.size()));
    }

    void
    testBackward()
    {
        auto const bs = make_cseq();
        std::string out;
        auto const first = buffers_begin(bs);
        for(auto it = buffers_end(bs); it != first;)
            out.push_back(*--it);
        std::reverse(out.begin(), out.end());
        BOOST_TEST_EQ(out, text);
    }

    void
    testRandomAccess()
    {
        auto const bs = make_cseq();
        auto const first = buffers_begin(bs);
        auto const n = static_cast<std::ptrdiff_t>(text.size());
        for(std::ptrdiff_t i = 0; i <= n; ++i)
        {
            auto a = first + i;
            BOOST_TEST_EQ(a - first, i);
            if(i < n)
            {
                BOOST_TEST_EQ(*a, text[i]);
                BOOST_TEST_EQ(first[i], text[i]);
            }
            for(std::ptrdiff_t j = 0; j <= n; ++j)
            {
                auto b = a + (j - i);
                BOOST_TEST_EQ(b - first, j);
                BOOST_TEST_EQ(b - a, j - i);
                BOOST_TEST_EQ(b < a, j < i);
                BOOST_TEST_EQ(b == a, j == i);
                b -= j - i;
                BOOST_TEST(b == a);
            }
        }
        BOOST_TEST(first + n == buffers_end(bs));
        BOOST_TEST(buffers_end(bs) - n == first);
    }

    void
    testSearchAcrossBuffers()
    {
        auto const bs = make_cseq();
        std::string_view const needle = "cdef";
        auto it = std::search(
            buffers_begin(bs), buffers_end(bs),
            needle.begin(), needle.end());
        BOOST_TEST_EQ(it - buffers_begin(bs), 2);
    }

    void
    testSingleBuffer()
    {
        const_buffer const b(text.data(), text.size());
        std::string out(buffers_begin(b), buffers_end(b));
        BOOST_TEST_EQ(out, text);
    }

    void
    testEmpty()
    {
        std::array<const_buffer, 2> const none{};
        BOOST_TEST(buffers_begin(none) == buffers_end(none));
        BOOST_TEST_EQ(buffers_end(none) - buffers_begin(none), 0);

        mseq const nothing;
        BOOST_TEST(buffers_begin(nothing) == buffers_end(nothing));
    }

    void
    testMutable()
    {
        char a[3] = {};
        char b[4] = {};
        mseq const bs{
            mutable_buffer(a, sizeof(a)),
            mutable_buffer(b, 0),
            mutable_buffer(b, sizeof(b)) };
        std::string_view const src = "1234567";
        std::copy(src.begin(), src.end(), buffers_begin(bs));
        BOOST_TEST_EQ(std::string_view(a, 3), "123");
        BOOST_TEST_EQ(std::string_view(b, 4), "4567");

        buffers_begin(bs)[4] = 'x';
        BOOST_TEST_EQ(b[1], 'x');
    }

    // The sequence's own iterator is only bidirectional
    void
    testList()
    {
        auto const arr = make_cseq();
        std::list<const_buffer> const bs(arr.begin(), arr.end());
        std::string out(buffers_begin(bs), buffers_end(bs));
        BOOST_TEST_EQ(out, text);

        std::string back;
        auto const first = buffers_begin(bs);
        for(auto it = buffers_end(bs); it != first;)
            back.push_back(*--it);
        std::reverse(back.begin(), back.end());
        BOOST_TEST_EQ(back, text);

        auto const n = static_cast<std::ptrdiff_t>(text.size());
        for(std::ptrdiff_t i = 0; i < n; ++i)
        {
            BOOST_TEST_EQ(first[i], text[i]);
            BOOST_TEST_EQ((buffers_end(bs) - (n - i))[0], text[i]);
        }
    }

    void
    testSingleMutable()
    {
        char data[4] = {};
        mutable_buffer const b(data, sizeof(data));
        auto it = buffers_begin(b);
        static_assert(std::same_as<decltype(*it), char&>);
        *it = 'a';
        it[3] = 'd';
        BOOST_TEST_EQ(data[0], 'a');
        BOOST_TEST_EQ(data[3], 'd');
        BOOST_TEST_EQ(buffers_end(b) - it, 4);
    }

    void
    testValueInitialized()
    {
        buffers_iterator<cseq> a{};
        buffers_iterator<cseq> b{};
        BOOST_TEST(a == b);
        BOOST_TEST_EQ(a - b, 0);
    }

    void
    testByteType()
    {
        auto const bs = make_cseq();
        auto it = buffers_iterator<cseq, unsigned char>::begin(bs);
        BOOST_TEST_EQ(it[3], static_cast<unsigned char>('d'));
    }

    void
    run()
    {
        testForward();
        testBackward();
        testRandomAccess();
        testSearchAcrossBuffers();
        testSingleBuffer();
        testEmpty();
        testMutable();
        testList();
        testSingleMutable();
        testValueInitialized();
        testByteType();
    }
};

TEST_SUITE(buffers_iterator_test, "boost.capy.buffers.buffers_iterator");

} // (anon)

} // namespace capy
} // namespace boost
