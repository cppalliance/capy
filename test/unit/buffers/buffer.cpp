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
#include <boost/core/span.hpp>
#include <array>

#include "test_buffers.hpp"

/*
#if defined(__cpp_lib_span) && (__cpp_lib_span >= 202002L)
#define HAVE_STD_SPAN 1
#include <span>
#endif
*/

namespace boost {
namespace capy {

static_assert(  ConstBufferSequence<const_buffer>);
static_assert(  ConstBufferSequence<mutable_buffer>);
static_assert(! MutableBufferSequence<const_buffer>);
static_assert(  MutableBufferSequence<mutable_buffer>);

static_assert(  ConstBufferSequence<const_buffer const>);
static_assert(  ConstBufferSequence<mutable_buffer const>);
static_assert(! MutableBufferSequence<const_buffer const>);
static_assert(  MutableBufferSequence<mutable_buffer const>);

static_assert(  ConstBufferSequence<span<const_buffer>>);
static_assert(  ConstBufferSequence<span<mutable_buffer>>);
static_assert(! MutableBufferSequence<span<const_buffer>>);
static_assert(  MutableBufferSequence<span<mutable_buffer>>);

static_assert(  ConstBufferSequence<span<const_buffer const>>);
static_assert(  ConstBufferSequence<span<mutable_buffer const>>);
static_assert(! MutableBufferSequence<span<const_buffer const>>);
static_assert(  MutableBufferSequence<span<mutable_buffer const>>);

static_assert(  ConstBufferSequence<std::array<const_buffer const, 3>>);
static_assert(  ConstBufferSequence<std::array<mutable_buffer const, 3>>);
static_assert(! MutableBufferSequence<std::array<const_buffer const, 3>>);
static_assert(  MutableBufferSequence<std::array<mutable_buffer const, 3>>);

static_assert(  ConstBufferSequence<const_buffer[3]>);
static_assert(  ConstBufferSequence<mutable_buffer[3]>);
static_assert(! MutableBufferSequence<const_buffer[3]>);
static_assert(  MutableBufferSequence<mutable_buffer[3]>);

namespace {

// test fixture
template<class T>
struct fixt;

// VFALCO This is a quick hack, need to fix make_buffer
const_buffer buf(core::string_view s) noexcept
{
    return const_buffer(s.data(), s.size());
}

template<>
struct fixt<const_buffer>
{
    const_buffer t;
    fixt(core::string_view pat)
        : t(pat.data(), pat.size())
    {
    }
};

template<>
struct fixt<mutable_buffer>
{
    char data[64];
    mutable_buffer t;
    fixt(core::string_view pat)
        : t(data, pat.size())
    {
        BOOST_ASSERT(pat.size()<=sizeof(data));
        pat.copy(data, pat.size());
    }
};

template<>
struct fixt<const_buffer_pair>
{
    const_buffer_pair t;
    fixt(core::string_view pat)
        : t{{ {buf(pat.substr(0, 3))}, {buf(pat.substr(3))} }}
    {
    }
};

template<>
struct fixt<mutable_buffer_pair>
{
    char data[64];
    mutable_buffer_pair t;
    fixt(core::string_view pat)
        : t{{{data,3}, {data+3, pat.size()-3}}}
    {
        BOOST_ASSERT(pat.size()>=3);
        BOOST_ASSERT(pat.size()<=sizeof(data));
        pat.copy(data, pat.size());
    }
};

template<>
struct fixt<span<const_buffer,3>>
{
    const_buffer a[3];
    span<const_buffer,3> t;
    fixt(core::string_view pat)
        : a{ buf(pat.substr(0, 3)),
             buf(pat.substr(3, pat.size()-8)),
             buf(pat.substr(pat.size()-5)) }
        , t(a)
    {
    }
};

template<>
struct fixt<span<mutable_buffer,3>>
{
    char data[64];
    mutable_buffer a[3];
    span<mutable_buffer,3> t;
    fixt(core::string_view pat)
        : t([&]
            {
                a[0] = { data+0, 3 };
                a[1] = { data+3, pat.size()-8 };
                a[2] = { data+pat.size()-5, 5 };
                return span<mutable_buffer,3>(a);
            }())
    {
        BOOST_ASSERT(pat.size()>=8);
        BOOST_ASSERT(pat.size()<=sizeof(data));
        pat.copy(data, pat.size());
    }
};

template<>
struct fixt<std::array<const_buffer,3>>
{
    std::array<const_buffer,3> t;
    fixt(core::string_view pat)
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
    fixt(core::string_view pat)
        : t([&]
            {
                return std::array<mutable_buffer,3>{{
                    { data+0, 3 },
                    { data+3, pat.size()-8 },
                    { data+pat.size()-5, 5 }}};
            }())
    {
        BOOST_ASSERT(pat.size()>=8);
        BOOST_ASSERT(pat.size()<=sizeof(data));
        pat.copy(data, pat.size());
    }
};

template<>
struct fixt<const_buffer[3]>
{
    const_buffer t[3];
    fixt(core::string_view pat)
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
    fixt(core::string_view pat)
        : t{ { data+0, 3 },
             { data+3, pat.size()-8 },
             { data+pat.size()-5, 5 }}
    {
        BOOST_ASSERT(pat.size()>=8);
        BOOST_ASSERT(pat.size()<=sizeof(data));
        pat.copy(data, pat.size());
    }
};

} // (anon)

struct buffer_test
{
    template<class T>
    void testBuffer()
    {
        core::string_view pat = "0123456789abcdef";

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
            keep_prefix(mb, copy(mb, f.t));
            BOOST_TEST_EQ(test::make_string(mb), pat);
        }
    }

    void testBuffers()
    {
        testBuffer<const_buffer>();
        testBuffer<mutable_buffer>();
        testBuffer<const_buffer_pair>();
        testBuffer<mutable_buffer_pair>();
        testBuffer<span<const_buffer,3>>();
        testBuffer<span<mutable_buffer,3>>();
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

        // boost::span
        {
            const_buffer b[3] = {
                const_buffer("123", 3),
                const_buffer("456", 3),
                const_buffer("789", 3)
            };
            span<const_buffer const> bs(&b[0], 3);
            test::check_sequence(bs, "123456789");
        }

        // std::span
        {
        #if HAVE_STD_SPAN
            static_assert(ConstBufferSequence<
                std::span<const_buffer const>>);
            const_buffer b[3] = {
                const_buffer("123", 3),
                const_buffer("456", 3),
                const_buffer("789", 3)
            };
            std::span<const_buffer const> bs(&b[0], 3);
            test::check_sequence(bs, "123456789");
        #endif
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

        // boost::span
        {
            char c[10] = "123456789";
            mutable_buffer b[3] = {
                mutable_buffer(c+0, 3),
                mutable_buffer(c+3, 3),
                mutable_buffer(c+6, 3)
            };
            span<mutable_buffer const> bs(&b[0], 3);
            test::check_sequence(bs, "123456789");
        }

        // std::span
        {
        #if HAVE_STD_SPAN
            static_assert(ConstBufferSequence<
                std::span<const_buffer const>>);
            const_buffer b[3] = {
                const_buffer("123", 3),
                const_buffer("456", 3),
                const_buffer("789", 3)
            };
            std::span<const_buffer const> bs(&b[0], 3);
            test::check_sequence(bs, "123456789");
        #endif
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
            span<const_buffer const> s(cb, 3);
            BOOST_TEST_EQ(
                buffer_size(s), i + j + k);
        }
    }

    void run()
    {
        testBuffers();
        testConstBuffer();
        testMutableBuffer();
        testSize();
    }
};

TEST_SUITE(
    buffer_test,
    "boost.capy.buffers.buffer");

} // capy
} // boost

#if 0
const_buffer
mutable_buffer
const_buffer_pair
mutable_buffer_pair
span<const_buffer,3>
span<mutable_buffer,3>
std::array<const_buffer,3>
std::array<mutable_buffer,3>
const_buffer[3]
mutable_buffer[3]
#endif
