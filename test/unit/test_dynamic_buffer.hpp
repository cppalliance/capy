//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_TEST_UNIT_TEST_DYNAMIC_BUFFER_HPP
#define BOOST_CAPY_TEST_UNIT_TEST_DYNAMIC_BUFFER_HPP

#include <boost/capy/test/fuse.hpp>
#include <boost/capy/test/bufgrind.hpp>
#include <boost/capy/test/buffer_to_string.hpp>
#include <boost/capy/test/read_stream.hpp>
#include <boost/capy/test/write_stream.hpp>
#include <boost/capy/buffers/make_buffer.hpp>
#include <boost/capy/task.hpp>

#include "test_suite.hpp"

#include <string>
#include <string_view>

namespace boost {
namespace capy {
namespace test {

/** Exercises DynamicBuffer with all buffer split points.

    Uses bufgrind to test prepare/commit/consume/data/size
    operations at every possible split point of a small
    test string. Tests I/O round-trip via read_stream and
    write_stream with error injection.

    @param make_buffer_fn Factory returning a fresh dynamic buffer.

    @return The fuse result indicating success or failure.
*/
template<class F>
fuse::result
grind_dynamic_buffer(F&& make_buffer_fn)
{
    fuse f;
    return f.armed([&](fuse& f) -> task<> {
        std::string const data = "abcdefgh";
        auto cb = make_buffer(data);
        bufgrind bg(cb);

        while(bg)
        {
            auto [b1, b2] = co_await bg.next();
            BOOST_TEST_EQ(buffer_to_string(b1, b2), data);

            auto db = make_buffer_fn();

            // Read b1 into dynamic buffer via read_stream
            read_stream rs(f);
            rs.provide(std::string_view(
                static_cast<char const*>(b1.data()), b1.size()));

            if(buffer_size(b1) > 0)
            {
                auto mb = db.prepare(buffer_size(b1));
                auto [ec, n] = co_await rs.read_some(mb);
                if(ec.failed())
                    co_return;
                db.commit(n);
            }

            BOOST_TEST_EQ(db.size(), buffer_size(b1));

            // Write from dynamic buffer to write_stream
            write_stream ws(f);
            if(db.size() > 0)
            {
                auto [ec, n] = co_await ws.write_some(db.data());
                if(ec.failed())
                    co_return;
                BOOST_TEST_EQ(n, db.size());
            }

            // Verify round-trip
            BOOST_TEST_EQ(ws.data(), buffer_to_string(b1));

            db.consume(db.size());
            BOOST_TEST_EQ(db.size(), 0u);
        }
    });
}

} // test
} // capy
} // boost

#endif
