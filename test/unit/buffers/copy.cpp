//
// Copyright (c) 2023 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Test that header file is self-contained.
#include <boost/capy/buffers/copy.hpp>

#include <boost/capy/buffers/buffer_pair.hpp>
#include <boost/core/span.hpp>
#include "test_buffers.hpp"

namespace boost {
namespace capy {

struct buffer_copy_test
{
    void
    testBufferCopy1()
    {
        std::string const s =
            "Howdy partner";
        auto const N = s.size();
        for(std::size_t i = 0;
            i < N; ++i)
        {
            for(std::size_t j = 0;
                j < N; ++j)
            {
                for(std::size_t k = 0;
                    k < N + 2; ++k)
                {
                    const_buffer_pair p0{{
                        const_buffer(s.data(), i),
                        const_buffer(s.data() + i, N - i) }};
                    char tmp[13];
                    std::memset(tmp, 0, sizeof(tmp));
                    mutable_buffer_pair p1{{
                        mutable_buffer(tmp, j),
                        mutable_buffer(tmp + j, N - j) }};
                    auto const n = copy(
                        p1, p0, k);
                    BOOST_TEST_LE(n, N);
                    BOOST_TEST_EQ(
                        s.substr(0, n),
                        std::string(tmp, n));
                }
            }
        }
    }

    void
    testBufferCopy2()
    {
        auto const& pat = test_pattern();

        for(std::size_t i = 0;
            i <= pat.size(); ++i)
        {
            for(std::size_t j = 0;
                j <= pat.size(); ++j)
            {
                std::string s;
                s.resize(pat.size());
                const_buffer cb[2] = {
                    { &pat[0], i },
                    { &pat[i],
                        pat.size() - i } };
                mutable_buffer mb[2] = {
                    { &s[0], j },
                    { &s[j],
                        pat.size() - j } };
                auto n = copy(
                    span<mutable_buffer const>(mb, 2),
                    span<const_buffer const>(cb, 2));
                BOOST_TEST_EQ(n, pat.size());
                BOOST_TEST_EQ(s, pat);
            }
            for(std::size_t j = 0;
                j <= pat.size(); ++j)
            {
                for(std::size_t k = 0;
                    k <= pat.size(); ++k)
                {
                    std::string s;
                    s.resize(pat.size());
                    const_buffer cb[2] = {
                        { &pat[0], i },
                        { &pat[i],
                            pat.size() - i } };
                    mutable_buffer mb[2] = {
                        { &s[0], j },
                        { &s[j],
                            pat.size() - j } };
                    auto n = copy(
                        span<mutable_buffer const>(mb, 2),
                        span<const_buffer const>(cb, 2), k);
                    s.resize(n);
                    BOOST_TEST_EQ(s,
                        pat.substr(0, k));
                }
            }
        }
    }

    void
    testEmptyBufferCopy()
    {
        std::string s = test_pattern();

        // empty source
        {
            const_buffer source{ nullptr, 0 };
            mutable_buffer target{ &s[0], s.size() };

            auto n = copy(target, source);
            BOOST_TEST_EQ(n, 0);
        }

        // empty target
        {
            const_buffer source{ &s[0], s.size() };
            mutable_buffer target{ nullptr, 0 };

            auto n = copy(target, source);
            BOOST_TEST_EQ(n, 0);
        }
    }

    void
    run()
    {
        testBufferCopy1();
        testBufferCopy2();
        testEmptyBufferCopy();
    }
};

TEST_SUITE(
    buffer_copy_test,
    "boost.capy.buffers.copy");

} // capy
} // boost
