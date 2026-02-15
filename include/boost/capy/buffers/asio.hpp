//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_BUFFERS_ASIO_HPP
#define BOOST_CAPY_BUFFERS_ASIO_HPP

#include <boost/capy/detail/config.hpp>
#include <boost/capy/buffers.hpp>
#include <boost/asio/buffer.hpp>

#include <compare>
#include <cstddef>
#include <iterator>
#include <type_traits>
#include <utility>

namespace boost {
namespace capy {

namespace detail {

// true when BS itself or its range element is convertible
// to asio::const_buffer (i.e. it is an asio-native sequence)
template<class BS, class = void>
constexpr bool is_native_asio_v = false;

template<class BS>
constexpr bool is_native_asio_v<BS,
    std::enable_if_t<
        std::is_convertible_v<BS, asio::const_buffer>>> = true;

template<class BS>
constexpr bool is_native_asio_v<BS,
    std::enable_if_t<
        !std::is_convertible_v<BS, asio::const_buffer> &&
        std::ranges::bidirectional_range<BS> &&
        std::is_convertible_v<
            std::ranges::range_value_t<BS>,
            asio::const_buffer>>> = true;

/** Adapts a buffer sequence so its iterators yield the other library's buffer type.

    When the source is an asio-native sequence the adaptor's
    iterators produce `capy::mutable_buffer` or `capy::const_buffer`.
    Otherwise the source is a capy sequence and the iterators
    produce `asio::mutable_buffer` or `asio::const_buffer`.

    @tparam BufferSequence The source buffer sequence type.
    @tparam IsMutable Whether the target buffer type is mutable.
*/
template<
    class BufferSequence,
    bool IsMutable>
class buffer_sequence_adaptor
{
    static constexpr bool native_asio =
        is_native_asio_v<BufferSequence>;

    static auto get_begin(BufferSequence const& bs) noexcept
    {
        if constexpr (native_asio)
            return asio::buffer_sequence_begin(bs);
        else
            return capy::begin(bs);
    }

    using source_iterator = decltype(
        get_begin(std::declval<BufferSequence const&>()));

    using source_value_type = std::conditional_t<
        native_asio,
        std::conditional_t<IsMutable, asio::mutable_buffer, asio::const_buffer>,
        std::conditional_t<IsMutable, capy::mutable_buffer, capy::const_buffer>>;

public:
    using value_type = std::conditional_t<
        native_asio,
        std::conditional_t<IsMutable, capy::mutable_buffer, capy::const_buffer>,
        std::conditional_t<IsMutable, asio::mutable_buffer, asio::const_buffer>>;

    class const_iterator
    {
        source_iterator it_{};

    public:
        using value_type = buffer_sequence_adaptor::value_type;
        using reference = value_type;
        using pointer = void;
        using difference_type = std::ptrdiff_t;

        using iterator_category = std::conditional_t<
            std::random_access_iterator<source_iterator>,
            std::random_access_iterator_tag,
            std::bidirectional_iterator_tag>;

        const_iterator() = default;

        explicit const_iterator(
            source_iterator it) noexcept
            : it_(it)
        {
        }

        reference operator*() const noexcept
        {
            source_value_type b(*it_);
            return value_type(b.data(), b.size());
        }

        const_iterator&
        operator++() noexcept
        {
            ++it_;
            return *this;
        }

        const_iterator
        operator++(int) noexcept
        {
            auto tmp = *this;
            ++*this;
            return tmp;
        }

        const_iterator&
        operator--() noexcept
        {
            --it_;
            return *this;
        }

        const_iterator
        operator--(int) noexcept
        {
            auto tmp = *this;
            --*this;
            return tmp;
        }

        bool operator==(
            const_iterator const& other) const noexcept
        {
            return it_ == other.it_;
        }

        bool operator!=(
            const_iterator const& other) const noexcept
        {
            return it_ != other.it_;
        }

        //--- random access (conditional) ---

        const_iterator&
        operator+=(difference_type n) noexcept
            requires std::random_access_iterator<source_iterator>
        {
            it_ += n;
            return *this;
        }

        const_iterator&
        operator-=(difference_type n) noexcept
            requires std::random_access_iterator<source_iterator>
        {
            it_ -= n;
            return *this;
        }

        reference
        operator[](difference_type n) const noexcept
            requires std::random_access_iterator<source_iterator>
        {
            return *(*this + n);
        }

        friend const_iterator
        operator+(
            const_iterator it,
            difference_type n) noexcept
            requires std::random_access_iterator<source_iterator>
        {
            it += n;
            return it;
        }

        friend const_iterator
        operator+(
            difference_type n,
            const_iterator it) noexcept
            requires std::random_access_iterator<source_iterator>
        {
            it += n;
            return it;
        }

        friend const_iterator
        operator-(
            const_iterator it,
            difference_type n) noexcept
            requires std::random_access_iterator<source_iterator>
        {
            it -= n;
            return it;
        }

        friend difference_type
        operator-(
            const_iterator const& a,
            const_iterator const& b) noexcept
            requires std::random_access_iterator<source_iterator>
        {
            return a.it_ - b.it_;
        }

        auto operator<=>(
            const_iterator const& other) const noexcept
            requires std::random_access_iterator<source_iterator>
        {
            return it_ <=> other.it_;
        }
    };

    template<class BS>
    explicit buffer_sequence_adaptor(BS&& bs)
        noexcept(std::is_nothrow_constructible_v<
            BufferSequence, BS&&>)
        : bs_(std::forward<BS>(bs))
    {
    }

    const_iterator
    begin() const noexcept
    {
        if constexpr (native_asio)
            return const_iterator(
                asio::buffer_sequence_begin(bs_));
        else
            return const_iterator(
                capy::begin(bs_));
    }

    const_iterator
    end() const noexcept
    {
        if constexpr (native_asio)
            return const_iterator(
                asio::buffer_sequence_end(bs_));
        else
            return const_iterator(
                capy::end(bs_));
    }

private:
    BufferSequence bs_;
};

} // detail

//------------------------------------------------

/** Adapt a capy buffer sequence for use with Asio.

    Returns a wrapper whose iterators dereference to
    `asio::mutable_buffer` or `asio::const_buffer`,
    depending on whether the source is a
    `MutableBufferSequence`.

    @param bs The capy buffer sequence to adapt.
    @return An adapted buffer sequence usable with Asio.
*/
template<class BS>
    requires ConstBufferSequence<std::remove_cvref_t<BS>>
auto
to_asio(BS&& bs)
{
    using Seq = std::remove_cvref_t<BS>;
    constexpr bool is_mutable =
        MutableBufferSequence<Seq>;

    return detail::buffer_sequence_adaptor<
        Seq, is_mutable>(
            std::forward<BS>(bs));
}

/** Adapt an Asio buffer sequence for use with capy.

    Returns a wrapper whose iterators dereference to
    `capy::mutable_buffer` or `capy::const_buffer`,
    depending on whether the source is convertible to
    `asio::mutable_buffer`. Accepts single asio buffers
    and ranges of asio buffers.

    @param bs The Asio buffer sequence to adapt.
    @return An adapted buffer sequence usable with capy.
*/
template<class BS>
    requires std::is_convertible_v<
        std::remove_cvref_t<BS>, asio::const_buffer> ||
        std::ranges::bidirectional_range<std::remove_cvref_t<BS>>
auto
from_asio(BS&& bs)
{
    using Seq = std::remove_cvref_t<BS>;

    constexpr bool is_mutable = [] {
        if constexpr (std::is_convertible_v<Seq, asio::mutable_buffer>)
            return true;
        else if constexpr (std::is_convertible_v<Seq, asio::const_buffer>)
            return false;
        else
            return std::is_convertible_v<
                std::ranges::range_value_t<Seq>,
                asio::mutable_buffer>;
    }();

    return detail::buffer_sequence_adaptor<
        Seq, is_mutable>(
            std::forward<BS>(bs));
}

} // capy
} // boost

#endif
