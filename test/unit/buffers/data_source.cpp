//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Test that header file is self-contained.
#include <boost/capy/buffers/data_source.hpp>

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

    test_source(core::string_view s)
        : s_(s)
    {
    }

    const_buffer data() const noexcept
    {
        return { s_.data(), s_.size() };
    }
};

struct not_source1
{
};

struct not_source2
{
    core::string_view data() const noexcept
    {
        return {};
    }
};

struct read_source
{
    template<class MutableBufferSequence>
    std::size_t read(
        MutableBufferSequence const&,
        system::error_code&);
};

// blows up in gcc 5,6,7 without workarounds
struct gcc_banger
{
    template<class T,
        typename std::enable_if<
            is_data_source<typename std::decay<T>::type>::value,
        int>::type = 0>
    gcc_banger(T&&);
    
    gcc_banger(gcc_banger&&) = default;
};

} // (anon)

BOOST_CORE_STATIC_ASSERT(  is_data_source<test_source>::value);
BOOST_CORE_STATIC_ASSERT(! is_data_source<int>::value);
BOOST_CORE_STATIC_ASSERT(! is_data_source<not_source1>::value);
BOOST_CORE_STATIC_ASSERT(! is_data_source<not_source2>::value);
BOOST_CORE_STATIC_ASSERT(! is_data_source<read_source>::value);
BOOST_CORE_STATIC_ASSERT(! is_data_source<gcc_banger>::value);

struct data_source_test
{
    void run()
    {
    }
};

TEST_SUITE(
    data_source_test,
    "boost.capy.buffers.data_source");

} // buffers
} // capy
} // boost
