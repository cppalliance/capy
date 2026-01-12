//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Test that header file is self-contained.
#include <boost/capy/buffers/any_read_source.hpp>

#include <boost/capy/buffers/copy.hpp>
#include <boost/capy/buffers/slice.hpp>
#include <boost/capy/buffers/to_string.hpp>

#include <boost/core/span.hpp>
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

// has_size
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

// read from source into a string using a mutable buffers
template<
    class ReadSource,
    class MutableBufferSequence>
auto
read(
    ReadSource&& source,
    MutableBufferSequence const& temp) ->
        std::string
{
    std::string result;
    auto sr = make_any_read_source(
        std::forward<ReadSource>(source));
    system::error_code ec;
    for(;;)
    {
        auto nread = sr.read(temp, ec);
        result += to_string(prefix(temp, nread)); 
        if(ec == error::eof)
            break;
        if(! BOOST_TEST(! ec.failed()))
            break;
    }
    return result;
}

} // (anon)

BOOST_CORE_STATIC_ASSERT(is_read_source<any_read_source>::value);

struct any_read_source_test
{
    void run()
    {
        core::string_view s0 = 
            "Not him old music think his found enjoy merry. Listening acuteness "
            "dependent at or an. Apartments thoroughly unsatiable terminated "
            "sex how themselves. She are ten hours wrong walls stand early. "
            "Domestic perceive on an ladyship extended received do. Why "
            "jennings our whatever his learning gay perceive.";

        {
            char buf[8];
            mutable_buffer mb(buf, sizeof(buf));
            auto s1 = read(test_source(s0), mb);
            BOOST_TEST_EQ(s1, s0);
        }

        {
            constexpr int N = 20;
            char buf[N];
            mutable_buffer mb[N];
            for(int i = 0; i < N; ++i)
                mb[i] = { buf + i, 1 };
            auto s1 = read(test_source(s0),
                span<mutable_buffer const>(mb, N));
            BOOST_TEST_EQ(s1, s0);
        }
    }
};

TEST_SUITE(
    any_read_source_test,
    "boost.capy.buffers.any_read_source");

} // buffers
} // capy
} // boost
