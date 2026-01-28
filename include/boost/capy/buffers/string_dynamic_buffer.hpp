//
// Copyright (c) 2023 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_BUFFERS_STRING_DYNAMIC_BUFFER_HPP
#define BOOST_CAPY_BUFFERS_STRING_DYNAMIC_BUFFER_HPP

#include <boost/capy/detail/config.hpp>
#include <boost/capy/buffers.hpp>
#include <boost/capy/detail/except.hpp>
#include <string>

namespace boost {
namespace capy {

/** A dynamic buffer using an underlying string
*/
template<
    class CharT,
    class Traits = std::char_traits<CharT>,
    class Allocator = std::allocator<CharT>>
class basic_string_dynamic_buffer
{
    std::basic_string<
        CharT, Traits, Allocator>* s_;
    std::size_t max_size_;

    std::size_t in_size_ = 0;
    std::size_t out_size_ = 0;

public:
    using is_dynamic_buffer_adapter = void;
    using string_type = std::basic_string<
        CharT, Traits, Allocator>;
    using const_buffers_type = const_buffer;
    using mutable_buffers_type = mutable_buffer;

    ~basic_string_dynamic_buffer() = default;

    /** Constructor.
    */
    basic_string_dynamic_buffer(
        basic_string_dynamic_buffer&& other) noexcept
        : s_(other.s_)
        , max_size_(other.max_size_)
        , in_size_(other.in_size_)
        , out_size_(other.out_size_)
    {
        other.s_ = nullptr;
    }

    /** Constructor.
    */
    explicit
    basic_string_dynamic_buffer(
        string_type* s,
        std::size_t max_size =
            std::size_t(-1)) noexcept
        : s_(s)
        , max_size_(
            max_size > s_->max_size()
                ? s_->max_size()
                : max_size)
    {
        if(s_->size() > max_size_)
            s_->resize(max_size_);
        in_size_ = s_->size();
    }

    /** Assignment.
    */
    basic_string_dynamic_buffer& operator=(
        basic_string_dynamic_buffer const&) = delete;

    std::size_t
    size() const noexcept
    {
        return in_size_;
    }

    std::size_t
    max_size() const noexcept
    {
        return max_size_;
    }

    std::size_t
    capacity() const noexcept
    {
        if(s_->capacity() <= max_size_)
            return s_->capacity() - in_size_;
        return max_size_ - in_size_;
    }

    const_buffers_type
    data() const noexcept
    {
        return const_buffers_type(
            s_->data(), in_size_);
    }

    mutable_buffers_type
    prepare(std::size_t n)
    {
        // n exceeds available space
        if(n > max_size_ - in_size_)
            detail::throw_invalid_argument();

        if( s_->size() < in_size_ + n)
            s_->resize(in_size_ + n);
        out_size_ = n;
        return mutable_buffers_type(
            &(*s_)[in_size_], out_size_);
    }

    void commit(std::size_t n) noexcept
    {
        if(n < out_size_)
            in_size_ += n;
        else
            in_size_ += out_size_;
        out_size_ = 0;
        s_->resize(in_size_);
    }

    void consume(std::size_t n) noexcept
    {
        if(n < in_size_)
        {
            s_->erase(0, n);
            in_size_ -= n;
        }
        else
        {
            s_->clear();
            in_size_ = 0;
        }
        out_size_ = 0;
    }
};

using string_dynamic_buffer = basic_string_dynamic_buffer<char>;

} // capy
} // boost

#endif
