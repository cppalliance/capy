//
// Copyright (c) 2023 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Test that header file is self-contained.
#include <boost/capy/buffers/buffer_pair.hpp>

#include "test_buffers.hpp"

namespace boost {
namespace capy {

struct buffer_pair_test
{
    void
    testConstPair()
    {
        auto const& pat = test_pattern();

        // const_buffer_pair()
        {
            const_buffer_pair cb;
            BOOST_TEST_EQ(
                buffer_size(cb), 0);
        }

        // const_buffer_pair(
        //  const_buffer_pair const&),
        // const_buffer_pair(
        //  const_buffer const&)
        //  const_buffer const&)
        {
            for(std::size_t i = 0;
                i <= pat.size(); ++i)
            {
                const_buffer_pair cb0({{
                    { &pat[0], i },
                    { &pat[i], pat.size() - i }}});
                const_buffer_pair cb1(cb0);
                BOOST_TEST_EQ(
                    test::make_string(cb0), pat);
                BOOST_TEST_EQ(
                    test::make_string(cb0),
                    test::make_string(cb1));
                BOOST_TEST_EQ(
                    cb0[0].data(), cb1[0].data());
                BOOST_TEST_EQ(
                    cb0[1].size(), cb1[1].size());
                auto const& ccb0 = cb0;
                auto const& ccb1 = cb1;
                BOOST_TEST_EQ(
                    ccb0[0].data(), ccb1[0].data());
                BOOST_TEST_EQ(
                    ccb0[1].size(), ccb1[1].size());
            }
        }

        // operator=(const_buffer_pair const&)
        {
            for(std::size_t i = 0;
                i <= pat.size(); ++i)
            {
                const_buffer_pair cb0({{
                    { &pat[0], i },
                    { &pat[i], pat.size() - i }}});
                const_buffer_pair cb1;
                cb1 = cb0;
                BOOST_TEST_EQ(
                    test::make_string(cb0), pat);
                BOOST_TEST_EQ(
                    test::make_string(cb0),
                    test::make_string(cb1));
            }
        }

        {
            for(std::size_t i = 0;
                i <= pat.size(); ++i)
            {
                const_buffer_pair cb({{
                    { &pat[0], i },
                    { &pat[i], pat.size() - i }}});
                test::check_sequence(cb, pat);
            }
        }
    }

    void
    testMutablePair()
    {
        std::string pat = test_pattern();

        // mutable_buffer_pair()
        {
            mutable_buffer_pair mb;
            BOOST_TEST_EQ(buffer_size(mb), 0);
        }

        // mutable_buffer_pair(
        //  mutable_buffer_pair const&),
        // mutable_buffer_pair(
        //  mutable_buffer const&)
        //  mutable_buffer const&)
        {
            for(std::size_t i = 0;
                i <= pat.size(); ++i)
            {
                mutable_buffer_pair mb0({{
                    { &pat[0], i },
                    { &pat[i], pat.size() - i }}});
                mutable_buffer_pair mb1(mb0);
                BOOST_TEST_EQ(
                    test::make_string(mb0), pat);
                BOOST_TEST_EQ(
                    test::make_string(mb0),
                    test::make_string(mb1));
                BOOST_TEST_EQ(
                    mb0[0].data(), mb1[0].data());
                BOOST_TEST_EQ(
                    mb0[1].size(), mb1[1].size());
                auto const& cmb0 = mb0;
                auto const& cmb1 = mb1;
                BOOST_TEST_EQ(
                    cmb0[0].data(), cmb1[0].data());
                BOOST_TEST_EQ(
                    cmb0[1].size(), cmb1[1].size());
            }
        }

        // operator=(mutable_buffer_pair const&)
        {
            for(std::size_t i = 0;
                i <= pat.size(); ++i)
            {
                mutable_buffer_pair mb0({{
                    { &pat[0], i },
                    { &pat[i], pat.size() - i }}});
                mutable_buffer_pair mb1;
                mb1 = mb0;
                BOOST_TEST_EQ(
                    test::make_string(mb0), pat);
                BOOST_TEST_EQ(
                    test::make_string(mb0),
                    test::make_string(mb1));
            }
        }

        // operator=(mutable_buffer_pair const&)
        {
            for(std::size_t i = 0;
                i <= pat.size(); ++i)
            {
                auto s = pat;
                mutable_buffer_pair b({{
                    { &s[0], i },
                    { &s[i], s.size() - i }}});
                mutable_buffer_pair mb;
                mb = b;
                BOOST_TEST_EQ(
                    test::make_string(mb), pat);
                BOOST_TEST_EQ(
                    test::make_string(mb),
                    test::make_string(b));
            }
        }

        {
            for(std::size_t i = 0;
                i <= pat.size(); ++i)
            {
                mutable_buffer_pair cb({{
                    { &pat[0], i },
                    { &pat[i], pat.size() - i }}});
                test::check_sequence(cb, pat);
            }
        }
    }

    void
    run()
    {
        testConstPair();
        testMutablePair();
    }
};

TEST_SUITE(
    buffer_pair_test,
    "boost.capy.buffers.buffer_pair");

} // capy
} // boost
