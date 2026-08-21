//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
// Copyright (c) 2026 Michael Vandeberg
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_TEST_BUFGRIND_HPP
#define BOOST_CAPY_TEST_BUFGRIND_HPP

#include <boost/capy/detail/config.hpp>
#include <boost/capy/buffers.hpp>
#include <boost/capy/buffers/buffer_slice.hpp>
#include <coroutine>
#include <boost/capy/ex/io_env.hpp>

#include <algorithm>
#include <cstddef>
#include <type_traits>
#include <utility>

namespace boost {
namespace capy {
namespace test {

/** Iterates split points of a buffer sequence into two adjacent halves.

    This class iterates through all possible ways to split a buffer
    sequence into two parts (b1, b2) where concatenating them yields
    the original sequence. It uses an async-generator-like pattern
    that allows `co_await` between iterations.

    The split type automatically preserves mutability: passing a
    `MutableBufferSequence` yields halves that model
    @ref MutableBufferSequence, while passing a `ConstBufferSequence`
    yields halves that model @ref ConstBufferSequence. Each half is
    the buffer-sequence view exposed by a @ref buffer_slice over the
    corresponding byte range, and can be passed directly to
    `read_some`, `write_some`, `buffer_size`, etc.

    @par Thread Safety
    Not thread-safe.

    @par Example
    @par !example example_1


    @par Mutable Buffer Example
    @par !example example_2


    @par Step Size Example
    @par !example example_3


    @see buffer_slice
*/
template<ConstBufferSequence BS>
class bufgrind
{
    BS const& bs_;
    std::size_t size_;
    std::size_t step_;
    std::size_t pos_ = 0;

public:
    /// Names the buffer-sequence type `buffer_slice` yields for each half.
    using slice_type = std::decay_t<
        decltype(buffer_slice(std::declval<BS const&>()))>;

    /// Pairs the two `slice_type` halves that @ref next yields together.
    using split_type = std::pair<slice_type, slice_type>;

    /** Construct a buffer grinder.

        @param bs The buffer sequence to iterate over.

        @param step The number of bytes to advance on each call to
        @ref next. A value of 0 is treated as 1. The final split
        at `buffer_size( bs )` is always included regardless of
        step alignment.
    */
    explicit
    bufgrind(
        BS const& bs,
        std::size_t step = 1) noexcept
        : bs_(bs)
        , size_(buffer_size(bs))
        , step_(step > 0 ? step : 1)
    {
    }

    /** Check if more split points remain.

        @return `true` if @ref next can be called, `false` otherwise.
    */
    explicit operator bool() const noexcept
    {
        return pos_ <= size_;
    }

    /** Computes the current split synchronously, so awaiting it never suspends the caller.
    */
    struct next_awaitable
    {
        /// The grinder that produced this awaitable.
        bufgrind* self_;

        /** Report whether the awaitable is ready.

            @return `true` always; the split is available without suspending.
        */
        bool await_ready() const noexcept { return true; }

        /** Resume the caller inline without suspending.

            @param h The awaiting coroutine handle.

            @return @p h, so the caller resumes immediately.
        */
        std::coroutine_handle<> await_suspend(std::coroutine_handle<> h, io_env const*) const noexcept { return h; }

        /** Return the current split and advance to the next.

            @return The `(b1, b2)` split at the current position.
        */
        split_type
        await_resume()
        {
            split_type result{
                buffer_slice(self_->bs_, 0, self_->pos_),
                buffer_slice(self_->bs_, self_->pos_)
            };
            if(self_->pos_ < self_->size_)
                self_->pos_ = (std::min)(self_->pos_ + self_->step_, self_->size_);
            else
                ++self_->pos_;
            return result;
        }
    };

    /** Return the next split point.

        Returns an awaitable that yields the current (b1, b2) pair
        and advances to the next split point.

        @par Preconditions
        `static_cast<bool>( *this )` is `true`.

        @return An awaitable that await-returns `split_type`.
    */
    next_awaitable
    next() noexcept
    {
        return {this};
    }
};

} // test
} // capy
} // boost

#endif
