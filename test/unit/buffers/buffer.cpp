//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Test that header file is self-contained.
#include <boost/capy/buffers.hpp>

#include <boost/capy.hpp>
#include <array>
#include <ranges>
#include <span>

#include "test_buffers.hpp"

namespace boost {
namespace capy {

// Buffer Sequence Concepts

static_assert(  ConstBufferSequence<const_buffer>);
static_assert(  ConstBufferSequence<mutable_buffer>);
static_assert(! MutableBufferSequence<const_buffer>);
static_assert(  MutableBufferSequence<mutable_buffer>);

static_assert(  ConstBufferSequence<const_buffer const>);
static_assert(  ConstBufferSequence<mutable_buffer const>);
static_assert(! MutableBufferSequence<const_buffer const>);
static_assert(  MutableBufferSequence<mutable_buffer const>);

static_assert(  ConstBufferSequence<std::span<const_buffer>>);
static_assert(  ConstBufferSequence<std::span<mutable_buffer>>);
static_assert(! MutableBufferSequence<std::span<const_buffer>>);
static_assert(  MutableBufferSequence<std::span<mutable_buffer>>);

static_assert(  ConstBufferSequence<std::span<const_buffer const>>);
static_assert(  ConstBufferSequence<std::span<mutable_buffer const>>);
static_assert(! MutableBufferSequence<std::span<const_buffer const>>);
static_assert(  MutableBufferSequence<std::span<mutable_buffer const>>);

static_assert(  ConstBufferSequence<std::array<const_buffer const, 3>>);
static_assert(  ConstBufferSequence<std::array<mutable_buffer const, 3>>);
static_assert(! MutableBufferSequence<std::array<const_buffer const, 3>>);
static_assert(  MutableBufferSequence<std::array<mutable_buffer const, 3>>);

static_assert(  ConstBufferSequence<const_buffer[3]>);
static_assert(  ConstBufferSequence<mutable_buffer[3]>);
static_assert(! MutableBufferSequence<const_buffer[3]>);
static_assert(  MutableBufferSequence<mutable_buffer[3]>);

// std::ranges concepts for span<const_buffer>

static_assert(std::ranges::range<std::span<const_buffer>>);
static_assert(std::ranges::input_range<std::span<const_buffer>>);
static_assert(std::ranges::forward_range<std::span<const_buffer>>);
static_assert(std::ranges::bidirectional_range<std::span<const_buffer>>);
static_assert(std::ranges::random_access_range<std::span<const_buffer>>);
static_assert(std::ranges::contiguous_range<std::span<const_buffer>>);

// std::ranges concepts for span<mutable_buffer>

static_assert(std::ranges::range<std::span<mutable_buffer>>);
static_assert(std::ranges::input_range<std::span<mutable_buffer>>);
static_assert(std::ranges::forward_range<std::span<mutable_buffer>>);
static_assert(std::ranges::bidirectional_range<std::span<mutable_buffer>>);
static_assert(std::ranges::random_access_range<std::span<mutable_buffer>>);
static_assert(std::ranges::contiguous_range<std::span<mutable_buffer>>);

// std::ranges concepts for array<const_buffer, N>

static_assert(std::ranges::range<std::array<const_buffer, 3>>);
static_assert(std::ranges::input_range<std::array<const_buffer, 3>>);
static_assert(std::ranges::forward_range<std::array<const_buffer, 3>>);
static_assert(std::ranges::bidirectional_range<std::array<const_buffer, 3>>);
static_assert(std::ranges::random_access_range<std::array<const_buffer, 3>>);
static_assert(std::ranges::contiguous_range<std::array<const_buffer, 3>>);

// std::ranges concepts for array<mutable_buffer, N>

static_assert(std::ranges::range<std::array<mutable_buffer, 3>>);
static_assert(std::ranges::input_range<std::array<mutable_buffer, 3>>);
static_assert(std::ranges::forward_range<std::array<mutable_buffer, 3>>);
static_assert(std::ranges::bidirectional_range<std::array<mutable_buffer, 3>>);
static_assert(std::ranges::random_access_range<std::array<mutable_buffer, 3>>);
static_assert(std::ranges::contiguous_range<std::array<mutable_buffer, 3>>);

// std::ranges concepts for 2-element buffer arrays

static_assert(std::ranges::range<std::array<const_buffer, 2>>);
static_assert(std::ranges::bidirectional_range<std::array<const_buffer, 2>>);
static_assert(std::ranges::random_access_range<std::array<const_buffer, 2>>);

static_assert(std::ranges::range<std::array<mutable_buffer, 2>>);
static_assert(std::ranges::bidirectional_range<std::array<mutable_buffer, 2>>);
static_assert(std::ranges::random_access_range<std::array<mutable_buffer, 2>>);

// std::views producing valid ConstBufferSequence

using span_cb = std::span<const_buffer>;
using span_mb = std::span<mutable_buffer>;

// take_view preserves bidirectional + value type
using take_cb = decltype(std::declval<span_cb>() | std::views::take(1));
static_assert(std::ranges::bidirectional_range<take_cb>);
static_assert(std::is_convertible_v<std::ranges::range_value_t<take_cb>, const_buffer>);
static_assert(ConstBufferSequence<take_cb>);

using take_mb = decltype(std::declval<span_mb>() | std::views::take(1));
static_assert(std::ranges::bidirectional_range<take_mb>);
static_assert(MutableBufferSequence<take_mb>);

// drop_view preserves bidirectional + value type
using drop_cb = decltype(std::declval<span_cb>() | std::views::drop(1));
static_assert(std::ranges::bidirectional_range<drop_cb>);
static_assert(ConstBufferSequence<drop_cb>);

using drop_mb = decltype(std::declval<span_mb>() | std::views::drop(1));
static_assert(std::ranges::bidirectional_range<drop_mb>);
static_assert(MutableBufferSequence<drop_mb>);

// reverse_view preserves bidirectional + value type
using rev_cb = decltype(std::declval<span_cb>() | std::views::reverse);
static_assert(std::ranges::bidirectional_range<rev_cb>);
static_assert(ConstBufferSequence<rev_cb>);

using rev_mb = decltype(std::declval<span_mb>() | std::views::reverse);
static_assert(std::ranges::bidirectional_range<rev_mb>);
static_assert(MutableBufferSequence<rev_mb>);

// filter_view is bidirectional but not const-iterable;
// it satisfies ConstBufferSequence for non-const lvalue
// but the buffer APIs take const& so filter_view cannot
// be used directly with buffer_size, buffer_copy, etc.
using filt_cb = decltype(
    std::declval<span_cb>()
        | std::views::filter([](const_buffer b) { return b.size() > 0; }));
static_assert(std::ranges::bidirectional_range<filt_cb>);
static_assert(ConstBufferSequence<filt_cb>);
static_assert(!ConstBufferSequence<filt_cb const>);

namespace {

// test fixture
template<class T>
struct fixt;

// VFALCO This is a quick hack, need to fix make_buffer
const_buffer buf(std::string_view s) noexcept
{
    return const_buffer(s.data(), s.size());
}

template<>
struct fixt<const_buffer>
{
    const_buffer t;
    fixt(std::string_view pat)
        : t(pat.data(), pat.size())
    {
    }
};

template<>
struct fixt<mutable_buffer>
{
    char data[64];
    mutable_buffer t;
    fixt(std::string_view pat)
        : t(data, pat.size())
    {
        BOOST_CAPY_ASSERT(pat.size()<=sizeof(data));
        pat.copy(data, pat.size());
    }
};

template<>
struct fixt<std::array<const_buffer, 2>>
{
    std::array<const_buffer, 2> t;
    fixt(std::string_view pat)
        : t{{ {buf(pat.substr(0, 3))}, {buf(pat.substr(3))} }}
    {
    }
};

template<>
struct fixt<std::array<mutable_buffer, 2>>
{
    char data[64];
    std::array<mutable_buffer, 2> t;
    fixt(std::string_view pat)
        : t{{{data,3}, {data+3, pat.size()-3}}}
    {
        BOOST_CAPY_ASSERT(pat.size()>=3);
        BOOST_CAPY_ASSERT(pat.size()<=sizeof(data));
        pat.copy(data, pat.size());
    }
};

template<>
struct fixt<std::span<const_buffer,3>>
{
    const_buffer a[3];
    std::span<const_buffer,3> t;
    fixt(std::string_view pat)
        : a{ buf(pat.substr(0, 3)),
             buf(pat.substr(3, pat.size()-8)),
             buf(pat.substr(pat.size()-5)) }
        , t(a)
    {
    }
};

template<>
struct fixt<std::span<mutable_buffer,3>>
{
    char data[64];
    mutable_buffer a[3];
    std::span<mutable_buffer,3> t;
    fixt(std::string_view pat)
        : t([&]
            {
                a[0] = { data+0, 3 };
                a[1] = { data+3, pat.size()-8 };
                a[2] = { data+pat.size()-5, 5 };
                return std::span<mutable_buffer,3>(a);
            }())
    {
        BOOST_CAPY_ASSERT(pat.size()>=8);
        BOOST_CAPY_ASSERT(pat.size()<=sizeof(data));
        pat.copy(data, pat.size());
    }
};

template<>
struct fixt<std::array<const_buffer,3>>
{
    std::array<const_buffer,3> t;
    fixt(std::string_view pat)
        : t{{ buf(pat.substr(0, 3)),
              buf(pat.substr(3, pat.size()-8)),
              buf(pat.substr(pat.size()-5)) }}
    {
    }
};

template<>
struct fixt<std::array<mutable_buffer,3>>
{
    char data[64];
    std::array<mutable_buffer,3> t;
    fixt(std::string_view pat)
        : t([&]
            {
                return std::array<mutable_buffer,3>{{
                    { data+0, 3 },
                    { data+3, pat.size()-8 },
                    { data+pat.size()-5, 5 }}};
            }())
    {
        BOOST_CAPY_ASSERT(pat.size()>=8);
        BOOST_CAPY_ASSERT(pat.size()<=sizeof(data));
        pat.copy(data, pat.size());
    }
};

template<>
struct fixt<const_buffer[3]>
{
    const_buffer t[3];
    fixt(std::string_view pat)
        : t{ buf(pat.substr(0, 3)),
             buf(pat.substr(3, pat.size()-8)),
             buf(pat.substr(pat.size()-5)) }
    {
    }
};

template<>
struct fixt<mutable_buffer[3]>
{
    char data[64];
    mutable_buffer t[3];
    fixt(std::string_view pat)
        : t{ { data+0, 3 },
             { data+3, pat.size()-8 },
             { data+pat.size()-5, 5 }}
    {
        BOOST_CAPY_ASSERT(pat.size()>=8);
        BOOST_CAPY_ASSERT(pat.size()<=sizeof(data));
        pat.copy(data, pat.size());
    }
};

} // (anon)

struct buffer_test
{
    template<class T>
    void testBuffer()
    {
        std::string_view pat = "0123456789abcdef";

        // buffer_size()
        {
            fixt<T> f(pat);
            BOOST_TEST_EQ(buffer_size(f.t), pat.size());
        }

        // copy()
        {
            char data[64];
            mutable_buffer mb(data, sizeof(data));
            fixt<T> f(pat);
            auto const n = buffer_copy(mb, f.t);
            mb = mutable_buffer(mb.data(), n);
            BOOST_TEST_EQ(test::make_string(mb), pat);
        }
    }

    void testBuffers()
    {
        testBuffer<const_buffer>();
        testBuffer<mutable_buffer>();
        testBuffer<std::array<const_buffer, 2>>();
        testBuffer<std::array<mutable_buffer, 2>>();
        testBuffer<std::span<const_buffer,3>>();
        testBuffer<std::span<mutable_buffer,3>>();
        testBuffer<std::array<const_buffer,3>>();
        testBuffer<std::array<mutable_buffer,3>>();
        testBuffer<const_buffer[3]>();
        testBuffer<mutable_buffer[3]>();
    }

    //--------------------------------------------

    void testConstBuffer()
    {
        // const_buffer()
        BOOST_TEST_EQ(const_buffer().size(), 0);
        BOOST_TEST_EQ(const_buffer().data(), nullptr);

        // const_buffer(void const*, size_t)
        {
            char const* p = "12345";
            const_buffer b( p, 5 );
            BOOST_TEST_EQ(b.data(), p);
            BOOST_TEST_EQ(b.size(), 5);
        }

        // const_buffer(const_buffer)
        {
            char const* p = "12345";
            const_buffer b0( p, 5 );
            const_buffer b(b0);
            BOOST_TEST_EQ(b.data(), p);
            BOOST_TEST_EQ(b.size(), 5);
        }

        // const_buffer(mutable_buffer)
        {
            char buf[6] = "12345";
            mutable_buffer b0( buf, 5 );
            const_buffer b(b0);
            BOOST_TEST_EQ(b.data(), buf);
            BOOST_TEST_EQ(b.size(), 5);
        }

        // operator=(const_buffer)
        {
            char const* p = "12345";
            const_buffer b;
            b = const_buffer(p, 5);
            BOOST_TEST_EQ(b.data(), p);
            BOOST_TEST_EQ(b.size(), 5);
        }

        // std::span
        {
            const_buffer b[3] = {
                const_buffer("123", 3),
                const_buffer("456", 3),
                const_buffer("789", 3)
            };
            std::span<const_buffer const> bs(&b[0], 3);
            test::check_sequence(bs, "123456789");
        }

        // operator+= advances and clamps to size()
        {
            char const* p = "12345";
            const_buffer b(p, 5);
            b += 2;
            BOOST_TEST_EQ(b.data(), p + 2);
            BOOST_TEST_EQ(b.size(), 3);
            b += 100;  // over-advance is clamped
            BOOST_TEST_EQ(b.size(), 0);
        }
    }

    void testMutableBuffer()
    {
        // mutable_buffer()
        BOOST_TEST_EQ(mutable_buffer().size(), 0);

        // mutable_buffer(void const*, size_t)
        {
            char p[6] = "12345";
            mutable_buffer b( p, 5 );
            BOOST_TEST_EQ(b.data(), p);
            BOOST_TEST_EQ(b.size(), 5);
        }

        // mutable_buffer(mutable_buffer)
        {
            char p[6] = "12345";
            mutable_buffer b0( p, 5 );
            mutable_buffer b(b0);
            BOOST_TEST_EQ(b.data(), p);
            BOOST_TEST_EQ(b.size(), 5);
        }

        // mutable_buffer(mutable_buffer)
        {
            char buf[6] = "12345";
            mutable_buffer b0( buf, 5 );
            mutable_buffer b(b0);
            BOOST_TEST_EQ(b.data(), buf);
            BOOST_TEST_EQ(b.size(), 5);
        }

        // operator=(mutable_buffer)
        {
            char p[6] = "12345";
            mutable_buffer b;
            b = mutable_buffer(p, 5);
            BOOST_TEST_EQ(b.data(), p);
            BOOST_TEST_EQ(b.size(), 5);
        }

        // std::span
        {
            char c[10] = "123456789";
            mutable_buffer b[3] = {
                mutable_buffer(c+0, 3),
                mutable_buffer(c+3, 3),
                mutable_buffer(c+6, 3)
            };
            std::span<mutable_buffer const> bs(&b[0], 3);
            test::check_sequence(bs, "123456789");
        }

        // operator+= advances and clamps to size()
        {
            char p[6] = "12345";
            mutable_buffer b(p, 5);
            b += 2;
            BOOST_TEST_EQ(b.data(), p + 2);
            BOOST_TEST_EQ(b.size(), 3);
            b += 100;  // over-advance is clamped
            BOOST_TEST_EQ(b.size(), 0);
        }
    }

    void testSize()
    {
        char data[9];
        for(std::size_t i = 0; i < 3; ++i)
        for(std::size_t j = 0; j < 3; ++j)
        for(std::size_t k = 0; k < 3; ++k)
        {
            const_buffer cb[3] = {
                { data, i },
                { data + i, j },
                { data + i + j, k }
            };
            std::span<const_buffer const> s(cb, 3);
            BOOST_TEST_EQ(
                buffer_size(s), i + j + k);
        }
    }

    void testEmpty()
    {
        char data[9] = "12345678";

        // empty const_buffer
        BOOST_TEST(buffer_empty(const_buffer()));
        BOOST_TEST(buffer_empty(const_buffer(data, 0)));

        // non-empty const_buffer
        BOOST_TEST(! buffer_empty(const_buffer(data, 1)));
        BOOST_TEST(! buffer_empty(const_buffer(data, 5)));

        // empty mutable_buffer
        BOOST_TEST(buffer_empty(mutable_buffer()));
        BOOST_TEST(buffer_empty(mutable_buffer(data, 0)));

        // non-empty mutable_buffer
        BOOST_TEST(! buffer_empty(mutable_buffer(data, 1)));
        BOOST_TEST(! buffer_empty(mutable_buffer(data, 5)));

        // empty buffer_pair (both empty)
        {
            std::array<const_buffer, 2> cbp{{ {data, 0}, {data, 0} }};
            BOOST_TEST(buffer_empty(cbp));
        }

        // non-empty buffer_pair (one non-empty)
        {
            std::array<const_buffer, 2> cbp{{ {data, 0}, {data, 3} }};
            BOOST_TEST(! buffer_empty(cbp));
        }
        {
            std::array<const_buffer, 2> cbp{{ {data, 3}, {data, 0} }};
            BOOST_TEST(! buffer_empty(cbp));
        }

        // buffer sequence: all empty
        {
            const_buffer cb[3] = {
                { data, 0 },
                { data, 0 },
                { data, 0 }
            };
            std::span<const_buffer const> s(cb, 3);
            BOOST_TEST(buffer_empty(s));
        }

        // buffer sequence: some empty, one non-empty
        {
            const_buffer cb[3] = {
                { data, 0 },
                { data, 1 },
                { data, 0 }
            };
            std::span<const_buffer const> s(cb, 3);
            BOOST_TEST(! buffer_empty(s));
        }

        // buffer sequence: none empty
        {
            const_buffer cb[3] = {
                { data, 1 },
                { data, 2 },
                { data, 3 }
            };
            std::span<const_buffer const> s(cb, 3);
            BOOST_TEST(! buffer_empty(s));
        }

        // empty span (zero elements)
        {
            std::span<const_buffer const> s;
            BOOST_TEST(buffer_empty(s));
        }
    }

    void testViews()
    {
        char data[9] = "ABCDEFGH";
        const_buffer cb[3] = {
            { data, 3 },
            { data + 3, 3 },
            { data + 6, 2 }
        };
        std::span<const_buffer> bufs(cb, 3);

        // take: first 2 buffers = "ABCDEF"
        {
            auto v = bufs | std::views::take(2);
            BOOST_TEST_EQ(buffer_size(v), 6u);
            BOOST_TEST_EQ(test::make_string(v), "ABCDEF");
        }

        // drop: skip first buffer = "DEFGH"
        {
            auto v = bufs | std::views::drop(1);
            BOOST_TEST_EQ(buffer_size(v), 5u);
            BOOST_TEST_EQ(test::make_string(v), "DEFGH");
        }

        // reverse: buffers in reverse order = "GHDEFABC"
        {
            auto v = bufs | std::views::reverse;
            BOOST_TEST_EQ(buffer_size(v), 8u);
            BOOST_TEST_EQ(test::make_string(v), "GHDEFABC");
        }

        // take + drop composition = middle buffer only
        {
            auto v = bufs | std::views::drop(1) | std::views::take(1);
            BOOST_TEST_EQ(buffer_size(v), 3u);
            BOOST_TEST_EQ(test::make_string(v), "DEF");
        }
    }

    void run()
    {
        testBuffers();
        testConstBuffer();
        testMutableBuffer();
        testSize();
        testEmpty();
        testViews();
    }
};

TEST_SUITE(
    buffer_test,
    "boost.capy.buffers.buffer");

} // capy
} // boost
