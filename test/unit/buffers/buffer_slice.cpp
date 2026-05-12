//
// Copyright (c) 2026 Michael Vandeberg
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Test that headers are self-contained.
#include <boost/capy/buffers/buffer_slice.hpp>
#include <boost/capy/concept/slice.hpp>
#include <boost/capy/detail/slice_impl.hpp>

#include <boost/capy/buffers.hpp>

#include <array>
#include <cstring>
#include <ranges>
#include <string>

#include "test_suite.hpp"

namespace boost {
namespace capy {

namespace {

// Flatten the bytes exposed by a Slice's data() into a std::string for
// byte-exact comparison.
template<class Slice>
std::string flatten(Slice const& s)
{
    std::string out;
    auto view = s.data();
    for (auto it = view.begin(); it != view.end(); ++it)
    {
        auto buf = *it;
        out.append(
            static_cast<char const*>(buf.data()),
            buf.size());
    }
    return out;
}

} // anonymous namespace

struct buffer_slice_test
{
    void
    testConceptModeled()
    {
        char a[10];
        std::array<mutable_buffer, 1> mbufs = {
            mutable_buffer(a, sizeof(a))
        };
        std::array<const_buffer, 1> cbufs = {
            const_buffer(a, sizeof(a))
        };
        using m_slice = detail::slice_impl<decltype(mbufs)>;
        using c_slice = detail::slice_impl<decltype(cbufs)>;

        // Both satisfy Slice
        static_assert(Slice<m_slice>,
            "mutable-input slice_impl must satisfy Slice");
        static_assert(Slice<c_slice>,
            "const-input slice_impl must satisfy Slice");

        // Only the mutable-input one satisfies MutableSlice
        static_assert(MutableSlice<m_slice>,
            "mutable-input slice_impl must satisfy MutableSlice");
        static_assert(!MutableSlice<c_slice>,
            "const-input slice_impl must NOT satisfy MutableSlice");
    }

    void
    testNotABufferSequence()
    {
        char a[10];
        std::array<mutable_buffer, 1> bufs = {
            mutable_buffer(a, sizeof(a))
        };
        using slice_t = detail::slice_impl<decltype(bufs)>;
        static_assert(
            !ConstBufferSequence<slice_t>,
            "slice_impl must not model ConstBufferSequence");
        static_assert(
            !MutableBufferSequence<slice_t>,
            "slice_impl must not model MutableBufferSequence");
    }

    void
    testDataIsBufferSequence()
    {
        char a[10];
        std::array<mutable_buffer, 1> bufs = {
            mutable_buffer(a, sizeof(a))
        };
        detail::slice_impl<decltype(bufs)> s(bufs);
        using data_t = decltype(s.data());
        static_assert(
            MutableBufferSequence<data_t>,
            "data() return must satisfy MutableBufferSequence "
            "when input is mutable");
        static_assert(
            ConstBufferSequence<data_t>,
            "data() return must satisfy ConstBufferSequence");
        static_assert(
            std::ranges::bidirectional_range<data_t>,
            "data() return must be a bidirectional_range");
    }

    void
    testWholeSequenceCtor()
    {
        char a[10];
        char b[20];
        std::memset(a, 'A', sizeof(a));
        std::memset(b, 'B', sizeof(b));
        std::array<mutable_buffer, 2> bufs = {
            mutable_buffer(a, sizeof(a)),
            mutable_buffer(b, sizeof(b))
        };
        detail::slice_impl<decltype(bufs)> s(bufs);
        BOOST_TEST_EQ(buffer_size(s.data()), sizeof(a) + sizeof(b));

        std::string const expected =
            std::string(sizeof(a), 'A') + std::string(sizeof(b), 'B');
        BOOST_TEST_EQ(flatten(s), expected);
    }

    void
    testOffsetLengthCtor()
    {
        char a[10];
        char b[20];
        std::memset(a, 'A', sizeof(a));
        std::memset(b, 'B', sizeof(b));
        std::array<mutable_buffer, 2> bufs = {
            mutable_buffer(a, sizeof(a)),
            mutable_buffer(b, sizeof(b))
        };
        using slice_t = detail::slice_impl<decltype(bufs)>;

        // offset=0, length=total -> whole sequence
        {
            slice_t s(bufs, 0, 30);
            BOOST_TEST_EQ(buffer_size(s.data()), 30u);
            BOOST_TEST_EQ(flatten(s),
                std::string(10, 'A') + std::string(20, 'B'));
        }

        // offset inside first buffer (front trim, no back trim)
        {
            slice_t s(bufs, 3, 27);
            BOOST_TEST_EQ(buffer_size(s.data()), 27u);
            BOOST_TEST_EQ(flatten(s),
                std::string(7, 'A') + std::string(20, 'B'));
        }

        // offset past first buffer, length terminating inside last (front + back)
        {
            slice_t s(bufs, 12, 5);
            BOOST_TEST_EQ(buffer_size(s.data()), 5u);
            BOOST_TEST_EQ(flatten(s), std::string(5, 'B'));
        }

        // both offset and length inside first buffer
        {
            slice_t s(bufs, 2, 4);
            BOOST_TEST_EQ(buffer_size(s.data()), 4u);
            BOOST_TEST_EQ(flatten(s), std::string(4, 'A'));
        }

        // offset=0, length=0 -> empty
        {
            slice_t s(bufs, 0, 0);
            BOOST_TEST_EQ(buffer_size(s.data()), 0u);
            BOOST_TEST_EQ(flatten(s), std::string());
        }

        // offset >= total -> empty (no UB)
        {
            slice_t s(bufs, 50, 10);
            BOOST_TEST_EQ(buffer_size(s.data()), 0u);
        }

        // length > total - offset -> clamped to remainder
        {
            slice_t s(bufs, 5, 999);
            BOOST_TEST_EQ(buffer_size(s.data()), 25u);
            BOOST_TEST_EQ(flatten(s),
                std::string(5, 'A') + std::string(20, 'B'));
        }
    }

    void
    testRemovePrefix()
    {
        char a[10];
        char b[20];
        std::memset(a, 'A', sizeof(a));
        std::memset(b, 'B', sizeof(b));
        std::array<mutable_buffer, 2> bufs = {
            mutable_buffer(a, sizeof(a)),
            mutable_buffer(b, sizeof(b))
        };
        using slice_t = detail::slice_impl<decltype(bufs)>;

        // remove within first buffer
        {
            slice_t s(bufs);
            s.remove_prefix(3);
            BOOST_TEST_EQ(buffer_size(s.data()), 27u);
            BOOST_TEST_EQ(flatten(s),
                std::string(7, 'A') + std::string(20, 'B'));
        }

        // remove exactly to end of first buffer
        {
            slice_t s(bufs);
            s.remove_prefix(10);
            BOOST_TEST_EQ(buffer_size(s.data()), 20u);
            BOOST_TEST_EQ(flatten(s), std::string(20, 'B'));
        }

        // remove crossing into second buffer
        {
            slice_t s(bufs);
            s.remove_prefix(15);
            BOOST_TEST_EQ(buffer_size(s.data()), 15u);
            BOOST_TEST_EQ(flatten(s), std::string(15, 'B'));
        }

        // remove all
        {
            slice_t s(bufs);
            s.remove_prefix(30);
            BOOST_TEST_EQ(buffer_size(s.data()), 0u);
        }

        // remove more than total -> empty, no UB
        {
            slice_t s(bufs);
            s.remove_prefix(1000);
            BOOST_TEST_EQ(buffer_size(s.data()), 0u);
        }
    }

    void
    testRemovePrefixOnLengthCapped()
    {
        // Verify remove_prefix walks correctly through a slice that has
        // back_skip_ set by an offset/length constructor.
        char a[5];
        char b[5];
        char c[5];
        std::memset(a, 'a', sizeof(a));
        std::memset(b, 'b', sizeof(b));
        std::memset(c, 'c', sizeof(c));
        std::array<mutable_buffer, 3> bufs = {
            mutable_buffer(a, sizeof(a)),
            mutable_buffer(b, sizeof(b)),
            mutable_buffer(c, sizeof(c))
        };
        using slice_t = detail::slice_impl<decltype(bufs)>;

        // bytes 2..12 -> [3 'a' + 5 'b' + 2 'c']
        slice_t s(bufs, 2, 10);
        BOOST_TEST_EQ(buffer_size(s.data()), 10u);
        BOOST_TEST_EQ(flatten(s),
            std::string(3, 'a') + std::string(5, 'b') + std::string(2, 'c'));

        // remove 4 -> [4 'b' + 2 'c'] (consumed 3 'a' + 1 'b')
        s.remove_prefix(4);
        BOOST_TEST_EQ(buffer_size(s.data()), 6u);
        BOOST_TEST_EQ(flatten(s),
            std::string(4, 'b') + std::string(2, 'c'));

        // remove 5 -> [1 'c'] (consumed 4 'b' + 1 'c')
        s.remove_prefix(5);
        BOOST_TEST_EQ(buffer_size(s.data()), 1u);
        BOOST_TEST_EQ(flatten(s), std::string(1, 'c'));

        // remove 1 -> empty
        s.remove_prefix(1);
        BOOST_TEST_EQ(buffer_size(s.data()), 0u);
    }

    void
    testEmpty()
    {
        // default-constructed slice
        detail::slice_impl<std::array<mutable_buffer, 0>> s{};
        BOOST_TEST_EQ(buffer_size(s.data()), 0u);
        s.remove_prefix(5);
        BOOST_TEST_EQ(buffer_size(s.data()), 0u);
    }

    void
    testMutableVsConst()
    {
        char a[10];
        std::array<mutable_buffer, 1> mbufs = {
            mutable_buffer(a, sizeof(a))
        };
        std::array<const_buffer, 1> cbufs = {
            const_buffer(a, sizeof(a))
        };
        using m_slice = detail::slice_impl<decltype(mbufs)>;
        using c_slice = detail::slice_impl<decltype(cbufs)>;

        static_assert(
            std::is_same_v<m_slice::buffer_type, mutable_buffer>,
            "mutable input -> mutable buffer_type");
        static_assert(
            std::is_same_v<c_slice::buffer_type, const_buffer>,
            "const input -> const buffer_type");

        m_slice ms(mbufs);
        c_slice cs(cbufs);
        BOOST_TEST_EQ(buffer_size(ms.data()), 10u);
        BOOST_TEST_EQ(buffer_size(cs.data()), 10u);
    }

    void
    testSingleBuffer()
    {
        char a[10];
        std::memset(a, 'X', sizeof(a));
        mutable_buffer mb(a, sizeof(a));

        detail::slice_impl<mutable_buffer> s(mb);
        BOOST_TEST_EQ(buffer_size(s.data()), 10u);
        BOOST_TEST_EQ(flatten(s), std::string(10, 'X'));

        s.remove_prefix(3);
        BOOST_TEST_EQ(buffer_size(s.data()), 7u);
        BOOST_TEST_EQ(flatten(s), std::string(7, 'X'));
    }

    void
    testPublicFunction()
    {
        char a[10];
        char b[20];
        std::memset(a, 'A', sizeof(a));
        std::memset(b, 'B', sizeof(b));
        std::array<mutable_buffer, 2> bufs = {
            mutable_buffer(a, sizeof(a)),
            mutable_buffer(b, sizeof(b))
        };

        // default args: whole sequence
        {
            auto s = buffer_slice(bufs);
            static_assert(Slice<decltype(s)>,
                "buffer_slice's return must satisfy Slice");
            static_assert(MutableSlice<decltype(s)>,
                "buffer_slice over mutable input must satisfy MutableSlice");
            BOOST_TEST_EQ(buffer_size(s.data()), 30u);
            BOOST_TEST_EQ(flatten(s),
                std::string(10, 'A') + std::string(20, 'B'));
        }

        // const input -> Slice but not MutableSlice
        {
            std::array<const_buffer, 1> cbufs = {
                const_buffer(a, sizeof(a))
            };
            auto s = buffer_slice(cbufs);
            static_assert(Slice<decltype(s)>,
                "buffer_slice over const input must satisfy Slice");
            static_assert(!MutableSlice<decltype(s)>,
                "buffer_slice over const input must NOT satisfy MutableSlice");
            BOOST_TEST_EQ(buffer_size(s.data()), 10u);
        }

        // offset + length
        {
            auto s = buffer_slice(bufs, 5, 10);
            BOOST_TEST_EQ(buffer_size(s.data()), 10u);
            BOOST_TEST_EQ(flatten(s),
                std::string(5, 'A') + std::string(5, 'B'));
        }

        // offset only (length defaults to "to end")
        {
            auto s = buffer_slice(bufs, 12);
            BOOST_TEST_EQ(buffer_size(s.data()), 18u);
            BOOST_TEST_EQ(flatten(s), std::string(18, 'B'));
        }

        // single buffer
        {
            mutable_buffer mb(a, sizeof(a));
            auto s = buffer_slice(mb, 2, 5);
            BOOST_TEST_EQ(buffer_size(s.data()), 5u);
            BOOST_TEST_EQ(flatten(s), std::string(5, 'A'));
        }
    }

    void
    run()
    {
        testConceptModeled();
        testNotABufferSequence();
        testDataIsBufferSequence();
        testWholeSequenceCtor();
        testOffsetLengthCtor();
        testRemovePrefix();
        testRemovePrefixOnLengthCapped();
        testEmpty();
        testMutableVsConst();
        testSingleBuffer();
        testPublicFunction();
    }
};

TEST_SUITE(buffer_slice_test, "boost.capy.buffer_slice");

} // namespace capy
} // namespace boost
