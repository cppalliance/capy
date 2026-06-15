//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_TEST_BUFFER_SOURCE_HPP
#define BOOST_CAPY_TEST_BUFFER_SOURCE_HPP

#include <boost/capy/detail/config.hpp>
#include <boost/capy/buffers.hpp>
#include <boost/capy/buffers/make_buffer.hpp>
#include <coroutine>
#include <boost/capy/error.hpp>
#include <boost/capy/ex/io_env.hpp>
#include <boost/capy/io_result.hpp>
#include <boost/capy/test/fuse.hpp>

#include <algorithm>
#include <span>
#include <string>
#include <string_view>

namespace boost {
namespace capy {
namespace test {

/** A mock buffer source for testing push operations.

    Use this to verify code that transfers data from a buffer source to
    a sink without needing real I/O. Call @ref provide to supply data,
    then @ref pull to retrieve buffer descriptors. The associated
    @ref fuse enables error injection at controlled points.

    This class satisfies the @ref BufferSource concept by providing
    a pull interface that fills an array of buffer descriptors and
    a consume interface to indicate bytes used.

    @par Thread Safety
    Not thread-safe.

    @par Example
    @code
    fuse f;
    buffer_source bs( f );
    bs.provide( "Hello, " );
    bs.provide( "World!" );

    auto r = f.armed( [&]( fuse& ) -> task<void> {
        const_buffer arr[16];
        auto [ec, bufs] = co_await bs.pull( arr );
        if( ec )
            co_return;
        // bufs contains buffer descriptors
        std::size_t n = buffer_size( bufs );
        bs.consume( n );
    } );
    @endcode

    @see fuse, BufferSource
*/
class buffer_source
{
    fuse f_;
    std::string data_;
    std::size_t pos_ = 0;
    std::size_t max_pull_size_;

public:
    /** Construct a buffer source.

        @param f The fuse used to inject errors during pulls.

        @param max_pull_size Maximum bytes returned per pull.
        Use to simulate chunked delivery.
    */
    explicit buffer_source(
        fuse f = {},
        std::size_t max_pull_size = std::size_t(-1)) noexcept
        : f_(std::move(f))
        , max_pull_size_(max_pull_size)
    {
    }

    /** Append data to be returned by subsequent pulls.

        Multiple calls accumulate data that @ref pull returns.

        @param sv The data to append.
    */
    void
    provide(std::string_view sv)
    {
        data_.append(sv);
    }

    /// Clear all data and reset the read position.
    void
    clear() noexcept
    {
        data_.clear();
        pos_ = 0;
    }

    /// Return the number of bytes available for pulling.
    std::size_t
    available() const noexcept
    {
        return data_.size() - pos_;
    }

    /** Consume bytes from the source.

        Advances the internal read position by the specified number
        of bytes. The next call to @ref pull returns data starting
        after the consumed bytes.

        @param n The number of bytes to consume. Must not exceed the
        total size of buffers returned by the previous @ref pull.
    */
    void
    consume(std::size_t n) noexcept
    {
        pos_ += n;
    }

    /** Pull buffer data from the source.

        Fills the provided span with buffer descriptors pointing to
        internal data starting from the current unconsumed position.
        Returns a span of filled buffers. When no data remains,
        returns an empty span to signal completion.

        Calling pull multiple times without intervening @ref consume
        returns the same data. Use consume to advance past processed
        bytes.

        @param dest Span of const_buffer to fill.

        @return An awaitable that await-returns `(error_code,std::span<const_buffer>)`.

        @see consume, fuse
    */
    auto
    pull(std::span<const_buffer> dest)
    {
        struct awaitable
        {
            buffer_source* self_;
            std::span<const_buffer> dest_;

            bool await_ready() const noexcept { return true; }

            // This method is required to satisfy Capy's IoAwaitable concept,
            // but is never called because await_ready() returns true.
            //
            // Capy uses a two-layer awaitable system: the promise's
            // await_transform wraps awaitables in a transform_awaiter whose
            // standard await_suspend(coroutine_handle) calls this custom
            // 2-argument overload, passing the io_env from the coroutine's
            // context. For synchronous test awaitables like this one, the
            // coroutine never suspends, so this is not invoked. The signature
            // exists to allow the same awaitable type to work with both
            // synchronous (test) and asynchronous (real I/O) code.
            void await_suspend(
                std::coroutine_handle<>,
                io_env const*) const noexcept
            {
            }

            io_result<std::span<const_buffer>>
            await_resume()
            {
                auto ec = self_->f_.maybe_fail();
                if(ec)
                    return {ec, {}};

                if(self_->pos_ >= self_->data_.size())
                    return {error::eof, {}};

                std::size_t avail = self_->data_.size() - self_->pos_;
                std::size_t to_return = (std::min)(avail, self_->max_pull_size_);

                if(dest_.empty())
                    return {{}, {}};

                // Fill a single buffer descriptor
                dest_[0] = make_buffer(
                    self_->data_.data() + self_->pos_,
                    to_return);

                return {{}, dest_.first(1)};
            }
        };
        return awaitable{this, dest};
    }
};

} // test
} // capy
} // boost

#endif
