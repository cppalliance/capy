//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Test that header file is self-contained.
#include <boost/capy/detail/buffer_array.hpp>

#include "../buffers/test_buffers.hpp"

#include <ranges>
#include <span>
#include <vector>

namespace boost {
namespace capy {

// std::ranges concepts for detail::const_buffer_array

static_assert(std::ranges::range<detail::const_buffer_array<4>>);
static_assert(std::ranges::input_range<detail::const_buffer_array<4>>);
static_assert(std::ranges::forward_range<detail::const_buffer_array<4>>);
static_assert(std::ranges::bidirectional_range<detail::const_buffer_array<4>>);
static_assert(std::ranges::random_access_range<detail::const_buffer_array<4>>);
static_assert(std::ranges::contiguous_range<detail::const_buffer_array<4>>);

static_assert(ConstBufferSequence<detail::const_buffer_array<4>>);
static_assert(!MutableBufferSequence<detail::const_buffer_array<4>>);

// std::ranges concepts for detail::mutable_buffer_array

static_assert(std::ranges::range<detail::mutable_buffer_array<4>>);
static_assert(std::ranges::input_range<detail::mutable_buffer_array<4>>);
static_assert(std::ranges::forward_range<detail::mutable_buffer_array<4>>);
static_assert(std::ranges::bidirectional_range<detail::mutable_buffer_array<4>>);
static_assert(std::ranges::random_access_range<detail::mutable_buffer_array<4>>);
static_assert(std::ranges::contiguous_range<detail::mutable_buffer_array<4>>);

static_assert(ConstBufferSequence<detail::mutable_buffer_array<4>>);
static_assert(MutableBufferSequence<detail::mutable_buffer_array<4>>);

struct buffer_array_test
{
    void
    testConstArray()
    {
        auto const& pat = test_pattern();

        // default constructor
        {
            detail::const_buffer_array<4> ba;
            BOOST_TEST_EQ(ba.to_span().size(), 0);
            BOOST_TEST_EQ(buffer_size(ba), 0);
        }

        // single buffer constructor
        {
            const_buffer b(pat.data(), pat.size());
            detail::const_buffer_array<4> ba(b);
            BOOST_TEST_EQ(ba.to_span().size(), 1);
            BOOST_TEST_EQ(buffer_size(ba), pat.size());
            BOOST_TEST_EQ(test::make_string(ba), pat);
        }

        // empty buffer is skipped
        {
            const_buffer b(pat.data(), 0);
            detail::const_buffer_array<4> ba(b);
            BOOST_TEST_EQ(ba.to_span().size(), 0);
            BOOST_TEST_EQ(buffer_size(ba), 0);
        }

        // buffer sequence constructor (truncating)
        {
            std::vector<const_buffer> v;
            v.emplace_back(pat.data(), 3);
            v.emplace_back(pat.data() + 3, 5);
            v.emplace_back(pat.data() + 8, pat.size() - 8);
            detail::const_buffer_array<4> ba(v);
            BOOST_TEST_EQ(ba.to_span().size(), 3);
            BOOST_TEST_EQ(buffer_size(ba), pat.size());
            BOOST_TEST_EQ(test::make_string(ba), pat);
        }

        // copy constructor
        {
            const_buffer b(pat.data(), pat.size());
            detail::const_buffer_array<4> ba1(b);
            detail::const_buffer_array<4> ba2(ba1);
            BOOST_TEST_EQ(ba2.to_span().size(), 1);
            BOOST_TEST_EQ(buffer_size(ba2), pat.size());
            BOOST_TEST_EQ(test::make_string(ba2), pat);
        }

        // copy assignment
        {
            const_buffer b(pat.data(), pat.size());
            detail::const_buffer_array<4> ba1(b);
            detail::const_buffer_array<4> ba2;
            ba2 = ba1;
            BOOST_TEST_EQ(ba2.to_span().size(), 1);
            BOOST_TEST_EQ(buffer_size(ba2), pat.size());
            BOOST_TEST_EQ(test::make_string(ba2), pat);
        }

        // assignment from buffer sequence (truncates)
        {
            std::vector<const_buffer> v;
            v.emplace_back(pat.data(), 3);
            v.emplace_back(pat.data() + 3, 5);
            v.emplace_back(pat.data() + 8, pat.size() - 8);
            detail::const_buffer_array<2> ba;
            ba = v;
            BOOST_TEST_EQ(ba.to_span().size(), 2);
            BOOST_TEST_EQ(buffer_size(ba), 8);
        }

        // span conversion
        {
            const_buffer b(pat.data(), pat.size());
            detail::const_buffer_array<4> ba(b);
            std::span<const_buffer const> sp = ba;
            BOOST_TEST_EQ(sp.size(), 1);
            BOOST_TEST_EQ(sp[0].data(), pat.data());
        }

        // to_span
        {
            const_buffer b(pat.data(), pat.size());
            detail::const_buffer_array<4> ba(b);
            auto sp = ba.to_span();
            BOOST_TEST_EQ(sp.size(), 1);
            BOOST_TEST_EQ(sp[0].data(), pat.data());
        }

        // 1-arg constructor truncates
        {
            std::vector<const_buffer> v;
            v.emplace_back(pat.data(), 3);
            v.emplace_back(pat.data() + 3, 5);
            v.emplace_back(pat.data() + 8, pat.size() - 8);
            detail::const_buffer_array<2> ba(v);
            BOOST_TEST_EQ(ba.to_span().size(), 2);
            BOOST_TEST_EQ(buffer_size(ba), 8);
        }

        // in_place constructor throws on overflow
        {
            std::vector<const_buffer> v;
            v.emplace_back(pat.data(), 3);
            v.emplace_back(pat.data() + 3, 5);
            v.emplace_back(pat.data() + 8, pat.size() - 8);
            bool threw = false;
            try
            {
                detail::const_buffer_array<2> ba(std::in_place, v);
                (void)ba;
            }
            catch(std::length_error const&)
            {
                threw = true;
            }
            BOOST_TEST(threw);
        }

        // in_place constructor with exact fit
        {
            std::vector<const_buffer> v;
            v.emplace_back(pat.data(), 3);
            v.emplace_back(pat.data() + 3, 5);
            v.emplace_back(pat.data() + 8, pat.size() - 8);
            detail::const_buffer_array<4> ba(std::in_place, v);
            BOOST_TEST_EQ(ba.to_span().size(), 3);
            BOOST_TEST_EQ(buffer_size(ba), pat.size());
            BOOST_TEST_EQ(test::make_string(ba), pat);
        }

        // iterator-pair constructor (fits)
        {
            std::vector<const_buffer> v;
            v.emplace_back(pat.data(), 3);
            v.emplace_back(pat.data() + 3, 5);
            v.emplace_back(pat.data() + 8, pat.size() - 8);
            detail::const_buffer_array<4> ba(v.begin(), v.end());
            BOOST_TEST_EQ(ba.to_span().size(), 3);
            BOOST_TEST_EQ(buffer_size(ba), pat.size());
            BOOST_TEST_EQ(test::make_string(ba), pat);
        }

        // iterator-pair constructor truncates
        {
            std::vector<const_buffer> v;
            v.emplace_back(pat.data(), 3);
            v.emplace_back(pat.data() + 3, 5);
            v.emplace_back(pat.data() + 8, pat.size() - 8);
            detail::const_buffer_array<2> ba(v.begin(), v.end());
            BOOST_TEST_EQ(ba.to_span().size(), 2);
            BOOST_TEST_EQ(buffer_size(ba), 8);
        }

        // iterator-pair empty range
        {
            std::vector<const_buffer> v;
            detail::const_buffer_array<4> ba(v.begin(), v.end());
            BOOST_TEST_EQ(ba.to_span().size(), 0);
            BOOST_TEST_EQ(buffer_size(ba), 0);
        }

        // iterator-pair skips zero-sized buffers
        {
            std::vector<const_buffer> v;
            v.emplace_back(pat.data(), 0);
            v.emplace_back(pat.data(), 3);
            v.emplace_back(pat.data() + 3, 0);
            v.emplace_back(pat.data() + 3, 5);
            detail::const_buffer_array<4> ba(v.begin(), v.end());
            BOOST_TEST_EQ(ba.to_span().size(), 2);
            BOOST_TEST_EQ(buffer_size(ba), 8);
        }

        // in_place iterator-pair throws on overflow
        {
            std::vector<const_buffer> v;
            v.emplace_back(pat.data(), 3);
            v.emplace_back(pat.data() + 3, 5);
            v.emplace_back(pat.data() + 8, pat.size() - 8);
            bool threw = false;
            try
            {
                detail::const_buffer_array<2> ba(
                    std::in_place, v.begin(), v.end());
                (void)ba;
            }
            catch(std::length_error const&)
            {
                threw = true;
            }
            BOOST_TEST(threw);
        }

        // in_place iterator-pair with exact fit
        {
            std::vector<const_buffer> v;
            v.emplace_back(pat.data(), 3);
            v.emplace_back(pat.data() + 3, 5);
            v.emplace_back(pat.data() + 8, pat.size() - 8);
            detail::const_buffer_array<4> ba(
                std::in_place, v.begin(), v.end());
            BOOST_TEST_EQ(ba.to_span().size(), 3);
            BOOST_TEST_EQ(buffer_size(ba), pat.size());
            BOOST_TEST_EQ(test::make_string(ba), pat);
        }

        // slice tests
        {
            for(std::size_t i = 0; i <= pat.size(); ++i)
            {
                std::vector<const_buffer> v;
                v.emplace_back(pat.data(), i);
                v.emplace_back(pat.data() + i, pat.size() - i);
                detail::const_buffer_array<4> ba(v);
                test::check_sequence(ba, pat);
            }
        }
    }

    void
    testMutableArray()
    {
        std::string pat = test_pattern();

        // default constructor
        {
            detail::mutable_buffer_array<4> ba;
            BOOST_TEST_EQ(ba.to_span().size(), 0);
            BOOST_TEST_EQ(buffer_size(ba), 0);
        }

        // single buffer constructor
        {
            mutable_buffer b(pat.data(), pat.size());
            detail::mutable_buffer_array<4> ba(b);
            BOOST_TEST_EQ(ba.to_span().size(), 1);
            BOOST_TEST_EQ(buffer_size(ba), pat.size());
            BOOST_TEST_EQ(test::make_string(ba), pat);
        }

        // buffer sequence constructor
        {
            std::vector<mutable_buffer> v;
            v.emplace_back(pat.data(), 3);
            v.emplace_back(pat.data() + 3, 5);
            v.emplace_back(pat.data() + 8, pat.size() - 8);
            detail::mutable_buffer_array<4> ba(v);
            BOOST_TEST_EQ(ba.to_span().size(), 3);
            BOOST_TEST_EQ(buffer_size(ba), pat.size());
            BOOST_TEST_EQ(test::make_string(ba), pat);
        }

        // copy constructor
        {
            mutable_buffer b(pat.data(), pat.size());
            detail::mutable_buffer_array<4> ba1(b);
            detail::mutable_buffer_array<4> ba2(ba1);
            BOOST_TEST_EQ(ba2.to_span().size(), 1);
            BOOST_TEST_EQ(buffer_size(ba2), pat.size());
            BOOST_TEST_EQ(test::make_string(ba2), pat);
        }

        // copy assignment
        {
            mutable_buffer b(pat.data(), pat.size());
            detail::mutable_buffer_array<4> ba1(b);
            detail::mutable_buffer_array<4> ba2;
            ba2 = ba1;
            BOOST_TEST_EQ(ba2.to_span().size(), 1);
            BOOST_TEST_EQ(buffer_size(ba2), pat.size());
            BOOST_TEST_EQ(test::make_string(ba2), pat);
        }

        // assignment from buffer sequence (truncates)
        {
            std::vector<mutable_buffer> v;
            v.emplace_back(pat.data(), 3);
            v.emplace_back(pat.data() + 3, 5);
            v.emplace_back(pat.data() + 8, pat.size() - 8);
            detail::mutable_buffer_array<2> ba;
            ba = v;
            BOOST_TEST_EQ(ba.to_span().size(), 2);
            BOOST_TEST_EQ(buffer_size(ba), 8);
        }

        // span conversion
        {
            mutable_buffer b(pat.data(), pat.size());
            detail::mutable_buffer_array<4> ba(b);
            std::span<mutable_buffer> sp = ba;
            BOOST_TEST_EQ(sp.size(), 1);
            BOOST_TEST_EQ(sp[0].data(), pat.data());
        }

        // to_span
        {
            mutable_buffer b(pat.data(), pat.size());
            detail::mutable_buffer_array<4> ba(b);
            auto sp = ba.to_span();
            BOOST_TEST_EQ(sp.size(), 1);
            BOOST_TEST_EQ(sp[0].data(), pat.data());
        }

        // 1-arg constructor truncates
        {
            std::vector<mutable_buffer> v;
            v.emplace_back(pat.data(), 3);
            v.emplace_back(pat.data() + 3, 5);
            v.emplace_back(pat.data() + 8, pat.size() - 8);
            detail::mutable_buffer_array<2> ba(v);
            BOOST_TEST_EQ(ba.to_span().size(), 2);
            BOOST_TEST_EQ(buffer_size(ba), 8);
        }

        // in_place constructor throws on overflow
        {
            std::vector<mutable_buffer> v;
            v.emplace_back(pat.data(), 3);
            v.emplace_back(pat.data() + 3, 5);
            v.emplace_back(pat.data() + 8, pat.size() - 8);
            bool threw = false;
            try
            {
                detail::mutable_buffer_array<2> ba(std::in_place, v);
                (void)ba;
            }
            catch(std::length_error const&)
            {
                threw = true;
            }
            BOOST_TEST(threw);
        }

        // in_place constructor with exact fit
        {
            std::vector<mutable_buffer> v;
            v.emplace_back(pat.data(), 3);
            v.emplace_back(pat.data() + 3, 5);
            v.emplace_back(pat.data() + 8, pat.size() - 8);
            detail::mutable_buffer_array<4> ba(std::in_place, v);
            BOOST_TEST_EQ(ba.to_span().size(), 3);
            BOOST_TEST_EQ(buffer_size(ba), pat.size());
            BOOST_TEST_EQ(test::make_string(ba), pat);
        }

        // iterator-pair constructor (fits)
        {
            std::vector<mutable_buffer> v;
            v.emplace_back(pat.data(), 3);
            v.emplace_back(pat.data() + 3, 5);
            v.emplace_back(pat.data() + 8, pat.size() - 8);
            detail::mutable_buffer_array<4> ba(v.begin(), v.end());
            BOOST_TEST_EQ(ba.to_span().size(), 3);
            BOOST_TEST_EQ(buffer_size(ba), pat.size());
            BOOST_TEST_EQ(test::make_string(ba), pat);
        }

        // iterator-pair constructor truncates
        {
            std::vector<mutable_buffer> v;
            v.emplace_back(pat.data(), 3);
            v.emplace_back(pat.data() + 3, 5);
            v.emplace_back(pat.data() + 8, pat.size() - 8);
            detail::mutable_buffer_array<2> ba(v.begin(), v.end());
            BOOST_TEST_EQ(ba.to_span().size(), 2);
            BOOST_TEST_EQ(buffer_size(ba), 8);
        }

        // iterator-pair empty range
        {
            std::vector<mutable_buffer> v;
            detail::mutable_buffer_array<4> ba(v.begin(), v.end());
            BOOST_TEST_EQ(ba.to_span().size(), 0);
            BOOST_TEST_EQ(buffer_size(ba), 0);
        }

        // iterator-pair skips zero-sized buffers
        {
            std::vector<mutable_buffer> v;
            v.emplace_back(pat.data(), 0);
            v.emplace_back(pat.data(), 3);
            v.emplace_back(pat.data() + 3, 0);
            v.emplace_back(pat.data() + 3, 5);
            detail::mutable_buffer_array<4> ba(v.begin(), v.end());
            BOOST_TEST_EQ(ba.to_span().size(), 2);
            BOOST_TEST_EQ(buffer_size(ba), 8);
        }

        // in_place iterator-pair throws on overflow
        {
            std::vector<mutable_buffer> v;
            v.emplace_back(pat.data(), 3);
            v.emplace_back(pat.data() + 3, 5);
            v.emplace_back(pat.data() + 8, pat.size() - 8);
            bool threw = false;
            try
            {
                detail::mutable_buffer_array<2> ba(
                    std::in_place, v.begin(), v.end());
                (void)ba;
            }
            catch(std::length_error const&)
            {
                threw = true;
            }
            BOOST_TEST(threw);
        }

        // in_place iterator-pair with exact fit
        {
            std::vector<mutable_buffer> v;
            v.emplace_back(pat.data(), 3);
            v.emplace_back(pat.data() + 3, 5);
            v.emplace_back(pat.data() + 8, pat.size() - 8);
            detail::mutable_buffer_array<4> ba(
                std::in_place, v.begin(), v.end());
            BOOST_TEST_EQ(ba.to_span().size(), 3);
            BOOST_TEST_EQ(buffer_size(ba), pat.size());
            BOOST_TEST_EQ(test::make_string(ba), pat);
        }

        // slice tests
        {
            for(std::size_t i = 0; i <= pat.size(); ++i)
            {
                std::vector<mutable_buffer> v;
                v.emplace_back(pat.data(), i);
                v.emplace_back(pat.data() + i, pat.size() - i);
                detail::mutable_buffer_array<4> ba(v);
                test::check_sequence(ba, pat);
            }
        }
    }

    void
    run()
    {
        testConstArray();
        testMutableArray();
    }
};

TEST_SUITE(
    buffer_array_test,
    "boost.capy.buffers.buffer_array");

} // capy
} // boost
