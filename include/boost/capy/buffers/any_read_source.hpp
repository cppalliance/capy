//
// Copyright (c) 2025 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_BUFFERS_ANY_READ_SOURCE_HPP
#define BOOST_CAPY_BUFFERS_ANY_READ_SOURCE_HPP

#include <boost/capy/buffers/detail/config.hpp>
#include <boost/capy/error.hpp>
#include <boost/capy/buffers/read_source.hpp>
#include <boost/core/span.hpp>
#include <boost/assert.hpp>

namespace boost {
namespace capy {
namespace buffers {

/** Type-erased interface to a read source
*/
class BOOST_SYMBOL_VISIBLE
    any_read_source
{
public:
    virtual ~any_read_source() = default;

    virtual bool has_size() const noexcept = 0;

    virtual bool has_rewind() const noexcept = 0;

    virtual std::uint64_t size() const = 0;

    virtual void rewind() = 0;

    template<class MutableBufferSequence>
    std::size_t read(
        MutableBufferSequence const& dest,
        system::error_code& ec);

private:
    virtual std::size_t do_read(
        mutable_buffer const* p,
        std::size_t n,
        system::error_code& ec) = 0;
};

//-----------------------------------------------

template<class MutableBufferSequence>
std::size_t
any_read_source::
read(
    MutableBufferSequence const& dest,
    system::error_code& ec)
{
    std::size_t result = 0;
    constexpr std::size_t N = 16;
    std::size_t n = 0;
    mutable_buffer mb[N];
    auto it = buffers::begin(dest);
    auto const end_ = buffers::end(dest);
    while(it != end_)
    {
        mb[n++] = *it++;
        if(n < N)
            continue;
        auto const nread = do_read(mb, n, ec);
        BOOST_ASSERT(
            ec.failed() ||
            nread == buffers::size(
                span<mutable_buffer const>(mb, n)));
        result += nread;
        if(ec.failed())
            return result;
        n = 0;
    }
    if(n > 0)
    {
        auto const nread = do_read(mb, n, ec);
        BOOST_ASSERT(
            ec.failed() ||
            nread == buffers::size(
                span<mutable_buffer const>(mb, n)));
        result += nread;
        if(ec.failed())
            return result;
    }
    return result;
}

//-----------------------------------------------

namespace detail {

template<class Source>
class read_source_impl : public any_read_source
{
public:
    template<class Source_>
    explicit read_source_impl(
        Source_&& source) noexcept
        : source_(std::forward<Source_>(source))
    {
    }

private:
    bool has_size() const noexcept override
    {
        return buffers::has_size<Source>::value;
    }

    bool has_rewind() const noexcept override
    {
        return buffers::has_rewind<Source>::value;
    }

    std::uint64_t size() const override
    {
        return size(buffers::has_size<Source>{});
    }

    std::uint64_t size(std::true_type) const
    {
        return source_.size();
    }

    std::uint64_t size(std::false_type) const
    {
        detail::throw_invalid_argument();
    }

    void rewind() override
    {
        rewind(buffers::has_rewind<Source>{});
    }

    void rewind(std::true_type)
    {
        ec_ = {};
        source_.rewind();
    }

    void rewind(std::false_type) const
    {
        detail::throw_invalid_argument();
    }

    std::size_t do_read(
        mutable_buffer const* p,
        std::size_t n,
        system::error_code& ec) override
    {
        if(ec_.failed())
        {
            ec = ec_;
            return 0;
        }
        auto const nread = source_.read(
            span<mutable_buffer const>(p, n), ec);
        ec_ = ec;
        return nread;
    }

    Source source_;
    system::error_code ec_;
};

} // detail

//-----------------------------------------------

template<class ReadSource>
auto
make_any_read_source(
    ReadSource&& source) ->
        detail::read_source_impl<typename
            std::decay<ReadSource>::type>
{
    return detail::read_source_impl<typename
        std::decay<ReadSource>::type>(
            std::forward<ReadSource>(source));
}

} // buffers
} // capy
} // boost

#endif
