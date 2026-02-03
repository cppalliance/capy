//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
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
#include <boost/capy/buffers/slice.hpp>
#include <boost/capy/coro.hpp>
#include <boost/capy/ex/executor_ref.hpp>

#include <algorithm>
#include <cstddef>
#include <stop_token>
#include <utility>

namespace boost {
namespace capy {
namespace test {

/** A test utility for iterating buffer sequence split points.

    This class iterates through all possible ways to split a buffer
    sequence into two parts (b1, b2) where concatenating them yields
    the original sequence. It uses an async-generator-like pattern
    that allows `co_await` between iterations.

    The split type automatically preserves mutability: passing a
    `MutableBufferSequence` yields mutable slices, while passing a
    `ConstBufferSequence` yields const slices. This is handled
    automatically through `slice_type<BS>`.

    @par Thread Safety
    Not thread-safe.

    @par Example
    @code
    // Test all split points of a buffer
    std::string data = "hello world";
    auto cb = make_buffer( data );

    fuse f;
    auto r = f.inert( [&]( fuse& ) -> task<> {
        bufgrind bg( cb );
        while( bg ) {
            auto [b1, b2] = co_await bg.next();
            // b1 contains first N bytes
            // b2 contains remaining bytes
            // concatenating b1 + b2 equals original
            co_await some_async_operation( b1, b2 );
        }
    } );
    @endcode

    @par Mutable Buffer Example
    @code
    // Mutable buffers preserve mutability
    char data[100];
    mutable_buffer mb( data, sizeof( data ) );

    bufgrind bg( mb );
    while( bg ) {
        auto [b1, b2] = co_await bg.next();
        // b1, b2 yield mutable_buffer when iterated
    }
    @endcode

    @par Step Size Example
    @code
    // Skip by 10 bytes for faster iteration
    bufgrind bg( cb, 10 );
    while( bg ) {
        auto [b1, b2] = co_await bg.next();
        // Visits positions 0, 10, 20, ..., and always size
    }
    @endcode

    @see prefix, sans_prefix, slice_type
*/
template<ConstBufferSequence BS>
class bufgrind
{
    BS const& bs_;
    std::size_t size_;
    std::size_t step_;
    std::size_t pos_ = 0;

public:
    /// The type returned by @ref next.
    using split_type = std::pair<slice_type<BS>, slice_type<BS>>;

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

    /** Awaitable returned by @ref next.
    */
    struct next_awaitable
    {
        bufgrind* self_;

        bool await_ready() const noexcept { return true; }
        coro await_suspend(coro h, executor_ref, std::stop_token) const noexcept { return h; }

        split_type
        await_resume()
        {
            auto b1 = prefix(self_->bs_, self_->pos_);
            auto b2 = sans_prefix(self_->bs_, self_->pos_);
            if(self_->pos_ < self_->size_)
                self_->pos_ = (std::min)(self_->pos_ + self_->step_, self_->size_);
            else
                ++self_->pos_;
            return {std::move(b1), std::move(b2)};
        }
    };

    /** Return the next split point.

        Returns an awaitable that yields the current (b1, b2) pair
        and advances to the next split point.

        @par Preconditions
        `static_cast<bool>( *this )` is `true`.

        @return An awaitable yielding `split_type`.
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
