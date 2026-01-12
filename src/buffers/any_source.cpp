//
// Copyright (c) 2025 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#include <boost/capy/buffers/any_source.hpp>

namespace boost {
namespace capy {
namespace buffers {

any_source::
any_impl::
~any_impl() = default;

bool
any_source::
any_impl::
has_size() const noexcept
{
    return false;
}

bool
any_source::
any_impl::
has_buffers() const noexcept
{
    return false;
}

std::size_t
any_source::
any_impl::
size() const
{
    detail::throw_invalid_argument();
}

any_const_buffers
any_source::
any_impl::
data() const    
{
    detail::throw_invalid_argument();
}

//-----------------------------------------------

any_source::
any_source() noexcept
{
    struct model : any_impl
    {
        bool has_size() const noexcept override
        {
            return true;
        }

        bool has_buffers() const noexcept override
        {
            return true;
        }

        std::size_t size() const override
        {
            return 0;
        }

        any_const_buffers data() const override
        {
            return {};
        }

        void rewind() override
        {
        }

        std::size_t read(
            span<mutable_buffer const>,
            system::error_code& ec) override
        {
            ec = error::eof;
            return 0;
        }
    };

    static model instance;
    sp_ = { &instance, [](any_impl*) {} };
}

any_source::
any_source(
    any_source&& other) noexcept
    : sp_(std::move(other.sp_))
{
    other.sp_ = any_source().sp_;
}

any_source&
any_source::
operator=(
    any_source&& other) noexcept
{
    sp_ = std::move(other.sp_);
    other.sp_ = any_source().sp_;
    return *this;
}

} // buffers
} // capy
} // boost
