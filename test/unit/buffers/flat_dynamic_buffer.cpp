//
// Copyright (c) 2023 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Test that header file is self-contained.
#include <boost/capy/buffers/flat_dynamic_buffer.hpp>

#include <boost/capy/concept/dynamic_buffer.hpp>

#include "test/unit/test_dynamic_buffer.hpp"
#include "test_buffers.hpp"

namespace boost {
namespace capy {

static_assert(DynamicBuffer<flat_dynamic_buffer>);

struct flat_dynamic_buffer_test
{
    void
    testMembers()
    {
        std::string pat = test_pattern();

        // flat_dynamic_buffer()
        {
            flat_dynamic_buffer fb;
            BOOST_TEST_EQ(fb.size(), 0);
            BOOST_TEST_EQ(fb.max_size(), 0);
            BOOST_TEST_EQ(fb.capacity(), 0);
        }

        // flat_dynamic_buffer( void*, size_t )
        {
            std::string s = pat;
            flat_dynamic_buffer fb(&s[0], s.size());
            BOOST_TEST_EQ(fb.size(), 0);
            BOOST_TEST_EQ(fb.max_size(), s.size());
            BOOST_TEST_EQ(fb.capacity(), s.size());
        }

        // flat_dynamic_buffer( void*, size_t, size_t )
        {
            std::string s = pat;
            flat_dynamic_buffer fb(&s[0], s.size(), 6);
            BOOST_TEST_EQ(fb.size(), 6);
            BOOST_TEST_EQ(fb.max_size(), s.size());
            BOOST_TEST_EQ(fb.capacity(), s.size() - 6);
        }
        {
            std::string s = pat;
            BOOST_TEST_THROWS(
                flat_dynamic_buffer(&s[0], s.size(),
                    s.size() + 1),
                std::invalid_argument);
        }

        // flat_dynamic_buffer( flat_dynamic_buffer const& )
        {
            std::string s = pat;
            flat_dynamic_buffer fb0(&s[0], s.size());
            flat_dynamic_buffer fb1(fb0);
            BOOST_TEST_EQ(fb1.size(), fb0.size());
            BOOST_TEST_EQ(fb1.max_size(), fb0.max_size());
            BOOST_TEST_EQ(fb1.capacity(), fb0.capacity());
        }

        // operator=( flat_dynamic_buffer const& )
        {
            std::string s = pat;
            flat_dynamic_buffer fb0(&s[0], s.size());
            flat_dynamic_buffer fb1;
            fb1 = fb0;
            BOOST_TEST_EQ(fb1.size(), fb0.size());
            BOOST_TEST_EQ(fb1.max_size(), fb0.max_size());
            BOOST_TEST_EQ(fb1.capacity(), fb0.capacity());
        }

        // prepare( std::size_t )
        {
            std::string s = pat;
            flat_dynamic_buffer fb(&s[0], s.size());
            BOOST_TEST_THROWS(
                fb.prepare(s.size() + 1),
                std::invalid_argument);
        }
        {
            std::string s = pat;
            flat_dynamic_buffer fb(&s[0], s.size(), 6);
            BOOST_TEST_THROWS(
                fb.prepare(s.size() + 1),
                std::invalid_argument);

            BOOST_TEST_EQ(fb.max_size(), s.size());
            BOOST_TEST_EQ(
                fb.size() + fb.capacity(),
                fb.max_size());
        }

        // commit( std::size_t )
        {
            std::string s = pat;
            for(std::size_t i = 0;
                i <= pat.size(); ++i)
            {
                flat_dynamic_buffer fb(
                    &s[0], s.size());
                fb.prepare(s.size());
                fb.commit(i);
                BOOST_TEST_EQ(
                    test::make_string(fb.data()),
                    pat.substr(0, i));
            }
        }

        // consume( std::size_t )
        {
            std::string s = pat;
            flat_dynamic_buffer fb(&s[0], s.size(), s.size());
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
            flat_dynamic_buffer fb(&s[0], s.size(), 6);

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
    testGrind()
    {
        std::string storage(64, '\0');
        auto r = test::grind_dynamic_buffer([&] {
            std::fill(storage.begin(), storage.end(), '\0');
            return flat_dynamic_buffer(&storage[0], storage.size());
        });
        BOOST_TEST(r.success);
    }

    void
    run()
    {
        testMembers();
        testGrind();
    }
};

TEST_SUITE(
    flat_dynamic_buffer_test,
    "boost.capy.buffers.flat_dynamic_buffer");

} // capy
} // boost
