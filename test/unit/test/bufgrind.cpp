//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Test that header file is self-contained.
#include <boost/capy/test/bufgrind.hpp>

#include <boost/capy/buffers/make_buffer.hpp>
#include <boost/capy/concept/slice.hpp>
#include <boost/capy/task.hpp>
#include <boost/capy/test/buffer_to_string.hpp>
#include <boost/capy/test/fuse.hpp>
#include <boost/capy/test/read_stream.hpp>
#include <boost/capy/test/write_stream.hpp>

#include "test/unit/test_helpers.hpp"

#include <array>
#include <string>
#include <string_view>

namespace boost {
namespace capy {
namespace test {

class bufgrind_test
{
public:
    void
    testConstruct()
    {
        fuse f;
        auto r = f.inert([&](fuse&) {
            std::string data = "hello";
            auto cb = make_buffer(data);

            bufgrind bg(cb);
            BOOST_TEST(static_cast<bool>(bg));

            bufgrind bg2(cb, 2);
            BOOST_TEST(static_cast<bool>(bg2));
        });
        BOOST_TEST(r.success);
    }

    void
    testEmptyBuffer()
    {
        fuse f;
        auto r = f.inert([&](fuse&) -> task<> {
            const_buffer cb;
            bufgrind bg(cb);

            int count = 0;
            while(bg) {
                auto [b1, b2] = co_await bg.next();
                BOOST_TEST_EQ(buffer_size(b1.data()), 0u);
                BOOST_TEST_EQ(buffer_size(b2.data()), 0u);
                ++count;
            }
            BOOST_TEST_EQ(count, 1);
        });
        BOOST_TEST(r.success);
    }

    void
    testSingleByte()
    {
        fuse f;
        auto r = f.inert([&](fuse&) -> task<> {
            std::string data = "X";
            auto cb = make_buffer(data);
            bufgrind bg(cb);

            int count = 0;
            while(bg) {
                auto [b1, b2] = co_await bg.next();
                if(count == 0) {
                    BOOST_TEST_EQ(buffer_size(b1.data()), 0u);
                    BOOST_TEST_EQ(buffer_size(b2.data()), 1u);
                } else if(count == 1) {
                    BOOST_TEST_EQ(buffer_size(b1.data()), 1u);
                    BOOST_TEST_EQ(buffer_size(b2.data()), 0u);
                }
                ++count;
            }
            BOOST_TEST_EQ(count, 2);
        });
        BOOST_TEST(r.success);
    }

    void
    testAllSplits()
    {
        // Verify that all splits concatenate to original
        fuse f;
        auto r = f.inert([&](fuse&) -> task<> {
            std::string data = "hello";
            auto cb = make_buffer(data);
            bufgrind bg(cb);

            int count = 0;
            while(bg) {
                auto [b1, b2] = co_await bg.next();

                BOOST_TEST_EQ(buffer_to_string(b1.data(), b2.data()), data);
                BOOST_TEST_EQ(buffer_size(b1.data()), static_cast<std::size_t>(count));
                BOOST_TEST_EQ(buffer_size(b2.data()), data.size() - count);
                ++count;
            }
            BOOST_TEST_EQ(count, 6);
        });
        BOOST_TEST(r.success);
    }

    void
    testStepSize()
    {
        fuse f;
        auto r = f.inert([&](fuse&) -> task<> {
            std::string data = "0123456789";
            auto cb = make_buffer(data);
            bufgrind bg(cb, 3);

            std::vector<std::size_t> positions;
            while(bg) {
                auto [b1, b2] = co_await bg.next();
                positions.push_back(buffer_size(b1.data()));
            }

            // Expect: 0, 3, 6, 9, 10 (always includes final position)
            BOOST_TEST_EQ(positions.size(), 5u);
            BOOST_TEST_EQ(positions[0], 0u);
            BOOST_TEST_EQ(positions[1], 3u);
            BOOST_TEST_EQ(positions[2], 6u);
            BOOST_TEST_EQ(positions[3], 9u);
            BOOST_TEST_EQ(positions[4], 10u);
        });
        BOOST_TEST(r.success);
    }

    void
    testStepSizeAligned()
    {
        // When step divides size evenly
        fuse f;
        auto r = f.inert([&](fuse&) -> task<> {
            std::string data = "012345";
            auto cb = make_buffer(data);
            bufgrind bg(cb, 2);

            std::vector<std::size_t> positions;
            while(bg) {
                auto [b1, b2] = co_await bg.next();
                positions.push_back(buffer_size(b1.data()));
            }

            // Expect: 0, 2, 4, 6
            BOOST_TEST_EQ(positions.size(), 4u);
            BOOST_TEST_EQ(positions[0], 0u);
            BOOST_TEST_EQ(positions[1], 2u);
            BOOST_TEST_EQ(positions[2], 4u);
            BOOST_TEST_EQ(positions[3], 6u);
        });
        BOOST_TEST(r.success);
    }

    void
    testStepZeroTreatedAsOne()
    {
        fuse f;
        auto r = f.inert([&](fuse&) -> task<> {
            std::string data = "abc";
            auto cb = make_buffer(data);
            bufgrind bg(cb, 0);

            int count = 0;
            while(bg) {
                co_await bg.next();
                ++count;
            }
            BOOST_TEST_EQ(count, 4);
        });
        BOOST_TEST(r.success);
    }

    void
    testStepLargerThanSize()
    {
        fuse f;
        auto r = f.inert([&](fuse&) -> task<> {
            std::string data = "abc";
            auto cb = make_buffer(data);
            bufgrind bg(cb, 100);

            std::vector<std::size_t> positions;
            while(bg) {
                auto [b1, b2] = co_await bg.next();
                positions.push_back(buffer_size(b1.data()));
            }

            // Expect: 0, 3 (clamped to size, then final)
            BOOST_TEST_EQ(positions.size(), 2u);
            BOOST_TEST_EQ(positions[0], 0u);
            BOOST_TEST_EQ(positions[1], 3u);
        });
        BOOST_TEST(r.success);
    }

    void
    testMutableBuffer()
    {
        // Verify mutable buffers yield mutable slices
        fuse f;
        auto r = f.inert([&](fuse&) -> task<> {
            char data[] = "hello";
            mutable_buffer mb(data, 5);
            bufgrind bg(mb);

            while(bg) {
                auto [b1, b2] = co_await bg.next();

                // Slices over a mutable input model MutableSlice
                static_assert(MutableSlice<decltype(b1)>);
                static_assert(MutableSlice<decltype(b2)>);
                BOOST_TEST_EQ(buffer_size(b1.data()) + buffer_size(b2.data()), 5u);
            }
        });
        BOOST_TEST(r.success);
    }

    void
    testConstBuffer()
    {
        // Verify const buffers yield const slices
        fuse f;
        auto r = f.inert([&](fuse&) -> task<> {
            std::string_view data = "hello";
            auto cb = make_buffer(data);
            bufgrind bg(cb);

            while(bg) {
                auto [b1, b2] = co_await bg.next();

                // Slices over a const-only input model Slice but not
                // MutableSlice.
                static_assert(Slice<decltype(b1)>);
                static_assert(!MutableSlice<decltype(b1)>);
                static_assert(Slice<decltype(b2)>);
                static_assert(!MutableSlice<decltype(b2)>);
                BOOST_TEST_EQ(buffer_size(b1.data()) + buffer_size(b2.data()), 5u);
            }
        });
        BOOST_TEST(r.success);
    }

    void
    testBufferSequence()
    {
        // Test with multi-buffer sequence
        fuse f;
        auto r = f.inert([&](fuse&) -> task<> {
            std::array<const_buffer, 3> buffers = {{
                make_buffer("abc", 3),
                make_buffer("de", 2),
                make_buffer("f", 1)
            }};

            bufgrind bg(buffers);

            int count = 0;
            while(bg) {
                auto [b1, b2] = co_await bg.next();

                // Verify concatenation reconstructs original
                BOOST_TEST_EQ(buffer_to_string(b1.data(), b2.data()), "abcdef");
                ++count;
            }
            BOOST_TEST_EQ(count, 7);
        });
        BOOST_TEST(r.success);
    }

    void
    testWithReadStream()
    {
        // Use bufgrind with read_stream to test reading split buffers
        fuse f;
        auto r = f.inert([&](fuse&) -> task<> {
            std::string data = "hello world";
            auto cb = make_buffer(data);
            bufgrind bg(cb, 3);

            while(bg) {
                auto [b1, b2] = co_await bg.next();

                // Set up read_stream with data matching b1 size
                read_stream rs(f);
                rs.provide(buffer_to_string(b1.data()));

                // Read into a destination buffer
                if(buffer_size(b1.data()) > 0) {
                    std::string dest;
                    dest.resize(buffer_size(b1.data()));
                    auto [ec, n] = co_await rs.read_some(make_buffer(dest));
                    BOOST_TEST(! ec);
                    BOOST_TEST_EQ(n, buffer_size(b1.data()));
                }
            }
        });
        BOOST_TEST(r.success);
    }

    void
    testWithWriteStream()
    {
        // Use bufgrind with write_stream to test writing split buffers
        fuse f;
        auto r = f.inert([&](fuse&) -> task<> {
            std::string data = "hello";
            auto cb = make_buffer(data);
            bufgrind bg(cb);

            while(bg) {
                auto [b1, b2] = co_await bg.next();

                // Write b1 then b2 to stream
                write_stream ws(f);

                if(buffer_size(b1.data()) > 0) {
                    auto [ec1, n1] = co_await ws.write_some(b1.data());
                    BOOST_TEST(! ec1);
                    BOOST_TEST_EQ(n1, buffer_size(b1.data()));
                }

                if(buffer_size(b2.data()) > 0) {
                    auto [ec2, n2] = co_await ws.write_some(b2.data());
                    BOOST_TEST(! ec2);
                    BOOST_TEST_EQ(n2, buffer_size(b2.data()));
                }

                // Verify total written equals original
                BOOST_TEST_EQ(ws.data(), buffer_to_string(b1.data(), b2.data()));
            }
        });
        BOOST_TEST(r.success);
    }

    void
    testSplitIntegrity()
    {
        // Comprehensive test that b1 + b2 always reconstructs original
        // via write_stream to exercise the stream for each split
        fuse f;
        auto r = f.inert([&](fuse&) -> task<> {
            std::string original = "The quick brown fox";
            auto cb = make_buffer(original);

            for(std::size_t step = 1; step <= original.size() + 1; ++step) {
                bufgrind bg(cb, step);

                while(bg) {
                    auto [b1, b2] = co_await bg.next();

                    // Write both parts through stream
                    write_stream ws(f);
                    if(buffer_size(b1.data()) > 0) {
                        auto [ec, n] = co_await ws.write_some(b1.data());
                        BOOST_TEST(! ec);
                    }
                    if(buffer_size(b2.data()) > 0) {
                        auto [ec, n] = co_await ws.write_some(b2.data());
                        BOOST_TEST(! ec);
                    }

                    BOOST_TEST_EQ(ws.data(), buffer_to_string(b1.data(), b2.data()));
                    BOOST_TEST_EQ(ws.data(), original);
                }
            }
        });
        BOOST_TEST(r.success);
    }

    void
    run()
    {
        testConstruct();
        testEmptyBuffer();
        testSingleByte();
        testAllSplits();
        testStepSize();
        testStepSizeAligned();
        testStepZeroTreatedAsOne();
        testStepLargerThanSize();
        testMutableBuffer();
        testConstBuffer();
        testBufferSequence();
        testWithReadStream();
        testWithWriteStream();
        testSplitIntegrity();
    }
};

TEST_SUITE(bufgrind_test, "boost.capy.test.bufgrind");

} // test
} // capy
} // boost
