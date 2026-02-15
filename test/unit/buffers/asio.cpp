//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#if BOOST_CAPY_HAS_ASIO

#include <boost/capy/buffers.hpp>
#include <boost/capy/buffers/asio.hpp>
#include <boost/capy/buffers/buffer_array.hpp>
#include <boost/capy/buffers/buffer_copy.hpp>
#include <boost/asio/buffer.hpp>

#include <array>
#include <span>
#include <type_traits>

#include "test_buffers.hpp"

namespace boost {
namespace capy {

//------------------------------------------------
// to_asio result satisfies asio buffer sequence traits

using to_asio_const_t = decltype(
    to_asio(std::declval<const_buffer_array<4> const&>()));
using to_asio_mutable_t = decltype(
    to_asio(std::declval<mutable_buffer_array<4> const&>()));

static_assert(asio::is_const_buffer_sequence<to_asio_const_t>::value);
static_assert(asio::is_const_buffer_sequence<to_asio_mutable_t>::value);
static_assert(!asio::is_mutable_buffer_sequence<to_asio_const_t>::value);
static_assert(asio::is_mutable_buffer_sequence<to_asio_mutable_t>::value);

//------------------------------------------------
// from_asio result satisfies capy buffer sequence concepts

using from_asio_const_t = decltype(
    from_asio(std::declval<std::span<asio::const_buffer const>>()));
using from_asio_mutable_t = decltype(
    from_asio(std::declval<std::span<asio::mutable_buffer const>>()));

static_assert(ConstBufferSequence<from_asio_const_t>);
static_assert(ConstBufferSequence<from_asio_mutable_t>);
static_assert(!MutableBufferSequence<from_asio_const_t>);
static_assert(MutableBufferSequence<from_asio_mutable_t>);

//------------------------------------------------
// to_asio iterator dereferences to asio buffer types

static_assert(std::is_same_v<
    to_asio_const_t::const_iterator::value_type,
    asio::const_buffer>);
static_assert(std::is_same_v<
    to_asio_mutable_t::const_iterator::value_type,
    asio::mutable_buffer>);

//------------------------------------------------
// from_asio iterator dereferences to capy buffer types

static_assert(std::is_same_v<
    from_asio_const_t::const_iterator::value_type,
    capy::const_buffer>);
static_assert(std::is_same_v<
    from_asio_mutable_t::const_iterator::value_type,
    capy::mutable_buffer>);

//------------------------------------------------

struct asio_test
{
    void
    test_to_asio_const()
    {
        char const data[] = "Hello";
        const_buffer cb(data, 5);

        auto adapted = to_asio(cb);
        auto it = adapted.begin();
        auto end = adapted.end();
        BOOST_TEST(it != end);

        asio::const_buffer ab = *it;
        BOOST_TEST_EQ(ab.data(), cb.data());
        BOOST_TEST_EQ(ab.size(), cb.size());

        ++it;
        BOOST_TEST(it == end);
    }

    void
    test_to_asio_mutable()
    {
        char data[] = "Hello";
        mutable_buffer mb(data, 5);

        auto adapted = to_asio(mb);
        auto it = adapted.begin();
        auto end = adapted.end();
        BOOST_TEST(it != end);

        asio::mutable_buffer ab = *it;
        BOOST_TEST_EQ(ab.data(), mb.data());
        BOOST_TEST_EQ(ab.size(), mb.size());

        ++it;
        BOOST_TEST(it == end);
    }

    void
    test_to_asio_array()
    {
        char d1[] = "abc";
        char d2[] = "defgh";
        mutable_buffer_array<4> bufs;
        bufs = mutable_buffer_array<4>(
            std::array<mutable_buffer, 2>{{
                mutable_buffer(d1, 3),
                mutable_buffer(d2, 5)
            }});

        auto adapted = to_asio(bufs);
        auto it = adapted.begin();
        auto end = adapted.end();

        asio::mutable_buffer ab0 = *it;
        BOOST_TEST_EQ(ab0.data(), static_cast<void*>(d1));
        BOOST_TEST_EQ(ab0.size(), 3u);
        ++it;

        asio::mutable_buffer ab1 = *it;
        BOOST_TEST_EQ(ab1.data(), static_cast<void*>(d2));
        BOOST_TEST_EQ(ab1.size(), 5u);
        ++it;

        BOOST_TEST(it == end);
    }

    void
    test_from_asio_const()
    {
        char const data[] = "Hello";
        asio::const_buffer ab(data, 5);
        std::span<asio::const_buffer const> sp(&ab, 1);

        auto adapted = from_asio(sp);
        auto it = adapted.begin();
        auto end = adapted.end();
        BOOST_TEST(it != end);

        const_buffer cb = *it;
        BOOST_TEST_EQ(cb.data(), ab.data());
        BOOST_TEST_EQ(cb.size(), ab.size());

        ++it;
        BOOST_TEST(it == end);
    }

    void
    test_from_asio_mutable()
    {
        char data[] = "Hello";
        asio::mutable_buffer ab(data, 5);
        std::span<asio::mutable_buffer const> sp(&ab, 1);

        auto adapted = from_asio(sp);
        auto it = adapted.begin();
        auto end = adapted.end();
        BOOST_TEST(it != end);

        mutable_buffer mb = *it;
        BOOST_TEST_EQ(mb.data(), ab.data());
        BOOST_TEST_EQ(mb.size(), ab.size());

        ++it;
        BOOST_TEST(it == end);
    }

    void
    test_from_asio_array()
    {
        char d1[] = "abc";
        char d2[] = "defgh";
        std::array<asio::mutable_buffer, 2> asio_bufs{{
            asio::mutable_buffer(d1, 3),
            asio::mutable_buffer(d2, 5)
        }};

        auto adapted = from_asio(asio_bufs);
        auto it = adapted.begin();
        auto end = adapted.end();

        mutable_buffer mb0 = *it;
        BOOST_TEST_EQ(mb0.data(), static_cast<void*>(d1));
        BOOST_TEST_EQ(mb0.size(), 3u);
        ++it;

        mutable_buffer mb1 = *it;
        BOOST_TEST_EQ(mb1.data(), static_cast<void*>(d2));
        BOOST_TEST_EQ(mb1.size(), 5u);
        ++it;

        BOOST_TEST(it == end);
    }

    void
    test_roundtrip()
    {
        // capy -> asio -> capy preserves data/size
        char data[] = "roundtrip";
        mutable_buffer mb(data, 9);

        auto asio_adapted = to_asio(mb);
        auto it = asio_adapted.begin();
        asio::mutable_buffer ab = *it;

        std::span<asio::mutable_buffer const> sp(&ab, 1);
        auto capy_adapted = from_asio(sp);
        auto it2 = capy_adapted.begin();
        mutable_buffer mb2 = *it2;

        BOOST_TEST_EQ(mb2.data(), mb.data());
        BOOST_TEST_EQ(mb2.size(), mb.size());
    }

    void
    test_bidirectional()
    {
        char d1[] = "ab";
        char d2[] = "cd";
        std::array<asio::const_buffer, 2> asio_bufs{{
            asio::const_buffer(d1, 2),
            asio::const_buffer(d2, 2)
        }};

        auto adapted = from_asio(asio_bufs);
        auto it = adapted.end();
        auto beg = adapted.begin();

        --it;
        const_buffer cb1 = *it;
        BOOST_TEST_EQ(cb1.data(), static_cast<void const*>(d2));

        --it;
        const_buffer cb0 = *it;
        BOOST_TEST_EQ(cb0.data(), static_cast<void const*>(d1));

        BOOST_TEST(it == beg);
    }

    void
    test_random_access()
    {
        char d1[] = "ab";
        char d2[] = "cd";
        char d3[] = "ef";
        std::array<asio::const_buffer, 3> asio_bufs{{
            asio::const_buffer(d1, 2),
            asio::const_buffer(d2, 2),
            asio::const_buffer(d3, 2)
        }};

        auto adapted = from_asio(asio_bufs);
        auto beg = adapted.begin();
        auto end = adapted.end();

        BOOST_TEST_EQ(end - beg, 3);
        BOOST_TEST(beg < end);

        auto mid = beg + 1;
        const_buffer cb = *mid;
        BOOST_TEST_EQ(cb.data(), static_cast<void const*>(d2));

        cb = beg[2];
        BOOST_TEST_EQ(cb.data(), static_cast<void const*>(d3));

        mid += 1;
        cb = *mid;
        BOOST_TEST_EQ(cb.data(), static_cast<void const*>(d3));

        mid -= 2;
        cb = *mid;
        BOOST_TEST_EQ(cb.data(), static_cast<void const*>(d1));
    }

    void
    test_move_semantics()
    {
        char d1[] = "abc";
        mutable_buffer_array<4> bufs;
        bufs = mutable_buffer_array<4>(
            mutable_buffer(d1, 3));

        auto adapted = to_asio(std::move(bufs));
        auto it = adapted.begin();
        asio::mutable_buffer ab = *it;
        BOOST_TEST_EQ(ab.data(), static_cast<void*>(d1));
        BOOST_TEST_EQ(ab.size(), 3u);
    }

    void
    run()
    {
        test_to_asio_const();
        test_to_asio_mutable();
        test_to_asio_array();
        test_from_asio_const();
        test_from_asio_mutable();
        test_from_asio_array();
        test_roundtrip();
        test_bidirectional();
        test_random_access();
        test_move_semantics();
    }
};

TEST_SUITE(
    asio_test,
    "boost.capy.buffers.asio");

} // capy
} // boost

#endif // BOOST_CAPY_HAS_ASIO
