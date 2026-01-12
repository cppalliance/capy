//
// Copyright (c) 2023 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_BUFFERS_RANGE_HPP
#define BOOST_CAPY_BUFFERS_RANGE_HPP

#include <boost/capy/detail/config.hpp>
#include <boost/capy/buffers/buffer.hpp>
#include <type_traits>

namespace boost {
namespace capy {
namespace buffers {

//------------------------------------------------

namespace detail {

template<class T>
class iter_range
{
    using begin_type = decltype(
        buffers::begin(std::declval<T&>()));
    using end_type = decltype(
        buffers::end(std::declval<T&>()));

    begin_type begin_;
    end_type end_;

public:
    iter_range(T& t) noexcept
        : begin_(buffers::begin(t))
        , end_(buffers::end(t))
    {
    }

    begin_type
    begin() const noexcept
    {
        return begin_;
    }

    end_type
    end() const noexcept
    {
        return end_;
    }
};

struct range_impl
{
    template<class BufferSequence>
    auto
    operator()(
        BufferSequence&& bs) const noexcept ->
            iter_range<typename
                std::remove_reference<
                    BufferSequence>::type>
    {
        return { bs };
    }
};

} // detail

/** Return a range representing the buffer sequence.
*/
constexpr detail::range_impl range{};

} // buffers
} // capy
} // boost

#endif
