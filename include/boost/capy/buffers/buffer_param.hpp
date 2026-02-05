//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

/*
    COROUTINE BUFFER SEQUENCE LIFETIME REQUIREMENT
    ===============================================
    Buffer sequence parameters in coroutine APIs MUST be passed BY VALUE,
    never by reference. When a coroutine suspends, reference parameters may
    dangle if the caller's object goes out of scope before resumption.

    CORRECT:   task<> read_some(MutableBufferSequence auto buffers)
    WRONG:     task<> read_some(MutableBufferSequence auto& buffers)
    WRONG:     task<> read_some(MutableBufferSequence auto const& buffers)

    The buffer_param class works with this model: it takes a const& in its
    constructor (for the non-coroutine scope) but the caller's template
    function accepts the buffer sequence by value, ensuring the sequence
    lives in the coroutine frame.
*/

#ifndef BOOST_CAPY_BUFFERS_BUFFER_PARAM_HPP
#define BOOST_CAPY_BUFFERS_BUFFER_PARAM_HPP

#include <boost/capy/detail/config.hpp>
#include <boost/capy/buffers.hpp>

#include <span>
#include <type_traits>

namespace boost {
namespace capy {

/** A buffer sequence wrapper providing windowed access.

    This template class wraps any buffer sequence and provides
    incremental access through a sliding window of buffer
    descriptors. It handles both const and mutable buffer
    sequences automatically.

    @par Coroutine Lifetime Requirement

    When used in coroutine APIs, the outer template function
    MUST accept the buffer sequence parameter BY VALUE:

    @code
    task<> write(ConstBufferSequence auto buffers);   // CORRECT
    task<> write(ConstBufferSequence auto& buffers);  // WRONG - dangling reference
    @endcode

    Pass-by-value ensures the buffer sequence is copied into
    the coroutine frame and remains valid across suspension
    points. References would dangle when the caller's scope
    exits before the coroutine resumes.

    @par Purpose

    When iterating through large buffer sequences, it is often
    more efficient to process buffers in batches rather than
    one at a time. This class maintains a window of up to
    @ref max_size buffer descriptors, automatically refilling
    from the underlying sequence as buffers are consumed.

    @par Usage

    Create a `buffer_param` from any buffer sequence and use
    `data()` to get the current window of buffers. After
    processing some bytes, call `consume()` to advance through
    the sequence.

    @code
    task<> send(ConstBufferSequence auto buffers)
    {
        buffer_param bp(buffers);
        while(true)
        {
            auto bufs = bp.data();
            if(bufs.empty())
                break;
            auto n = co_await do_something(bufs);
            bp.consume(n);
        }
    }
    @endcode

    @par Virtual Interface Pattern

    This class enables passing arbitrary buffer sequences through
    a virtual function boundary. The template function captures
    the buffer sequence by value and drives the iteration, while
    the virtual function receives a simple span:

    @code
    class base
    {
    public:
        task<> write(ConstBufferSequence auto buffers)
        {
            buffer_param bp(buffers);
            while(true)
            {
                auto bufs = bp.data();
                if(bufs.empty())
                    break;
                std::size_t n = 0;
                co_await write_impl(bufs, n);
                bp.consume(n);
            }
        }

    protected:
        virtual task<> write_impl(
            std::span<const_buffer> buffers,
            std::size_t& bytes_written) = 0;
    };
    @endcode

    @tparam BS The buffer sequence type. Must satisfy either
        ConstBufferSequence or MutableBufferSequence.

    @see ConstBufferSequence, MutableBufferSequence
*/
template<class BS, bool MakeConst = false>
    requires ConstBufferSequence<BS> || MutableBufferSequence<BS>
class buffer_param
{
public:
    /// The buffer type (const_buffer or mutable_buffer)
    using buffer_type = std::conditional_t<
        MakeConst,
        const_buffer,
        capy::buffer_type<BS>>;

private:
    decltype(begin(std::declval<BS const&>())) it_;
    decltype(end(std::declval<BS const&>())) end_;
    buffer_type arr_[detail::max_iovec_];
    std::size_t size_ = 0;
    std::size_t pos_ = 0;

    void
    refill()
    {
        pos_ = 0;
        size_ = 0;
        for(; it_ != end_ && size_ < detail::max_iovec_; ++it_)
        {
            buffer_type buf(*it_);
            if(buf.size() > 0)
                arr_[size_++] = buf;
        }
    }

public:
    /** Construct from a buffer sequence.

        @param bs The buffer sequence to wrap. The caller must
            ensure the buffer sequence remains valid for the
            lifetime of this object.
    */
    explicit
    buffer_param(BS const& bs)
        : it_(begin(bs))
        , end_(end(bs))
    {
        refill();
    }

    /** Return the current window of buffer descriptors.

        Returns a span of buffer descriptors representing the
        currently available portion of the buffer sequence.
        The span contains at most @ref max_size buffers.

        When the current window is exhausted, this function
        automatically refills from the underlying sequence.

        @return A span of buffer descriptors. Empty span
            indicates no more data is available.
    */
    std::span<buffer_type>
    data()
    {
        if(pos_ >= size_)
            refill();
        if(size_ == 0)
            return {};
        return {arr_ + pos_, size_ - pos_};
    }

    /** Consume bytes from the buffer sequence.

        Advances the current position by `n` bytes, consuming
        data from the front of the sequence. Partially consumed
        buffers are adjusted in place.

        @param n Number of bytes to consume.
    */
    void
    consume(std::size_t n)
    {
        while(n > 0 && pos_ < size_)
        {
            auto avail = arr_[pos_].size();
            if(n < avail)
            {
                arr_[pos_] += n;
                n = 0;
            }
            else
            {
                n -= avail;
                ++pos_;
            }
        }
    }
};

// CTAD deduction guide
template<class BS>
buffer_param(BS const&) -> buffer_param<BS>;

/// Alias for buffer_param that always uses const_buffer storage.
template<class BS>
using const_buffer_param = buffer_param<BS, true>;

} // namespace capy
} // namespace boost

#endif
