//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#include <boost/capy/buffers/any_buffers.hpp>
#include <boost/capy/buffers/detail/except.hpp>

namespace boost {
namespace capy {
namespace buffers {

template<>
any_buffers<true>::
any_buffers() noexcept
{
    struct impl : any_impl
    {
        bool is_small_buffers() const noexcept override
        {
            return true;
        }

        bool is_small_iter() const noexcept override
        {
            return true;
        }

        void copy(
            any_buffers& dest,
            std::shared_ptr<any_impl const> const& sp) const override
        {
            dest.sp_ = sp;
        }

        void it_copy(void*, void const*) const override
        {
        }

        void it_destroy(void*) const override
        {
        }

        void inc(void*) const override
        {
        }

        void dec(void*) const override
        {
        }

        value_type deref(void const*) const override
        {
            return {};
        }

        bool equal(void const*, void const*) const override
        {
            return true;
        }

        void begin(void*) const override
        {
        }

        void end(void*) const override
        {
        }
    };

    static impl const instance;
    sp_ = std::shared_ptr<any_impl const>(
        &instance, null_deleter{} );
}

template<>
any_buffers<false>::
any_buffers() noexcept
{
    struct impl : any_impl
    {
        bool is_small_buffers() const noexcept override
        {
            return true;
        }

        bool is_small_iter() const noexcept override
        {
            return true;
        }

        void copy(
            any_buffers& dest,
            std::shared_ptr<any_impl const> const& sp) const override
        {
            dest.sp_ = sp;
        }

        void it_copy(void*, void const*) const override
        {
        }

        void it_destroy(void*) const override
        {
        }

        void inc(void*) const override
        {
        }

        void dec(void*) const override
        {
        }

        value_type deref(void const*) const override
        {
            return {};
        }

        bool equal(void const*, void const*) const override
        {
            return true;
        }

        void begin(void*) const override
        {
        }

        void end(void*) const override
        {
        }
    };

    static impl const instance;
    sp_ = std::shared_ptr<any_impl const>(
        &instance, null_deleter{} );
}

template<>
any_buffers<true>::
any_buffers::
const_iterator::
const_iterator() noexcept
    : sp_(any_buffers<true>().begin().sp_)
{
}

template<>
any_buffers<false>::
any_buffers::
const_iterator::
const_iterator() noexcept
    : sp_(any_buffers<false>().begin().sp_)
{
}

} // buffers
} // capy
} // boost
