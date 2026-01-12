//
// Copyright (c) 2023 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#include <boost/capy/buffers/buffer_pair.hpp>
#include <boost/capy/buffers/slice.hpp>

namespace boost {
namespace capy {
namespace buffers {

void
tag_invoke(
    slice_tag const&,
    const_buffer_pair& bs,
    slice_how how,
    std::size_t n) noexcept
{
    switch(how)
    {
    case slice_how::remove_prefix:
    {
        auto p = &bs[0];
        if(n < p->size())
        {
            remove_prefix(*p, n);
            return;
        }
        n -= p->size();
        *p = bs[1];
        bs[1] = {};
        remove_prefix(*p, n);
        return;
    }

    case slice_how::keep_prefix:
    {
        auto p = &bs[0];
        if(n <= p->size())
        {
            keep_prefix(*p, n);
            bs[1] = {};
            return;
        }
        n -= p->size();
        ++p;
        keep_prefix(*p, n);
        return;
    }
    }
}

void
tag_invoke(
    slice_tag const&,
    mutable_buffer_pair& bs,
    slice_how how,
    std::size_t n) noexcept
{
    switch(how)
    {
    case slice_how::remove_prefix:
    {
        auto p = &bs[0];
        if(n < p->size())
        {
            remove_prefix(*p, n);
            return;
        }
        n -= p->size();
        *p = bs[1];
        bs[1] = {};
        remove_prefix(*p, n);
        return;
    }

    case slice_how::keep_prefix:
    {
        auto p = &bs[0];
        if(n <= p->size())
        {
            keep_prefix(*p, n);
            bs[1] = {};
            return;
        }
        n -= p->size();
        ++p;
        keep_prefix(*p, n);
        return;
    }
    }
}

} // buffers
} // capy
} // boost
