//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_TEST_READ_STREAM_HPP
#define BOOST_CAPY_TEST_READ_STREAM_HPP

#include <boost/capy/detail/config.hpp>
#include <boost/capy/buffers.hpp>
#include <boost/capy/buffers/buffer_copy.hpp>
#include <boost/capy/buffers/make_buffer.hpp>
#include <boost/capy/cond.hpp>
#include <boost/capy/coro.hpp>
#include <boost/capy/ex/executor_ref.hpp>
#include <boost/capy/io_result.hpp>
#include <boost/capy/test/fuse.hpp>

#include <stop_token>
#include <string>
#include <string_view>

namespace boost {
namespace capy {
namespace test {

/** A mock stream for testing read operations.

    Use this to verify code that performs reads without needing
    real I/O. Call @ref provide to supply data, then @ref read_some
    to consume it. The associated @ref fuse enables error injection
    at controlled points. An optional `max_read_size` constructor
    parameter limits bytes per read to simulate chunked delivery.

    This class satisfies the @ref ReadStream concept.

    @par Thread Safety
    Not thread-safe.

    @par Example
    @code
    fuse f;
    read_stream rs( f );
    rs.provide( "Hello, " );
    rs.provide( "World!" );

    auto r = f.armed( [&]( fuse& ) -> task<void> {
        char buf[32];
        auto [ec, n] = co_await rs.read_some(
            mutable_buffer( buf, sizeof( buf ) ) );
        if( ec )
            co_return;
        // buf contains "Hello, World!"
    } );
    @endcode

    @see fuse, ReadStream
*/
class read_stream
{
    fuse f_;
    std::string data_;
    std::size_t pos_ = 0;
    std::size_t max_read_size_;

public:
    /** Construct a read stream.

        @param f The fuse used to inject errors during reads.

        @param max_read_size Maximum bytes returned per read.
        Use to simulate chunked network delivery.
    */
    explicit read_stream(
        fuse f = {},
        std::size_t max_read_size = std::size_t(-1)) noexcept
        : f_(std::move(f))
        , max_read_size_(max_read_size)
    {
    }

    /** Append data to be returned by subsequent reads.

        Multiple calls accumulate data that @ref read_some returns.

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

    /// Return the number of bytes available for reading.
    std::size_t
    available() const noexcept
    {
        return data_.size() - pos_;
    }

    /** Asynchronously read data from the stream.

        Transfers up to `buffer_size( buffers )` bytes from the internal
        buffer to the provided mutable buffer sequence. If no data remains,
        returns `error::eof`. Before every read, the attached @ref fuse is
        consulted to possibly inject an error for testing fault scenarios.
        The returned `std::size_t` is the number of bytes transferred.

        @par Effects
        On success, advances the internal read position by the number of
        bytes copied. If an error is injected by the fuse, the read position
        remains unchanged.

        @par Exception Safety
        No-throw guarantee.

        @param buffers The mutable buffer sequence to receive data.

        @return An awaitable yielding `(error_code,std::size_t)`.

        @see fuse
    */
    template<MutableBufferSequence MB>
    auto
    read_some(MB buffers)
    {
        struct awaitable
        {
            read_stream* self_;
            MB buffers_;

            bool await_ready() const noexcept { return true; }

            // This method is required to satisfy Capy's IoAwaitable concept,
            // but is never called because await_ready() returns true.
            //
            // Capy uses a two-layer awaitable system: the promise's
            // await_transform wraps awaitables in a transform_awaiter whose
            // standard await_suspend(coroutine_handle) calls this custom
            // 3-argument overload, passing the executor and stop_token from
            // the coroutine's context. For synchronous test awaitables like
            // this one, the coroutine never suspends, so this is not invoked.
            // The signature exists to allow the same awaitable type to work
            // with both synchronous (test) and asynchronous (real I/O) code.
            void await_suspend(
                coro,
                executor_ref,
                std::stop_token) const noexcept
            {
            }

            io_result<std::size_t>
            await_resume()
            {
                // Empty buffer is a no-op regardless of
                // stream state or fuse.
                if(buffer_empty(buffers_))
                    return {{}, 0};

                auto ec = self_->f_.maybe_fail();
                if(ec)
                    return {ec, 0};

                if(self_->pos_ >= self_->data_.size())
                    return {error::eof, 0};

                std::size_t avail = self_->data_.size() - self_->pos_;
                if(avail > self_->max_read_size_)
                    avail = self_->max_read_size_;
                auto src = make_buffer(self_->data_.data() + self_->pos_, avail);
                std::size_t const n = buffer_copy(buffers_, src);
                self_->pos_ += n;
                return {{}, n};
            }
        };
        return awaitable{this, buffers};
    }
};

} // test
} // capy
} // boost

#endif
