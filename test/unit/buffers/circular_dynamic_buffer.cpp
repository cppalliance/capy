//
// Copyright (c) 2023 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Test that header file is self-contained.
#include <boost/capy/buffers/circular_dynamic_buffer.hpp>

#include <boost/capy/concept/dynamic_buffer.hpp>

#include "test/unit/test_dynamic_buffer.hpp"
#include "test_buffers.hpp"

#include <algorithm>
#include <cstring>
#include <random>
#include <vector>

namespace boost {
namespace capy {

static_assert(DynamicBuffer<circular_dynamic_buffer>);

struct circular_dynamic_buffer_test
{
    void
    testMembers()
    {
        std::string pat = test_pattern();

        // circular_dynamic_buffer()
        {
            circular_dynamic_buffer cb;
            BOOST_TEST_EQ(cb.size(), 0);
        }

        // circular_dynamic_buffer( void*, std::size_t )
        {
            circular_dynamic_buffer cb(
                &pat[0], pat.size());
            BOOST_TEST_EQ(cb.size(), 0);
            BOOST_TEST_EQ(cb.capacity(), pat.size());
            BOOST_TEST_EQ(cb.max_size(), pat.size());
        }

        // circular_dynamic_buffer( void*, std::size_t, std:size_t )
        {
            circular_dynamic_buffer cb(
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
                circular_dynamic_buffer(
                    &pat[0], pat.size(), 600),
                std::exception);
        }

        // circular_dynamic_buffer( circular_dynamic_buffer const& )
        {
            circular_dynamic_buffer cb0(&pat[0], pat.size());
            circular_dynamic_buffer cb1(cb0);
            BOOST_TEST_EQ(cb1.size(), cb0.size());
            BOOST_TEST_EQ(cb1.capacity(), cb0.capacity());
            BOOST_TEST_EQ(cb1.max_size(), cb0.max_size());
        }

        // operator=( circular_dynamic_buffer const& )
        {
            circular_dynamic_buffer cb0(&pat[0], pat.size());
            circular_dynamic_buffer cb1;
            cb1 = cb0;
            BOOST_TEST_EQ(cb1.size(), cb0.size());
            BOOST_TEST_EQ(cb1.capacity(), cb0.capacity());
            BOOST_TEST_EQ(cb1.max_size(), cb0.max_size());
        }

        // prepare( std::size_t )
        {
            circular_dynamic_buffer cb(&pat[0], pat.size());
            BOOST_TEST_THROWS(
                cb.prepare(cb.capacity() + 1),
                std::length_error);
        }

        // commit( std::size_t )
        {
            circular_dynamic_buffer cb(&pat[0], pat.size());
            auto n = pat.size() / 2;
            cb.prepare(pat.size());
            cb.commit(n);
            BOOST_TEST_EQ(
                test::make_string(cb.data()),
                pat.substr(0, n));
        }
    }

    // Helper: total size of a const_buffer_pair
    static std::size_t
    bp_total_size(const_buffer_pair const& bp) noexcept
    {
        return bp[0].size() + bp[1].size();
    }

    // Helper: total size of a mutable_buffer_pair
    static std::size_t
    bp_total_size(mutable_buffer_pair const& bp) noexcept
    {
        return bp[0].size() + bp[1].size();
    }

    // Helper: write a string into the buffer via prepare/commit
    static void
    write_string(
        circular_dynamic_buffer& cb,
        char const* s,
        std::size_t len)
    {
        auto mb = cb.prepare(len);
        std::size_t copied = 0;
        if(mb[0].size() > 0)
        {
            auto n = (std::min)(mb[0].size(), len);
            std::memcpy(mb[0].data(), s, n);
            copied += n;
        }
        if(mb[1].size() > 0 && copied < len)
        {
            auto n = (std::min)(mb[1].size(), len - copied);
            std::memcpy(mb[1].data(), s + copied, n);
            copied += n;
        }
        cb.commit(len);
    }

    // Helper: read all readable bytes into a string
    static std::string
    read_string(circular_dynamic_buffer const& cb)
    {
        auto d = cb.data();
        std::string result;
        result.append(
            static_cast<char const*>(d[0].data()),
            d[0].size());
        result.append(
            static_cast<char const*>(d[1].data()),
            d[1].size());
        return result;
    }

    void
    testDataWrapped()
    {
        char buf[8];
        circular_dynamic_buffer cb{buf, 8};

        write_string(cb, "ABCDEF", 6);
        cb.consume(5);
        BOOST_TEST(cb.size() == 1);

        write_string(cb, "GHIJK", 5);
        BOOST_TEST(cb.size() == 6);

        auto d = cb.data();
        BOOST_TEST(d[0].size() == 3);
        BOOST_TEST(d[1].size() == 3);
        BOOST_TEST(bp_total_size(d) == 6);

        std::string s = read_string(cb);
        BOOST_TEST(s == "FGHIJK");
    }

    void
    testPrepareTooLargeWithExistingData()
    {
        char buf[16];
        circular_dynamic_buffer cb{buf, 16};
        write_string(cb, "ABCDE", 5);
        BOOST_TEST_THROWS(cb.prepare(12), std::length_error);
        auto mb = cb.prepare(11);
        BOOST_TEST(bp_total_size(mb) == 11);
    }

    void
    testPrepareWrapped()
    {
        char buf[8];
        circular_dynamic_buffer cb{buf, 8};

        // Partial consume keeps in_pos_ at 5
        write_string(cb, "ABCDEF", 6);
        cb.consume(5);

        // pos=(5+1)%8=6, 6+5=11>8 => wraps
        auto mb = cb.prepare(5);
        BOOST_TEST(mb[0].size() == 2);
        BOOST_TEST(mb[1].size() == 3);
        BOOST_TEST(bp_total_size(mb) == 5);
    }

    void
    testCommitMoreThanPrepared()
    {
        char buf[32];
        circular_dynamic_buffer cb{buf, 32};
        cb.prepare(10);
        cb.commit(100);
        BOOST_TEST(cb.size() == 10);
    }

    void
    testCommitZero()
    {
        char buf[32];
        circular_dynamic_buffer cb{buf, 32};
        cb.prepare(10);
        cb.commit(0);
        BOOST_TEST(cb.size() == 0);
    }

    void
    testCommitClearsOutSize()
    {
        char buf[32];
        circular_dynamic_buffer cb{buf, 32};
        cb.prepare(10);
        cb.commit(5);
        cb.commit(5);
        BOOST_TEST(cb.size() == 5);
    }

    void
    testConsumeMoreThanSize()
    {
        char buf[32];
        circular_dynamic_buffer cb{buf, 32};
        write_string(cb, "ABC", 3);
        cb.consume(100);
        BOOST_TEST(cb.size() == 0);
    }

    void
    testConsumeZero()
    {
        char buf[32];
        circular_dynamic_buffer cb{buf, 32};
        write_string(cb, "ABCDE", 5);
        cb.consume(0);
        BOOST_TEST(cb.size() == 5);
        BOOST_TEST(read_string(cb) == "ABCDE");
    }

    void
    testConsumeAllWithPreparedBuffer()
    {
        char buf[16];
        circular_dynamic_buffer cb{buf, 16};
        write_string(cb, "ABCDE", 5);
        cb.prepare(5);
        cb.consume(5);
        BOOST_TEST(cb.size() == 0);
        cb.commit(3);
        BOOST_TEST(cb.size() == 3);
    }

    void
    testConsumeAllNoPrepareResetsPos()
    {
        char buf[16];
        circular_dynamic_buffer cb{buf, 16};

        write_string(cb, "ABCDE", 5);
        cb.consume(3);
        cb.consume(2);
        BOOST_TEST(cb.size() == 0);

        auto mb = cb.prepare(16);
        BOOST_TEST(mb[0].size() == 16);
        BOOST_TEST(mb[1].size() == 0);
    }

    void
    testWrapAroundRoundTrip()
    {
        char buf[8];
        circular_dynamic_buffer cb{buf, 8};

        // Partial consume to keep in_pos_ at 6
        write_string(cb, "ABCDEFG", 7);
        cb.consume(6);

        write_string(cb, "123456", 6);
        BOOST_TEST(cb.size() == 7);

        auto d = cb.data();
        // in_pos_=6, in_len_=7 => wraps
        BOOST_TEST(d[0].size() == 2);
        BOOST_TEST(d[1].size() == 5);
        BOOST_TEST(read_string(cb) == "G123456");

        cb.consume(3);
        BOOST_TEST(cb.size() == 4);
        BOOST_TEST(read_string(cb) == "3456");
    }

    void
    testCapacityOne()
    {
        char buf[1];
        circular_dynamic_buffer cb{buf, 1};
        BOOST_TEST(cb.max_size() == 1);

        write_string(cb, "X", 1);
        BOOST_TEST(cb.size() == 1);
        BOOST_TEST(read_string(cb) == "X");

        cb.consume(1);
        BOOST_TEST(cb.size() == 0);

        BOOST_TEST_THROWS(cb.prepare(2), std::length_error);
    }

    void
    testPrepareZero()
    {
        char buf[16];
        circular_dynamic_buffer cb{buf, 16};
        auto mb = cb.prepare(0);
        BOOST_TEST(bp_total_size(mb) == 0);
        cb.commit(0);
        BOOST_TEST(cb.size() == 0);
    }

    void
    testMultipleCycles()
    {
        char buf[10];
        circular_dynamic_buffer cb{buf, 10};

        for(int cycle = 0; cycle < 20; ++cycle)
        {
            std::string msg = "C";
            msg += std::to_string(cycle % 10);
            auto len = msg.size();
            BOOST_TEST(len <= 10);
            write_string(cb, msg.c_str(), len);
            BOOST_TEST(read_string(cb) == msg);
            cb.consume(len);
            BOOST_TEST(cb.size() == 0);
        }
    }

    void
    testFuzz()
    {
        constexpr std::size_t cap = 64;
        char buf[cap];
        circular_dynamic_buffer cb{buf, cap};

        std::vector<unsigned char> model;

        std::mt19937 rng{42};
        std::uniform_int_distribution<int> action_dist{0, 2};
        std::uniform_int_distribution<int> byte_dist{0, 255};

        for(int iter = 0; iter < 2000; ++iter)
        {
            int action = action_dist(rng);

            if(action == 0)
            {
                std::size_t avail = cap - model.size();
                if(avail == 0)
                    continue;
                std::uniform_int_distribution<std::size_t> sz_dist{1, avail};
                std::size_t n = sz_dist(rng);

                std::vector<unsigned char> data(n);
                for(auto& b : data)
                    b = static_cast<unsigned char>(byte_dist(rng));

                auto mb = cb.prepare(n);
                std::size_t copied = 0;
                if(mb[0].size() > 0)
                {
                    auto chunk = (std::min)(mb[0].size(), n);
                    std::memcpy(mb[0].data(), data.data(), chunk);
                    copied += chunk;
                }
                if(mb[1].size() > 0 && copied < n)
                {
                    auto chunk = (std::min)(mb[1].size(), n - copied);
                    std::memcpy(mb[1].data(), data.data() + copied, chunk);
                    copied += chunk;
                }
                cb.commit(n);
                model.insert(model.end(), data.begin(), data.end());
            }
            else if(action == 1)
            {
                if(model.empty())
                    continue;
                std::uniform_int_distribution<std::size_t> sz_dist{1, model.size()};
                std::size_t n = sz_dist(rng);
                cb.consume(n);
                model.erase(model.begin(), model.begin() + static_cast<std::ptrdiff_t>(n));
            }
            else
            {
                BOOST_TEST(cb.size() == model.size());
                auto d = cb.data();
                BOOST_TEST(bp_total_size(d) == model.size());

                std::string actual = read_string(cb);
                std::string expected(model.begin(), model.end());
                BOOST_TEST(actual == expected);
            }
        }

        BOOST_TEST(cb.size() == model.size());
        std::string actual = read_string(cb);
        std::string expected(model.begin(), model.end());
        BOOST_TEST(actual == expected);
    }

    void
    testCommitPartialThenPrepare()
    {
        char buf[16];
        circular_dynamic_buffer cb{buf, 16};

        cb.prepare(10);
        cb.commit(4);
        BOOST_TEST(cb.size() == 4);

        auto mb = cb.prepare(12);
        BOOST_TEST(bp_total_size(mb) == 12);
    }

    void
    testGrind()
    {
        std::string storage(64, '\0');
        auto r = test::grind_dynamic_buffer([&] {
            std::fill(storage.begin(), storage.end(), '\0');
            return circular_dynamic_buffer(&storage[0], storage.size());
        });
        BOOST_TEST(r.success);
    }

    void
    run()
    {
        testMembers();
        testGrind();

        testDataWrapped();
        testPrepareTooLargeWithExistingData();
        testPrepareWrapped();
        testPrepareZero();
        testCommitMoreThanPrepared();
        testCommitZero();
        testCommitClearsOutSize();
        testCommitPartialThenPrepare();
        testConsumeMoreThanSize();
        testConsumeZero();
        testConsumeAllWithPreparedBuffer();
        testConsumeAllNoPrepareResetsPos();
        testWrapAroundRoundTrip();
        testCapacityOne();
        testMultipleCycles();
        testFuzz();
    }
};

TEST_SUITE(
    circular_dynamic_buffer_test,
    "boost.capy.buffers.circular_dynamic_buffer");

} // capy
} // boost
