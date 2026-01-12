//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Test that header file is self-contained.
#include <boost/capy/buffers/read_source.hpp>

#include <boost/capy/buffers/error.hpp>
#include <boost/capy/buffers/slice.hpp>

#include <boost/core/detail/static_assert.hpp>
#include <boost/core/detail/string_view.hpp>

#include "test_suite.hpp"

namespace boost {
namespace capy {
namespace buffers {

namespace {

struct test_source
{
    core::string_view s_;
    std::size_t nread_ = 0;

    explicit test_source(
        core::string_view s)
        : s_(s)
    {
    }

    template<class MutableBufferSequence>
    std::size_t read(
        MutableBufferSequence const& dest,
        system::error_code& ec)
    {
        if(nread_ >= s_.size())
        {
            ec = error::eof;
            return 0;
        }
        auto const n = copy(dest, sans_prefix(
            const_buffer(s_.data(), s_.size()), nread_));
        nread_ += n;
        if(nread_ >= s_.size())
            ec = error::eof;
        return n;
    }
};

// has_sizee
struct test_source2 : test_source
{
    explicit test_source2(
        core::string_view s)
        : test_source(s)
    {
    }

    std::uint64_t
    size() const noexcept
    {
        return s_.size();
    }
};

// has_rewind
struct test_source3 : test_source
{
    explicit test_source3(
        core::string_view s)
        : test_source(s)
    {
    }

    void rewind()
    {
        nread_ = 0;
    }
};

// has_size, has_rewind
struct test_source4 : test_source3
{
    explicit test_source4(
        core::string_view s)
        : test_source3(s)
    {
    }

    std::uint64_t
    size() const noexcept
    {
        return s_.size();
    }
};

struct not_source1
{
};

struct not_source2
{
    void read(system::error_code&)
    {
    }
};

struct data_source
{
    const_buffer data() const noexcept
    {
        return {};
    }
};

} // (anon)

BOOST_CORE_STATIC_ASSERT(  is_read_source<test_source>::value);
BOOST_CORE_STATIC_ASSERT(! is_read_source<int>::value);
BOOST_CORE_STATIC_ASSERT(! is_read_source<not_source1>::value);
BOOST_CORE_STATIC_ASSERT(! is_read_source<not_source2>::value);
BOOST_CORE_STATIC_ASSERT(! is_read_source<data_source>::value);

BOOST_CORE_STATIC_ASSERT(! has_size<test_source>::value);
BOOST_CORE_STATIC_ASSERT(  has_size<test_source2>::value);
BOOST_CORE_STATIC_ASSERT(! has_size<test_source3>::value);
BOOST_CORE_STATIC_ASSERT(  has_size<test_source4>::value);

BOOST_CORE_STATIC_ASSERT(! has_rewind<test_source>::value);
BOOST_CORE_STATIC_ASSERT(! has_rewind<test_source2>::value);
BOOST_CORE_STATIC_ASSERT(  has_rewind<test_source3>::value);
BOOST_CORE_STATIC_ASSERT(  has_rewind<test_source4>::value);

struct read_source_test
{
    void run()
    {
    }
};

TEST_SUITE(
    read_source_test,
    "boost.capy.buffers.read_source");

} // buffers
} // capy
} // boost
