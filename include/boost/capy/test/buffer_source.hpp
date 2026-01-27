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
#include <boost/capy/coro.hpp>
#include <boost/capy/ex/executor_ref.hpp>
#include <boost/capy/io_result.hpp>
#include <boost/capy/test/fuse.hpp>

#include <algorithm>
#include <stop_token>
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
        auto [ec, count] = co_await bs.pull( arr, 16 );
        if( ec.failed() )
            co_return;
        // arr[0..count) contains buffer descriptors
        std::size_t n = buffer_size( std::span( arr, count ) );
        bs.consume( n );
    } );
    @endcode

    @see fuse, BufferSource
*/
class buffer_source
{
    fuse* f_;
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
        fuse& f,
        std::size_t max_pull_size = std::size_t(-1)) noexcept
        : f_(&f)
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

        Fills the provided array with buffer descriptors pointing to
        internal data starting from the current unconsumed position.
        Returns the number of buffers filled. When no data remains,
        returns count=0 to signal completion.

        Calling pull multiple times without intervening @ref consume
        returns the same data. Use consume to advance past processed
        bytes.

        @param arr Pointer to array of const_buffer to fill.
        @param max_count Maximum number of buffers to fill.

        @return An awaitable yielding `(error_code,std::size_t)`.

        @see consume, fuse
    */
    auto
    pull(const_buffer* arr, std::size_t max_count)
    {
        struct awaitable
        {
            buffer_source* self_;
            const_buffer* arr_;
            std::size_t max_count_;

            bool await_ready() const noexcept { return true; }

            void await_suspend(
                coro,
                executor_ref,
                std::stop_token) const noexcept
            {
            }

            io_result<std::size_t>
            await_resume()
            {
                auto ec = self_->f_->maybe_fail();
                if(ec.failed())
                    return {ec, 0};

                if(self_->pos_ >= self_->data_.size())
                    return {{}, 0}; // Source exhausted

                std::size_t avail = self_->data_.size() - self_->pos_;
                std::size_t to_return = (std::min)(avail, self_->max_pull_size_);

                if(max_count_ == 0)
                    return {{}, 0};

                // Fill a single buffer descriptor
                arr_[0] = make_buffer(
                    self_->data_.data() + self_->pos_,
                    to_return);

                return {{}, 1};
            }
        };
        return awaitable{this, arr, max_count};
    }
};

} // test
} // capy
} // boost

#endif
