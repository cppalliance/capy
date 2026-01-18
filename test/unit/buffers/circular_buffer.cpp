//
// Copyright (c) 2023 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Test that header file is self-contained.
#include <boost/capy/buffers/circular_buffer.hpp>

#include <boost/capy/buffers/dynamic_buffer.hpp>

#include "test_buffers.hpp"

namespace boost {
namespace capy {

BOOST_STATIC_ASSERT(is_DynamicBuffer<circular_buffer>::value);

struct circular_buffer_test
{
    void
    testMembers()
    {
        std::string pat = test_pattern();

        // circular_buffer()
        {
            circular_buffer cb;
            BOOST_TEST_EQ(cb.size(), 0);
        }

#if 0
        // circular_buffer(mutable_buffer)
        {
            circular_buffer cb(
                buffer(&pat[0], pat.size()));
            BOOST_TEST_EQ(cb.size(), 0);
            BOOST_TEST_EQ(cb.capacity(), pat.size());
            BOOST_TEST_EQ(cb.max_size(), pat.size());
        }
#endif

        // circular_buffer(void*, std::size_t)
        {
            circular_buffer cb(
                &pat[0], pat.size());
            BOOST_TEST_EQ(cb.size(), 0);
            BOOST_TEST_EQ(cb.capacity(), pat.size());
            BOOST_TEST_EQ(cb.max_size(), pat.size());
        }

        // circular_buffer(
        //  void*, std::size_t, std:size_t)
        {
            circular_buffer cb(
                &pat[0], pat.size(), 6);
            BOOST_TEST_EQ(cb.size(), 6);
            BOOST_TEST_EQ(
                cb.capacity(), pat.size() - 6);
            BOOST_TEST_EQ(cb.max_size(), pat.size());
            BOOST_TEST_EQ(
                test::make_string(cb.data()),
                pat.substr(0, 6));
        }
        {
            BOOST_TEST_THROWS(
                circular_buffer(
                    &pat[0], pat.size(), 600),
                std::exception);
        }

        // circular_buffer(
        //  circular_buffer const&)
        {
            circular_buffer cb0(&pat[0], pat.size());
            circular_buffer cb1(cb0);
            BOOST_TEST_EQ(cb1.size(), cb0.size());
            BOOST_TEST_EQ(cb1.capacity(), cb0.capacity());
            BOOST_TEST_EQ(cb1.max_size(), cb0.max_size());
        }

        // operator=(
        //  circular_buffer const&)
        {
            circular_buffer cb0(&pat[0], pat.size());
            circular_buffer cb1;
            cb1 = cb0;
            BOOST_TEST_EQ(cb1.size(), cb0.size());
            BOOST_TEST_EQ(cb1.capacity(), cb0.capacity());
            BOOST_TEST_EQ(cb1.max_size(), cb0.max_size());
        }

        // prepare(std::size_t)
        {
            circular_buffer cb(&pat[0], pat.size());
            BOOST_TEST_THROWS(
                cb.prepare(cb.capacity() + 1),
                std::length_error);
        }

        // commit(std::size_t)
        {
            circular_buffer cb(&pat[0], pat.size());
            auto n = pat.size() / 2;
            cb.prepare(pat.size());
            cb.commit(n);
            BOOST_TEST_EQ(
                test::make_string(cb.data()),
                pat.substr(0, n));
        }
    }

    void
    testBuffer()
    {
        auto const& pat = test_pattern();

        for(std::size_t i = 0;
            i <= pat.size(); ++i)
        for(std::size_t j = 0;
            j <=  pat.size(); ++j)
        for(std::size_t k = 0;
            k <= pat.size(); ++k)
        {
            std::string s(pat.size(), 0);
            circular_buffer bs(
                &s[0], s.size());
            BOOST_TEST_EQ(
                bs.capacity(), s.size());
            if( j < pat.size() &&
                i > 0)
            {
                bs.prepare(i);
                bs.commit(i);
                BOOST_TEST_EQ(
                    bs.capacity(),
                    bs.max_size() - bs.size());
                bs.consume(i - 1);
                bs.commit(copy(
                    bs.prepare(j),
                    make_buffer(
                        pat.data(),
                        pat.size())));
                bs.consume(1);
            }
            else
            {
                bs.commit(copy(
                    bs.prepare(j),
                    make_buffer(
                        pat.data(),
                        pat.size())));
                BOOST_TEST_EQ(
                    bs.capacity(),
                    bs.max_size() - bs.size());
            }
            bs.commit(copy(
                bs.prepare(pat.size() - j),
                make_buffer(
                    pat.data() + j,
                    pat.size() - j)));
            BOOST_TEST_EQ(test::make_string(
                bs.data()), pat);
            test::check_sequence(bs.data(), pat);
            bs.consume(k);
            BOOST_TEST_EQ(test::make_string(
                bs.data()), pat.substr(k));
        }
    }

    void
    testInterleavedReadWrite()
    {
        std::string pat = test_pattern();

        for(std::size_t i = 0; i <= pat.size(); ++i)
        {
            circular_buffer cb(&pat[0], pat.size());
            cb.prepare(i);
            cb.commit(i);
            BOOST_TEST_EQ(
                test::make_string(cb.data()),
                pat.substr(0, i));
            // commit after consume
            auto n = pat.size() - i;
            cb.prepare(n);
            cb.consume(i);
            BOOST_TEST_EQ(cb.size(), 0);
            cb.commit(n);
            BOOST_TEST_EQ(
                test::make_string(cb.data()),
                pat.substr(i, n));
        }
    }

    void
    run()
    {
        testMembers();
        testBuffer();
        testInterleavedReadWrite();
    }
};

TEST_SUITE(
    circular_buffer_test,
    "boost.capy.buffers.circular_buffer");

} // capy
} // boost
