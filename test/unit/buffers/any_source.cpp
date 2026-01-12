//
// Copyright (c) 2025 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Test that header file is self-contained.
#include <boost/capy/buffers/any_source.hpp>

#include <boost/core/detail/static_assert.hpp>
#include <boost/core/detail/string_view.hpp>
#include <boost/system/error_code.hpp>

#include "test_suite.hpp"

namespace boost {
namespace capy {
namespace buffers {

namespace {

struct test_data_source
{
    core::string_view s_;

    test_data_source(
        core::string_view s)
        : s_(s)
    {
    }

    buffers::const_buffer
    data() const
    {
        return { s_.data(), s_.size() };
    }
};

struct test_read_source
{
    core::string_view s_;
    std::size_t nread_ = 0;
    system::error_code ec_;

    explicit test_read_source(
        core::string_view s,
        system::error_code ec = {})
        : s_(s)
        , ec_(ec)
    {
    }

    void rewind()
    {
        nread_ = 0;
    }

    template<class MutableBufferSequence>
    std::size_t read(
        MutableBufferSequence const& dest,
        system::error_code& ec)
    {
        if( nread_ > 0 &&
            ec_.failed())
        {
            // fail on second read
            ec = ec_;
            return 0;
        }
        auto n = buffers::copy(
            dest,
            buffers::const_buffer(
                s_.data() + nread_,
                s_.size() - nread_));
        nread_ += n;
        if(nread_ >= s_.size())
        {
            if(ec_.failed())
                ec = ec_;
            else
                ec = error::eof;
        }
        else
        {
            ec = {};
        }
        return n;
    }
};

BOOST_CORE_STATIC_ASSERT(  is_data_source<test_data_source>::value);
BOOST_CORE_STATIC_ASSERT(! is_data_source<test_read_source>::value);
BOOST_CORE_STATIC_ASSERT(  is_read_source<test_read_source>::value);
BOOST_CORE_STATIC_ASSERT(! is_read_source<test_data_source>::value);

} // (anon)

BOOST_CORE_STATIC_ASSERT(std::is_move_constructible<any_source>::value);
BOOST_CORE_STATIC_ASSERT(std::is_copy_constructible<any_source>::value);
BOOST_CORE_STATIC_ASSERT(std::is_constructible<any_source, any_source const&>::value);
BOOST_CORE_STATIC_ASSERT(std::is_constructible<any_source, test_data_source>::value);
BOOST_CORE_STATIC_ASSERT(std::is_constructible<any_source, test_read_source>::value);
BOOST_CORE_STATIC_ASSERT(std::is_move_assignable<any_source>::value);
BOOST_CORE_STATIC_ASSERT(std::is_copy_assignable<any_source>::value);
BOOST_CORE_STATIC_ASSERT(std::is_assignable<any_source, any_source>::value);
BOOST_CORE_STATIC_ASSERT(std::is_assignable<any_source, any_source const&>::value);
BOOST_CORE_STATIC_ASSERT(std::is_assignable<any_source, test_data_source>::value);
BOOST_CORE_STATIC_ASSERT(std::is_assignable<any_source, test_read_source>::value);

struct any_source_test
{
    void checkEmpty(any_source& s)
    {
        BOOST_TEST_EQ(s.has_size(), true);
        BOOST_TEST_EQ(s.has_buffers(), true);
        BOOST_TEST_EQ(s.size(), 0);
        BOOST_TEST_EQ(size(s.data()), 0);
        BOOST_TEST_NO_THROW(s.rewind());
        mutable_buffer mb;
        system::error_code ec;
        auto const nread = s.read(mb, ec);
        BOOST_TEST_EQ(nread, 0);
        BOOST_TEST(ec == error::eof);
        BOOST_TEST_NO_THROW(s.rewind());
    }

    void grind(
        any_source& b,
        core::string_view s0,
        system::error_code fec = {})
    {
        char buf[16];
        mutable_buffer mb(buf, sizeof(buf));
        for(std::size_t n = 1; n <= mb.size(); ++n)
        {
            std::string s;
            system::error_code ec;
            b.rewind();
            for(;;)
            {
                auto nread = b.read(mb, ec);
                s.append(buf, nread);
                if(ec == error::eof)
                {
                    BOOST_TEST(! fec.failed());
                    BOOST_TEST_EQ(s, s0);
                    break;
                }
                if(ec.failed())
                {
                    BOOST_TEST_EQ(ec, fec);
                    break;
                }
                BOOST_TEST_GT(nread, 0);
            }
        }
    }

    void testEmpty()
    {
        any_source b;
        checkEmpty(b);
        grind(b, "");
    }

    void testBuffers()
    {
        core::string_view s1("Hello, world!");
        core::string_view s2("Boost");

        any_source b1((test_data_source(s1)));
        BOOST_TEST_EQ(b1.has_size(), true);
        BOOST_TEST_EQ(b1.size(), s1.size());
        BOOST_TEST_EQ(b1.has_buffers(), true);
        BOOST_TEST_EQ(buffers::size(b1.data()), s1.size());
        BOOST_TEST_NO_THROW(b1.rewind());
        grind(b1, s1);

        any_source b2 = std::move(b1);
        checkEmpty(b1);
        BOOST_TEST_EQ(b2.has_size(), true);
        BOOST_TEST_EQ(b2.size(), s1.size());
        BOOST_TEST_EQ(b2.has_buffers(), true);
        BOOST_TEST_EQ(buffers::size(b2.data()), s1.size());
        BOOST_TEST_NO_THROW(b2.rewind());
        grind(b2, s1);

        b1 = test_data_source(s2);
        BOOST_TEST_EQ(b1.has_size(), true);
        BOOST_TEST_EQ(b1.size(), s2.size());
        BOOST_TEST_EQ(b1.has_buffers(), true);
        BOOST_TEST_EQ(buffers::size(b1.data()), s2.size());
        BOOST_TEST_NO_THROW(b1.rewind());
        grind(b1, s2);
    }

    void testStream()
    {
        core::string_view s1("Hello, world!");
        core::string_view s2("Boost");

        any_source b1((test_read_source(s1)));
        BOOST_TEST_EQ(b1.has_size(), false);
        BOOST_TEST_EQ(b1.has_buffers(), false);
        BOOST_TEST_THROWS(b1.size(), std::invalid_argument);
        BOOST_TEST_THROWS(b1.data(), std::invalid_argument);
        BOOST_TEST_NO_THROW(b1.rewind());
        grind(b1, s1);

        any_source b2 = std::move(b1);
        BOOST_TEST_EQ(b2.has_size(), false);
        BOOST_TEST_EQ(b2.has_buffers(), false);
        BOOST_TEST_THROWS(b2.size(), std::invalid_argument);
        BOOST_TEST_THROWS(b2.data(), std::invalid_argument);
        BOOST_TEST_NO_THROW(b2.rewind());
        checkEmpty(b1);
        grind(b2, s1);

        b1 = test_read_source(s2);
        BOOST_TEST_EQ(b1.has_size(), false);
        BOOST_TEST_EQ(b1.has_buffers(), false);
        BOOST_TEST_THROWS(b1.size(), std::invalid_argument);
        BOOST_TEST_THROWS(b1.data(), std::invalid_argument);
        BOOST_TEST_NO_THROW(b1.rewind());
        grind(b1, s2);

        // sized source
        b2 = any_source(s2.size(), test_read_source(s2));
        BOOST_TEST_EQ(b2.has_size(), true);
        BOOST_TEST_EQ(b2.has_buffers(), false);
        BOOST_TEST_EQ(b2.size(), s2.size());
        BOOST_TEST_THROWS(b1.data(), std::invalid_argument);
        BOOST_TEST_NO_THROW(b2.rewind());
        grind(b2, s2);
    }

    void testFail()
    {
        core::string_view s1("Hello, world!");
        auto fec = make_error_code(
            boost::system::errc::address_in_use );
        any_source b1((test_read_source(s1, fec)));
        BOOST_TEST_EQ(b1.has_size(), false);
        BOOST_TEST_EQ(b1.has_buffers(), false);
        BOOST_TEST_THROWS(b1.size(), std::invalid_argument);
        BOOST_TEST_THROWS(b1.data(), std::invalid_argument);
        BOOST_TEST_NO_THROW(b1.rewind());
        grind(b1, s1, fec);
    }

    void run()
    {
        testEmpty();
        testBuffers();
        testStream();
        testFail();
    }
};

TEST_SUITE(
    any_source_test,
    "boost.capy.buffers.any_source");

} // buffers
} // capy
} // boost