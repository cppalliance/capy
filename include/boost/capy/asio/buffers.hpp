//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_ASIO_BUFFERS_HPP
#define BOOST_CAPY_ASIO_BUFFERS_HPP

#include <boost/capy/buffers.hpp>
#include <concepts>
#include <utility>

namespace asio 
{

class mutable_buffer; 
class const_buffer; 

}

namespace boost::asio 
{

class mutable_buffer; 
class const_buffer; 

}

namespace boost::capy
{

/** A const buffer type compatible with both capy and Asio.

    Extends capy's `const_buffer` with implicit conversion operators
    to both standalone Asio (`::asio::const_buffer`) and Boost.Asio
    (`boost::asio::const_buffer`) buffer types.

    This allows seamless use of capy buffers with Asio I/O operations
    without explicit conversion.

    @par Example
    @code
    capy::asio_const_buffer buf(data, size);

    // Works directly with Asio operations
    co_await socket.async_write_some(buf, capy::as_io_awaitable);
    @endcode

    @see asio_mutable_buffer, as_asio_buffer_sequence
*/
struct asio_const_buffer : const_buffer
{
    using const_buffer::const_buffer;
    using const_buffer::operator=;

    /// Construct from a capy const_buffer.
    asio_const_buffer(const_buffer cb) : const_buffer(cb) {}

    /// Convert to standalone Asio const_buffer.
    inline
    operator ::asio::const_buffer() const;

    /// Convert to Boost.Asio const_buffer.
    inline
    operator boost::asio::const_buffer() const;
};

/** A mutable buffer type compatible with both capy and Asio.

    Extends capy's `mutable_buffer` with implicit conversion operators
    to both standalone Asio and Boost.Asio buffer types. Supports
    conversion to both mutable and const buffer types.

    This allows seamless use of capy buffers with Asio I/O operations
    without explicit conversion.

    @par Example
    @code
    char data[1024];
    capy::asio_mutable_buffer buf(data, sizeof(data));

    // Works directly with Asio read operations
    auto [ec, n] = co_await socket.async_read_some(buf, capy::as_io_awaitable);
    @endcode

    @see asio_const_buffer, as_asio_buffer_sequence
*/
struct asio_mutable_buffer : mutable_buffer
{
    using mutable_buffer::mutable_buffer;
    using mutable_buffer::operator=;

    /// Construct from a capy mutable_buffer.
    asio_mutable_buffer(mutable_buffer mb) : mutable_buffer(mb) {}

    /// Convert to standalone Asio mutable_buffer.
    inline
    operator ::asio::mutable_buffer() const;

    /// Convert to Boost.Asio mutable_buffer.
    inline
    operator boost::asio::mutable_buffer() const;

    /// Convert to standalone Asio const_buffer.
    inline
    operator ::asio::const_buffer() const;

    /// Convert to Boost.Asio const_buffer.
    inline
    operator boost::asio::const_buffer() const;
};


namespace detail
{

struct asio_buffer_transformer_t
{
  inline
  asio_mutable_buffer operator()(const boost::asio::mutable_buffer &mb) const noexcept;

  inline
  asio_const_buffer operator()(const boost::asio::const_buffer &cb) const noexcept;

  inline
  asio_mutable_buffer operator()(const ::asio::mutable_buffer &mb) const noexcept;

  inline
  asio_const_buffer operator()(const ::asio::const_buffer &cb) const noexcept;


  asio_mutable_buffer operator()(const mutable_buffer &mb) const noexcept
  {
    return mb;
  }

  asio_const_buffer operator()(const const_buffer &cb) const noexcept
  {
    return cb;
  }

  asio_buffer_transformer_t() noexcept = default;
  asio_buffer_transformer_t(const asio_buffer_transformer_t &) noexcept = default;
};

constexpr static asio_buffer_transformer_t asio_buffer_transformer;

/** A bidirectional iterator that transforms buffer types using asio_buffer_transformer.
 *
 *  Wraps an underlying bidirectional iterator and applies asio_buffer_transformer
 *  to each dereferenced value, converting between Asio buffer types and capy
 *  asio_const_buffer/asio_mutable_buffer types.
 *
 *  @tparam Iterator The underlying bidirectional iterator type
 */
template<typename Iterator>
class asio_buffer_iterator
{
public:
    using iterator_type     = Iterator;
    using iterator_category = std::bidirectional_iterator_tag;
    using difference_type   = typename std::iterator_traits<Iterator>::difference_type;
    using underlying_value  = typename std::iterator_traits<Iterator>::value_type;
    using value_type        = decltype(asio_buffer_transformer(std::declval<underlying_value>()));
    using reference         = value_type;
    using pointer           = void;

    asio_buffer_iterator() = default;

    explicit asio_buffer_iterator(Iterator it)
        noexcept(std::is_nothrow_move_constructible_v<Iterator>)
        : it_(std::move(it))
    {
    }

    asio_buffer_iterator(const asio_buffer_iterator&) = default;
    asio_buffer_iterator(asio_buffer_iterator&&) = default;
    asio_buffer_iterator& operator=(const asio_buffer_iterator&) = default;
    asio_buffer_iterator& operator=(asio_buffer_iterator&&) = default;

    reference operator*() const
        noexcept(noexcept(asio_buffer_transformer(*std::declval<Iterator>())))
    {
        return asio_buffer_transformer(*it_);
    }

    asio_buffer_iterator& operator++()
        noexcept(noexcept(++std::declval<Iterator&>()))
    {
        ++it_;
        return *this;
    }

    asio_buffer_iterator operator++(int)
        noexcept(noexcept(std::declval<Iterator&>()++))
    {
        asio_buffer_iterator tmp(*this);
        ++it_;
        return tmp;
    }

    asio_buffer_iterator& operator--()
        noexcept(noexcept(--std::declval<Iterator&>()))
    {
        --it_;
        return *this;
    }

    asio_buffer_iterator operator--(int)
        noexcept(noexcept(std::declval<Iterator&>()--))
    {
        asio_buffer_iterator tmp(*this);
        --it_;
        return tmp;
    }

    bool operator==(const asio_buffer_iterator& other) const
        noexcept(noexcept(std::declval<Iterator>() == std::declval<Iterator>()))
    {
        return it_ == other.it_;
    }

    bool operator!=(const asio_buffer_iterator& other) const
        noexcept(noexcept(std::declval<Iterator>() != std::declval<Iterator>()))
    {
        return it_ != other.it_;
    }

    /// Returns the underlying iterator.
    Iterator base() const noexcept(std::is_nothrow_copy_constructible_v<Iterator>)
    {
        return it_;
    }

private:
    Iterator it_{};
};

// Deduction guide
template<typename Iterator>
asio_buffer_iterator(Iterator) -> asio_buffer_iterator<Iterator>;


/** A bidirectional range that transforms buffer sequences using asio_buffer_transformer.
 *
 *  Wraps a buffer sequence and provides begin/end iterators that transform
 *  buffer elements via asio_buffer_transformer. Satisfies the requirements
 *  of std::ranges::bidirectional_range.
 *
 *  @tparam Sequence The underlying buffer sequence type
 */
template<typename Sequence>
class asio_buffer_range
{
public:
    using sequence_type = Sequence;
    using iterator      = asio_buffer_iterator<
                            decltype(std::ranges::begin(std::declval<const Sequence&>()))>;
    using const_iterator = iterator;

    asio_buffer_range() = default;

    explicit asio_buffer_range(Sequence seq)
        noexcept(std::is_nothrow_move_constructible_v<Sequence>)
        : seq_(std::move(seq))
    {
    }

    asio_buffer_range(const asio_buffer_range&) = default;
    asio_buffer_range(asio_buffer_range&&) = default;
    asio_buffer_range& operator=(const asio_buffer_range&) = default;
    asio_buffer_range& operator=(asio_buffer_range&&) = default;

    iterator begin() const
        noexcept(noexcept(std::ranges::begin(std::declval<const Sequence&>())))
    {
        return iterator(std::ranges::begin(seq_));
    }

    iterator end() const
        noexcept(noexcept(std::ranges::end(std::declval<const Sequence&>())))
    {
        return iterator(std::ranges::end(seq_));
    }

    /// Returns true if the range is empty.
    bool empty() const
        noexcept(noexcept(std::ranges::empty(std::declval<const Sequence&>())))
    {
        return std::ranges::empty(seq_);
    }

    /// Returns the underlying sequence.
    const Sequence& base() const noexcept { return seq_; }

private:
    Sequence seq_{};
};

// Deduction guide
template<typename Sequence>
asio_buffer_range(Sequence) -> asio_buffer_range<Sequence>;

}


/** @defgroup as_asio_buffer_sequence as_asio_buffer_sequence

    Bidirectional conversion between Asio and capy buffer sequences.

    The `as_asio_buffer_sequence` function provides seamless conversion
    in both directions:

    - **Capy to Asio**: Convert capy buffer sequences for use with Asio
      I/O operations. The returned range satisfies Asio's
      `MutableBufferSequence` or `ConstBufferSequence` requirements.

    - **Asio to Capy**: Convert Asio buffer sequences for use with capy's
      buffer concepts. The returned range satisfies capy's
      `MutableBufferSequence` or `ConstBufferSequence` concepts.

    @par Example: Capy to Asio
    @code
    capy::mutable_buffer_pair buffers = ...;
    auto seq = capy::as_asio_buffer_sequence(buffers);
    co_await socket.async_read_some(seq, capy::as_io_awaitable);
    @endcode

    @par Example: Asio to Capy
    @code
    std::vector<boost::asio::mutable_buffer> asio_bufs = ...;
    auto seq = capy::as_asio_buffer_sequence(asio_bufs);
    std::size_t total = capy::buffer_size(seq);  // Use capy algorithms
    @endcode

    @{
*/

/** Pass through a range already containing asio_const_buffer elements.

    @param rng A bidirectional range of asio_const_buffer
    @return The range forwarded unchanged
*/
template<std::ranges::bidirectional_range<> Range>
    requires std::same_as<std::ranges::range_value_t<Range>, asio_const_buffer>
auto as_asio_buffer_sequence(Range && rng)
{
    return std::forward<Range>(rng);
}

/** Pass through a range already containing asio_mutable_buffer elements.

    @param rng A bidirectional range of asio_mutable_buffer
    @return The range forwarded unchanged
*/
template<std::ranges::bidirectional_range<> Range>
    requires std::same_as<std::ranges::range_value_t<Range>, asio_mutable_buffer>
auto as_asio_buffer_sequence(Range && rng)
{
    return std::forward<Range>(rng);
}

/** Convert a single asio_mutable_buffer to a buffer sequence.

    @param mb The mutable buffer
    @return A single-element array containing the buffer
*/
inline
auto as_asio_buffer_sequence(asio_mutable_buffer mb)
{
    return std::array<asio_mutable_buffer, 1u>{mb};
}

/** Convert a single asio_const_buffer to a buffer sequence.

    @param cb The const buffer
    @return A single-element array containing the buffer
*/
inline
auto as_asio_buffer_sequence(asio_const_buffer cb)
{
    return std::array<asio_const_buffer, 1u>{cb};
}

/** Convert a single capy mutable_buffer to a buffer sequence.

    @param mb The mutable buffer
    @return A single-element array containing an asio_mutable_buffer
*/
inline
auto as_asio_buffer_sequence(mutable_buffer mb)
{
    return std::array<asio_mutable_buffer, 1u>{mb};
}

/** Convert a buffer type that might support both mutable & const buffer
    to an asio buffer sequence.

    @param buf The const buffer
    @return A single-element array containing an asio_mutable_buffer

*/
template<std::convertible_to<mutable_buffer> Buffer>
inline
auto as_asio_buffer_sequence(const Buffer & buf)
{
    return as_asio_buffer_sequence(static_cast<mutable_buffer>(buf));
}

/** Convert a single capy const_buffer to a buffer sequence.
    @param cb The const buffer
    @return A single-element array containing an asio_const_buffer

*/
inline
auto as_asio_buffer_sequence(const_buffer cb)
{
    return std::array<asio_const_buffer, 1u>{cb};
}

/** Convert any buffer sequence for bidirectional Asio/capy compatibility.

    Wraps the buffer sequence in a transforming range that converts
    each buffer element to asio_const_buffer or asio_mutable_buffer.
    The returned range satisfies both Asio's buffer sequence requirements
    and capy's buffer sequence concepts.

    This overload handles:
    - Capy buffer sequences (for use with Asio operations)
    - Asio buffer sequences (for use with capy concepts/algorithms)

    @param seq The buffer sequence to convert
    @return A transforming range over the buffer sequence
*/
template<ConstBufferSequence Seq>
  requires (!std::convertible_to<Seq, const_buffer> && !std::convertible_to<Seq, mutable_buffer>)
inline
auto as_asio_buffer_sequence(const Seq & seq)
{
    return detail::asio_buffer_range(seq);
}

/** @} */

}


#endif // BOOST_CAPY_ASIO_BUFFERS_HPP

