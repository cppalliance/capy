//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_BUFFERS_SPAN_HPP
#define BOOST_CAPY_BUFFERS_SPAN_HPP

#include <boost/capy/detail/config.hpp>
#include <boost/capy/buffers.hpp>
#include <span>

namespace boost {
namespace capy {

/** Remove bytes from the beginning of a span of const buffers.

    Modifies the span and its buffer contents in-place to
    remove the first `n` bytes.

    @param bs The span to modify.
    @param n The number of bytes to remove.
*/
inline
void
remove_span_prefix(
    std::span<const_buffer>& bs,
    std::size_t n) noexcept
{
    while(bs.size() > 0)
    {
        if(n < bs.front().size())
        {
            bs.front() += n;
            return;
        }
        n -= bs.front().size();
        bs = bs.subspan(1);
    }
}

/** Remove bytes from the beginning of a span of mutable buffers.

    Modifies the span and its buffer contents in-place to
    remove the first `n` bytes.

    @param bs The span to modify.
    @param n The number of bytes to remove.
*/
inline
void
remove_span_prefix(
    std::span<mutable_buffer>& bs,
    std::size_t n) noexcept
{
    while(bs.size() > 0)
    {
        if(n < bs.front().size())
        {
            bs.front() += n;
            return;
        }
        n -= bs.front().size();
        bs = bs.subspan(1);
    }
}

/** Remove bytes from the end of a span of const buffers.

    Modifies the span and its buffer contents in-place to
    remove the last `n` bytes.

    @param bs The span to modify.
    @param n The number of bytes to remove.
*/
inline
void
remove_span_suffix(
    std::span<const_buffer>& bs,
    std::size_t n) noexcept
{
    while(bs.size() > 0)
    {
        if(n < bs.back().size())
        {
            auto& b = bs.back();
            b = const_buffer(b.data(), b.size() - n);
            return;
        }
        n -= bs.back().size();
        bs = bs.subspan(0, bs.size() - 1);
    }
}

/** Remove bytes from the end of a span of mutable buffers.

    Modifies the span and its buffer contents in-place to
    remove the last `n` bytes.

    @param bs The span to modify.
    @param n The number of bytes to remove.
*/
inline
void
remove_span_suffix(
    std::span<mutable_buffer>& bs,
    std::size_t n) noexcept
{
    while(bs.size() > 0)
    {
        if(n < bs.back().size())
        {
            auto& b = bs.back();
            b = mutable_buffer(b.data(), b.size() - n);
            return;
        }
        n -= bs.back().size();
        bs = bs.subspan(0, bs.size() - 1);
    }
}

/** Keep only the first bytes of a span of const buffers.

    Modifies the span and its buffer contents in-place to
    keep only the first `n` bytes.

    @param bs The span to modify.
    @param n The number of bytes to keep.
*/
inline
void
keep_span_prefix(
    std::span<const_buffer>& bs,
    std::size_t n) noexcept
{
    auto total = buffer_size(bs);
    if(n < total)
        remove_span_suffix(bs, total - n);
}

/** Keep only the first bytes of a span of mutable buffers.

    Modifies the span and its buffer contents in-place to
    keep only the first `n` bytes.

    @param bs The span to modify.
    @param n The number of bytes to keep.
*/
inline
void
keep_span_prefix(
    std::span<mutable_buffer>& bs,
    std::size_t n) noexcept
{
    auto total = buffer_size(bs);
    if(n < total)
        remove_span_suffix(bs, total - n);
}

/** Keep only the last bytes of a span of const buffers.

    Modifies the span and its buffer contents in-place to
    keep only the last `n` bytes.

    @param bs The span to modify.
    @param n The number of bytes to keep.
*/
inline
void
keep_span_suffix(
    std::span<const_buffer>& bs,
    std::size_t n) noexcept
{
    auto total = buffer_size(bs);
    if(n < total)
        remove_span_prefix(bs, total - n);
}

/** Keep only the last bytes of a span of mutable buffers.

    Modifies the span and its buffer contents in-place to
    keep only the last `n` bytes.

    @param bs The span to modify.
    @param n The number of bytes to keep.
*/
inline
void
keep_span_suffix(
    std::span<mutable_buffer>& bs,
    std::size_t n) noexcept
{
    auto total = buffer_size(bs);
    if(n < total)
        remove_span_prefix(bs, total - n);
}

} // capy
} // boost

#endif
