//
// Copyright (c) 2023 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_BUFFERS_DYNAMIC_BUFFER_HPP
#define BOOST_CAPY_BUFFERS_DYNAMIC_BUFFER_HPP

#include <boost/capy/detail/config.hpp>
#include <boost/capy/buffers.hpp>
#include <boost/capy/concept/dynamic_buffer.hpp>
#include <boost/core/span.hpp>
#include <cstdlib>

namespace boost {
namespace capy {

/** Metafunction to detect if a type is a dynamic buffer.
*/
template<class T>
struct is_dynamic_buffer
    : std::bool_constant<dynamic_buffer<T>>
{
};

/** An abstract, type-erased dynamic buffer.
*/
struct BOOST_SYMBOL_VISIBLE
    any_dynamic_buffer
{
    using const_buffers_type = span<const_buffer const>;
    using mutable_buffers_type = span<mutable_buffer const>;

    virtual ~any_dynamic_buffer() = default;
    virtual std::size_t size() const = 0;
    virtual std::size_t max_size() const = 0;
    virtual std::size_t capacity() const = 0;
    virtual const_buffers_type data() const = 0;
    virtual mutable_buffers_type prepare(std::size_t) = 0;
    virtual void commit(std::size_t) = 0;
    virtual void consume(std::size_t) = 0;
};

//-----------------------------------------------

/** A type-erased dynamic buffer.
*/
template<
    dynamic_buffer DynamicBuffer,
    std::size_t N = 8>
class any_dynamic_buffer_impl
    : public any_dynamic_buffer
{
    DynamicBuffer b_;
    const_buffer data_[N];
    mutable_buffer out_[N];
    std::size_t data_len_ = 0;
    std::size_t out_len_ = 0;

    template<const_buffer_sequence ConstBufferSequence, class BufferType>
    static
    std::size_t
    unroll(
        ConstBufferSequence const& bs,
        BufferType* dest,
        std::size_t len)
    {
        std::size_t i = 0;
        auto const end_ = end(bs);
        for(auto it = begin(bs); it != end_; ++it)
        {
            dest[i++] = *it;
            if(i == len)
                break;
        }
        return i;
    }

public:
    template<class DynamicBuffer_>
    explicit
    any_dynamic_buffer_impl(
        DynamicBuffer_&& b)
        : b_(std::forward<DynamicBuffer_>(b))
    {
    }

    DynamicBuffer&
    buffer() noexcept
    {
        return b_;
    }

    DynamicBuffer const&
    buffer() const noexcept
    {
        return b_;
    }

    std::size_t
    size() const override
    {
        return b_.size();
    }

    std::size_t
    max_size() const override
    {
        return b_.max_size();
    }

    std::size_t
    capacity() const override
    {
        return b_.capacity();
    }

    const_buffers_type
    data() const override
    {
        return const_buffers_type(
            data_, data_len_);
    }

    auto
    prepare(
        std::size_t n) ->
            mutable_buffers_type override
    {
        out_len_ = unroll(
            b_.prepare(n), out_, N);
        return mutable_buffers_type(
            out_, out_len_);
    }

    void
    commit(
        std::size_t n) override
    {
        b_.commit(n);
        data_len_ = unroll(
            b_.data(), data_, N);
    }

    void
    consume(
        std::size_t n) override
    {
        b_.consume(n);
        data_len_ = unroll(
            b_.data(), data_, N);
    }
};

template<dynamic_buffer DynamicBuffer>
auto
make_any(DynamicBuffer&& b) ->
    any_dynamic_buffer_impl<std::decay_t<DynamicBuffer>>
{
    return any_dynamic_buffer_impl<std::decay_t<DynamicBuffer>>(
            std::forward<DynamicBuffer>(b));
}

} // capy
} // boost

#endif
