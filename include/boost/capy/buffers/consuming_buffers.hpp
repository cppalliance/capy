//
// Copyright (c) 2026 Steve Gerbino
// Copyright (c) 2026 Michael Vandeberg
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_BUFFERS_CONSUMING_BUFFERS_HPP
#define BOOST_CAPY_BUFFERS_CONSUMING_BUFFERS_HPP

#include <boost/capy/detail/config.hpp>
#include <boost/capy/buffers.hpp>
#include <boost/capy/detail/slice_of.hpp>

#include <cstddef>
#include <utility>

namespace boost {
namespace capy {

/** A cursor that drives consumption of a buffer sequence.

    `consuming_buffers` is the dedicated driver for `read_some`/`write_some`
    loops. It presents the not-yet-consumed bytes of a buffer sequence via
    `data()`, and `consume(n)` advances past `n` transferred bytes **in
    place**.

    It is deliberately **not** itself a buffer sequence — it hands out the
    remaining bytes through `data()` (returning a `slice_of` view). It
    **borrows** the underlying sequence (iterators + a consumed-byte offset).
    The sequence must outlive the cursor. That is the natural case when the
    cursor is a local of a composed operation that took its buffers by value.

    @par Example
    @code
    consuming_buffers consuming(buffers);
    std::size_t total = 0, want = buffer_size(buffers);
    while (total < want)
    {
        auto [ec, n] = co_await stream.read_some(consuming.data());
        consuming.consume(n);
        total += n;
        if (ec && total < want) co_return {ec, total};
    }
    @endcode

    @see buffer_slice, slice_of
*/
template<class Seq>
    requires MutableBufferSequence<Seq> || ConstBufferSequence<Seq>
class consuming_buffers
{
public:
    /// Names the buffer type the underlying sequence `Seq` yields.
    using buffer_type = capy::buffer_type<Seq>;

private:
    using iterator_type =
        decltype(capy::begin(std::declval<Seq const&>()));

    iterator_type first_{};
    iterator_type last_{};
    std::size_t   front_skip_ = 0;  // bytes consumed from *first_

public:
    /** Construct a cursor over `s`.

        @param s The sequence to consume. Must outlive the cursor.
    */
    explicit consuming_buffers(Seq const& s) noexcept
        : first_(capy::begin(s))
        , last_(capy::end(s))
    {
    }

    /** Reject construction from a temporary (the view would dangle).

        @param s The sequence that would be consumed.
    */
    consuming_buffers(Seq const&& s) = delete;

    /** Return the remaining (unconsumed) bytes as a buffer sequence.

        @return The bytes not yet consumed, as a buffer sequence.
    */
    detail::slice_of<Seq>
    data() const noexcept
    {
        return detail::slice_of<Seq>(first_, last_, front_skip_, 0);
    }

    /** Discard `n` bytes from the front, in place.

        Advances past `min(n, remaining)` bytes.

        @param n The number of bytes consumed.
    */
    void
    consume(std::size_t n) noexcept
    {
        while (n > 0 && first_ != last_)
        {
            std::size_t const sz    = buffer_type(*first_).size();
            std::size_t const avail = sz - front_skip_;
            if (n < avail)
            {
                front_skip_ += n;
                return;
            }
            n -= avail;
            ++first_;
            front_skip_ = 0;
        }
    }
};

/** Deduce the sequence type from the constructor argument.

    @tparam Seq The buffer sequence type.
*/
template<class Seq>
consuming_buffers(Seq const&) -> consuming_buffers<Seq>;

} // namespace capy
} // namespace boost

#endif
