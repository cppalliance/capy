//
// Copyright (c) 2023 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Test that header file is self-contained.
#include <boost/capy/buffers/flat_buffer.hpp>

#include <boost/capy/buffers/dynamic_buffer.hpp>
#include <boost/static_assert.hpp>
#include "test_buffers.hpp"

namespace boost {
namespace capy {

struct flat_buffer_test
{
    BOOST_STATIC_ASSERT(
        is_DynamicBuffer<
            flat_buffer>::value);

    void
    testMembers()
    {
        std::string pat = test_pattern();

        // flat_buffer()
        {
            flat_buffer fb;
            BOOST_TEST_EQ(fb.size(), 0);
            BOOST_TEST_EQ(fb.max_size(), 0);
            BOOST_TEST_EQ(fb.capacity(), 0);
        }

        // flat_buffer(void*, size_t)
        {
            std::string s = pat;
            flat_buffer fb(&s[0], s.size());
            BOOST_TEST_EQ(fb.size(), 0);
            BOOST_TEST_EQ(fb.max_size(), s.size());
            BOOST_TEST_EQ(fb.capacity(), s.size());
        }

        // flat_buffer(void*, size_t, size_t)
        {
            std::string s = pat;
            flat_buffer fb(&s[0], s.size(), 6);
            BOOST_TEST_EQ(fb.size(), 6);
            BOOST_TEST_EQ(fb.max_size(), s.size());
            BOOST_TEST_EQ(fb.capacity(), s.size() - 6);
        }
        {
            std::string s = pat;
            BOOST_TEST_THROWS(
                flat_buffer(&s[0], s.size(),
                    s.size() + 1),
                std::invalid_argument);
        }

        // flat_buffer(flat_buffer const&)
        {
            std::string s = pat;
            flat_buffer fb0(&s[0], s.size());
            flat_buffer fb1(fb0);
            BOOST_TEST_EQ(fb1.size(), fb0.size());
            BOOST_TEST_EQ(fb1.max_size(), fb0.max_size());
            BOOST_TEST_EQ(fb1.capacity(), fb0.capacity());
        }

        // operator=(flat_buffer const&)
        {
            std::string s = pat;
            flat_buffer fb0(&s[0], s.size());
            flat_buffer fb1;
            fb1 = fb0;
            BOOST_TEST_EQ(fb1.size(), fb0.size());
            BOOST_TEST_EQ(fb1.max_size(), fb0.max_size());
            BOOST_TEST_EQ(fb1.capacity(), fb0.capacity());
        }

        // prepare(std::size_t)
        {
            std::string s = pat;
            flat_buffer fb(&s[0], s.size());
            BOOST_TEST_THROWS(
                fb.prepare(s.size() + 1),
                std::invalid_argument);
        }
        {
            std::string s = pat;
            flat_buffer fb(&s[0], s.size(), 6);
            BOOST_TEST_THROWS(
                fb.prepare(s.size() + 1),
                std::invalid_argument);

            BOOST_TEST_EQ(fb.max_size(), s.size());
            BOOST_TEST_EQ(
                fb.size() + fb.capacity(),
                fb.max_size());
        }

        // commit(std::size_t)
        {
            std::string s = pat;
            for(std::size_t i = 0;
                i <= pat.size(); ++i)
            {
                flat_buffer fb(
                    &s[0], s.size());
                fb.prepare(s.size());
                fb.commit(i);
                BOOST_TEST_EQ(
                    test::make_string(fb.data()),
                    pat.substr(0, i));
            }
        }

        // consume(std::size_t)
        {
            std::string s = pat;
            flat_buffer fb(&s[0], s.size(), s.size());
            BOOST_TEST_EQ(
                test::make_string(fb.data()), pat);

            auto const cap = fb.capacity();

            while( fb.size() > 0 )
            {
                fb.prepare(fb.capacity());
                fb.consume(1);

                if( fb.size() > 0 )
                    BOOST_TEST_EQ(fb.capacity(), cap);
            }

            BOOST_TEST_EQ(fb.capacity(), s.size());
        }
        {
            std::string s = pat;
            flat_buffer fb(&s[0], s.size(), 6);

            auto const cap = fb.capacity();

            BOOST_TEST_NO_THROW(
                fb.prepare(fb.max_size() - fb.size()));

            fb.consume(1);
            BOOST_TEST_EQ(fb.capacity(), cap);
            BOOST_TEST_THROWS(
                fb.prepare(cap + 1),
                std::invalid_argument);
        }
    }

    void
    testBuffer()
    {
        auto const& pat = test_pattern();
        for(std::size_t i = 0;
            i <= pat.size(); ++i)
        {
            std::string s(pat.size(), 0);
            flat_buffer fb(&s[0], s.size());
            fb.commit(copy(
                fb.prepare(i),
                make_buffer(&pat[0], i)));
            fb.commit(copy(
                fb.prepare(pat.size() - i),
                make_buffer(&pat[i], pat.size() - i)));
            BOOST_TEST_EQ(test::make_string(
                fb.data()), pat);
            fb.consume(i);
            BOOST_TEST_EQ(test::make_string(
                fb.data()), pat.substr(i));
        }
    }

    void
    run()
    {
        testMembers();
        testBuffer();
    }
};

TEST_SUITE(
    flat_buffer_test,
    "boost.capy.buffers.flat_buffer");

} // capy
} // boost
