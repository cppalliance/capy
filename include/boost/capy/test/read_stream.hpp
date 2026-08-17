//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
// Copyright (c) 2026 Michael Vandeberg
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
#include <coroutine>
#include <boost/capy/ex/io_env.hpp>
#include <boost/capy/io_result.hpp>
#include <boost/capy/test/fuse.hpp>

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
        Injected I/O conditions are reported via the `error_code`
        component of the result. Throws `std::system_error` only when
        the attached @ref fuse is in exception mode and reaches its
        failure point; no-throw otherwise.

        @par Cancellation
        If the environment's stop token has been requested, the read
        completes immediately with `error::canceled` and transfers no
        data. This lets code under test exercise its cancellation paths.
        An empty buffer sequence is a no-op that completes successfully
        regardless of the stop token.

        @param buffers The mutable buffer sequence to receive data.

        @return An awaitable that await-returns `(error_code,std::size_t)`.

        @throws std::system_error When the attached @ref fuse is in
        exception mode and reaches its failure point.

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
            bool canceled_ = false;

            bool await_ready() const noexcept { return false; }

            // The operation completes synchronously, but await_suspend
            // is the only place io_env is delivered (the promise's
            // transform_awaiter forwards it here). Returning false means
            // the coroutine does not actually suspend — it resumes
            // immediately — so the read still completes synchronously
            // while having observed the stop token. See io_env, IoAwaitable.
            bool
            await_suspend(
                std::coroutine_handle<>,
                io_env const* env) noexcept
            {
                canceled_ = env->stop_token.stop_requested();
                return false;
            }

            [[nodiscard]] io_result<std::size_t>
            await_resume()
            {
                // Empty buffer is a no-op regardless of
                // stream state, stop token, or fuse.
                if(buffer_empty(buffers_))
                    return {std::error_code(), 0};

                if(canceled_)
                    return {error::canceled, 0};

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
                return {std::error_code(), n};
            }
        };
        return awaitable{this, buffers};
    }
};

} // test
} // capy
} // boost

#endif
