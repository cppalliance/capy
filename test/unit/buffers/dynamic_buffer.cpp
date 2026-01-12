//
// Copyright (c) 2023 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Test that header file is self-contained.
#include <boost/capy/buffers/dynamic_buffer.hpp>

#include <boost/capy/buffers/circular_buffer.hpp>
#include <boost/static_assert.hpp>
#include "test_buffers.hpp"

namespace boost {
namespace capy {
namespace buffers {

struct any_dynamic_buffer_test
{
    BOOST_STATIC_ASSERT(
        is_dynamic_buffer<
            any_dynamic_buffer>::value);

    void
    testAny()
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
            auto db = make_any(circular_buffer(
                &s[0], s.size()));
            if( j < pat.size() &&
                i > 0)
            {
                db.prepare(i);
                db.commit(i);
                db.consume(i - 1);
                db.commit(copy(
                    db.prepare(j),
                    make_buffer(
                        pat.data(),
                        pat.size())));
                db.consume(1);
            }
            else
            {
                db.commit(copy(
                    db.prepare(j),
                    make_buffer(
                        pat.data(),
                        pat.size())));
            }
            db.commit(copy(
                db.prepare(pat.size() - j),
                make_buffer(
                    pat.data() + j,
                    pat.size() - j)));
            BOOST_TEST_EQ(test::make_string(
                db.data()), pat);
            test::check_sequence(db.data(), pat);
            db.consume(k);
            BOOST_TEST_EQ(test::make_string(
                db.data()), pat.substr(k));
        }
    }

    void
    run()
    {
        testAny();
    }
};

TEST_SUITE(
    any_dynamic_buffer_test,
    "boost.capy.buffers.any_dynamic_buffer");

} // buffers
} // capy
} // boost
