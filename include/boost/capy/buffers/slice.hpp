//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_BUFFERS_SLICE_HPP
#define BOOST_CAPY_BUFFERS_SLICE_HPP

#include <boost/capy/detail/config.hpp>
#include <boost/capy/buffers.hpp>
#include <array>
#include <iterator>
#include <type_traits>

namespace boost {
namespace capy {

template<class T> class slice_of;

namespace detail {

template<class T, class = void>
struct has_tag_invoke : std::false_type {};

template<class T>
struct has_tag_invoke<T, decltype(tag_invoke(
    std::declval<slice_tag const&>(),
    std::declval<T&>(),
    std::declval<slice_how>(),
    std::declval<std::size_t>()))>
    : std::true_type {};

} // detail

/** Alias for the type representing a slice of T
*/
template<class T>
using slice_type = std::conditional_t<
    detail::has_tag_invoke<T>::value,
    T, slice_of<T>>;

//------------------------------------------------

/** A wrapper enabling a buffer sequence to be consumed
*/
template<ConstBufferSequence BufferSequence>
class slice_of<BufferSequence>
{
    static_assert(!std::is_const_v<BufferSequence>,
        "BufferSequence can't be const");

    static_assert(!std::is_reference_v<BufferSequence>,
        "BufferSequence can't be a reference");

    using iter_type = decltype(
        std::declval<BufferSequence const&>().begin());

    using difference_type =
        typename std::iterator_traits<iter_type>::difference_type;

    BufferSequence bs_;
    difference_type begin_ = 0; // index of first buffer in sequence
    difference_type end_ = 0;   // 1 + index of last buffer in sequence
    std::size_t len_ = 0;       // length of bs_
    std::size_t size_ = 0;      // total bytes
    std::size_t prefix_ = 0;    // used prefix bytes
    std::size_t suffix_ = 0;    // used suffix bytes

public:
    /** The type of values returned by iterators
    */
    using value_type = std::conditional_t<
        MutableBufferSequence<BufferSequence>,
        mutable_buffer, const_buffer>;

    /** The type of returned iterators
    */
    class const_iterator
    {
        iter_type it_;
        // VFALCO we could just point back to
        // the original sequence to save size
        std::size_t prefix_ = 0;
        std::size_t suffix_ = 0;
        std::size_t i_ = 0;
        std::size_t n_ = 0;

        friend class slice_of<BufferSequence>;

        const_iterator(
            iter_type it,
            std::size_t prefix__,
            std::size_t suffix__,
            std::size_t i,
            std::size_t n) noexcept
            : it_(it)
            , prefix_(prefix__)
            , suffix_(suffix__)
            , i_(i)
            , n_(n)
        {
            // n_ is the index of the end iterator
        }

    public:
        using value_type = typename slice_of::value_type;
        using reference = value_type;
        using pointer = void;
        using difference_type = std::ptrdiff_t;
        using iterator_category =
            std::bidirectional_iterator_tag;
        using iterator_concept = std::bidirectional_iterator_tag;

        const_iterator() = default;

        bool
        operator==(
            const_iterator const& other) const noexcept
        {
            return
                it_     == other.it_ &&
                prefix_ == other.prefix_ &&
                suffix_ == other.suffix_ &&
                i_      == other.i_ &&
                n_      == other.n_;
        }

        bool
        operator!=(
            const_iterator const& other) const noexcept
        {
            return !(*this == other);
        }

        reference
        operator*() const noexcept
        {
            value_type v = *it_;
            using P = std::conditional_t<
                MutableBufferSequence<BufferSequence>,
                char*, char const*>;
            auto p = reinterpret_cast<P>(v.data());
            auto n = v.size();
            if(i_ == 0)
            {
                p += prefix_;
                n -= prefix_;
            }
            if(i_ == n_ - 1)
                n -= suffix_;
            return value_type(p, n);
        }

        const_iterator&
        operator++() noexcept
        {
            BOOST_CAPY_ASSERT(i_ < n_);
            ++it_;
            ++i_;
            return *this;
        }

        const_iterator
        operator++(int) noexcept
        {
            auto temp = *this;
            ++(*this);
            return temp;
        }

        const_iterator&
        operator--() noexcept
        {
            BOOST_CAPY_ASSERT(i_ > 0);
            --it_;
            --i_;
            return *this;
        }

        const_iterator
        operator--(int) noexcept
        {
            auto temp = *this;
            --(*this);
            return temp;
        }
    };

    /** Constructor
    */
    slice_of() = default;

    /** Constructor
    */
    slice_of(
        BufferSequence const& bs)
        : bs_(bs)
    {
        iter_type it = capy::begin(bs_);
        iter_type eit = capy::end(bs_);
        begin_ = 0;
        end_ = std::distance(it, eit);
        while(it != eit)
        {
            value_type b(*it);
            size_ += b.size();
            ++len_;
            ++it;
        }
    }

    /** Return an iterator to the beginning of the sequence
    */
    const_iterator
    begin() const noexcept
    {
        return const_iterator(
            begin_iter_impl(), prefix_, suffix_, 0, len_);
    }

    /** Return an iterator to the end of the sequence
    */
    const_iterator
    end() const noexcept
    {
        return const_iterator(
            end_iter_impl(), prefix_, suffix_, len_, len_);
    }

    friend
    void
    tag_invoke(
        slice_tag const&,
        slice_of<BufferSequence>& bs,
        slice_how how,
        std::size_t n)
    {
        bs.slice_impl(how, n);
    }

private:
    iter_type
    begin_iter_impl() const noexcept
    {
        iter_type it = capy::begin(bs_);
        std::advance(it, begin_);
        return it;
    }

    iter_type
    end_iter_impl() const noexcept
    {
        iter_type it = capy::begin(bs_);
        std::advance(it, end_);
        return it;
    }

    void
    remove_prefix_impl(
        std::size_t n)
    {
        if(n > size_)
            n = size_;

        // nice hack to simplify the loop (M. Nejati)
        n += prefix_;
        size_ += prefix_;
        prefix_ = 0;

        iter_type it = begin_iter_impl();

        while(n > 0 && begin_ != end_)
        {
            value_type b = *it;
            if(n < b.size())
            {
                prefix_ = n;
                size_ -= n;
                break;
            }
            n -= b.size();
            size_ -= b.size();
            ++begin_;
            ++it;
            --len_;
        }
    }

    void
    remove_suffix_impl(
        std::size_t n)
    {
        if(size_ == 0)
        {
            BOOST_CAPY_ASSERT(begin_ == end_);
            return;
        }
        BOOST_CAPY_ASSERT(begin_ != end_);

        if(n > size_)
            n = size_;

        n += suffix_;
        size_ += suffix_;
        suffix_ = 0;

        iter_type bit = begin_iter_impl();
        iter_type it = end_iter_impl();
        it--;

        while(it != bit)
        {
            value_type b = *it;
            if(n < b.size())
            {
                suffix_ = n;
                size_ -= n;
                return;
            }
            n -= b.size();
            size_ -= b.size();
            --it;
            --end_;
            --len_;
        }
        value_type b = *it;
        auto m = b.size() - prefix_;
        if(n < m)
        {
            suffix_ = n;
            size_ -= n;
            return;
        }
        end_ = begin_;
        len_ = 0;
        size_ = 0;
    }

    void
    keep_prefix_impl(
        std::size_t n)
    {
        if(n >= size_)
            return;
        if(n == 0)
        {
            end_ = begin_;
            len_ = 0;
            size_ = 0;
            return;
        }
        remove_suffix_impl(size_ - n);
    }

    void
    keep_suffix_impl(
        std::size_t n)
    {
        if(n >= size_)
            return;
        if(n == 0)
        {
            begin_ = end_;
            len_ = 0;
            size_ = 0;
            return;
        }
        remove_prefix_impl(size_ - n);
    }

    void
    slice_impl(
        slice_how how,
        std::size_t n)
    {
        switch(how)
        {
        case slice_how::remove_prefix:
        {
            remove_prefix_impl(n);
            break;
        }
        case slice_how::keep_prefix:
        {
            keep_prefix_impl(n);
            break;
        }
        }
    }
};

//------------------------------------------------

// in-place modify  return value
// -----------------------------
// keep_prefix*     prefix
// keep_suffix      suffix
// remove_prefix*   sans_prefix
// remove_suffix    sans_suffix

/** Remove all but the first `n` bytes from a buffer sequence
*/
constexpr struct keep_prefix_mrdocs_workaround_t
{
    template<ConstBufferSequence BufferSequence>
        requires detail::has_tag_invoke<BufferSequence>::value
    void operator()(
        BufferSequence& bs,
        std::size_t n) const
    {
        tag_invoke(slice_tag{}, bs, slice_how::keep_prefix, n);
    }
} const keep_prefix{};

/** Remove all but the last `n` bytes from a buffer sequence
*/
constexpr struct keep_suffix_mrdocs_workaround_t
{
    template<ConstBufferSequence BufferSequence>
        requires detail::has_tag_invoke<BufferSequence>::value
    void operator()(
        BufferSequence& bs,
        std::size_t n) const
    {
        auto n0 = buffer_size(bs);
        if(n < n0)
            tag_invoke(slice_tag{}, bs, slice_how::remove_prefix, n0 - n);
    }
} const keep_suffix{};

/** Remove `n` bytes from the beginning of a buffer sequence
*/
constexpr struct remove_prefix_mrdocs_workaround_t
{
    template<ConstBufferSequence BufferSequence>
        requires detail::has_tag_invoke<BufferSequence>::value
    void operator()(
        BufferSequence& bs,
        std::size_t n) const
    {
        tag_invoke(slice_tag{}, bs, slice_how::remove_prefix, n);
    }
} const remove_prefix{};

/** Remove `n` bytes from the end of a buffer sequence
*/
constexpr struct remove_suffix_mrdocs_workaround_t
{
    template<ConstBufferSequence BufferSequence>
        requires detail::has_tag_invoke<BufferSequence>::value
    void operator()(
        BufferSequence& bs,
        std::size_t n) const
    {
        auto n0 = buffer_size(bs);
        if(n > 0)
        {
            if( n > n0)
                n = n0;
            tag_invoke(slice_tag{}, bs, slice_how::keep_prefix, n0 - n);
        }
    }
} const remove_suffix{};

//------------------------------------------------

/** Return a sequence representing the first `n` bytes of a buffer sequence
*/
constexpr struct prefix_mrdocs_workaround_t
{
    template<ConstBufferSequence BufferSequence>
    slice_type<BufferSequence> operator()(
        BufferSequence const& bs,
        std::size_t n) const noexcept
    {
        slice_type<BufferSequence> result(bs);
        keep_prefix(result, n);
        return result;
    }
} prefix{};

/** Return a sequence representing the last `n` bytes of a buffer sequence
*/
constexpr struct suffix_mrdocs_workaround_t
{
    template<ConstBufferSequence BufferSequence>
    slice_type<BufferSequence> operator()(
        BufferSequence const& bs,
        std::size_t n) const noexcept
    {
        slice_type<BufferSequence> result(bs);
        keep_suffix(result, n);
        return result;
    }
} suffix{};

/** Return a sequence representing all but the first `n` bytes of a buffer sequence
*/
constexpr struct sans_prefix_mrdocs_workaround_t
{
    template<ConstBufferSequence BufferSequence>
    slice_type<BufferSequence> operator()(
        BufferSequence const& bs,
        std::size_t n) const noexcept
    {
        slice_type<BufferSequence> result(bs);
        remove_prefix(result, n);
        return result;
    }
} sans_prefix{};

/** Return a sequence representing all but the last `n` bytes of a buffer sequence
*/
constexpr struct sans_suffix_mrdocs_workaround_t
{
    template<ConstBufferSequence BufferSequence>
    slice_type<BufferSequence> operator()(
        BufferSequence const& bs,
        std::size_t n) const noexcept
    {
        slice_type<BufferSequence> result(bs);
        remove_suffix(result, n);
        return result;
    }
} sans_suffix{};

} // capy
} // boost

#endif
