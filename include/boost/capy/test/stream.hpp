//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_TEST_STREAM_HPP
#define BOOST_CAPY_TEST_STREAM_HPP

#include <boost/capy/detail/config.hpp>
#include <boost/capy/buffers.hpp>
#include <boost/capy/buffers/buffer_copy.hpp>
#include <boost/capy/buffers/make_buffer.hpp>
#include <boost/capy/coro.hpp>
#include <boost/capy/ex/executor_ref.hpp>
#include <boost/capy/io_result.hpp>
#include <boost/capy/error.hpp>
#include <boost/capy/test/fuse.hpp>

#include <algorithm>
#include <stop_token>
#include <string>
#include <string_view>

namespace boost {
namespace capy {
namespace test {

/** A mock stream for testing both read and write operations.

    Use this to verify code that performs reads and writes without
    needing real I/O. Call @ref provide to supply data for reads,
    then @ref read_some to consume it. Call @ref write_some to write
    data, then @ref data to retrieve what was written. The associated
    @ref fuse enables error injection at controlled points. Optional
    `max_read_size` and `max_write_size` constructor parameters limit
    bytes per operation to simulate chunked delivery.

    @par Thread Safety
    Not thread-safe.

    @par Example
    @code
    fuse f;
    stream s( f );
    s.provide( "Hello, " );
    s.provide( "World!" );

    auto r = f.armed( [&]( fuse& ) -> task<void> {
        char buf[32];
        auto [ec, n] = co_await s.read_some(
            mutable_buffer( buf, sizeof( buf ) ) );
        if( ec )
            co_return;
        // buf contains "Hello, World!"

        auto [ec2, n2] = co_await s.write_some(
            const_buffer( "Response", 8 ) );
        if( ec2 )
            co_return;
        // s.data() returns "Response"
    } );
    @endcode

    @see fuse, read_stream, write_stream
*/
class stream
{
    fuse* f_;
    std::string read_data_;
    std::size_t read_pos_ = 0;
    std::string write_data_;
    std::string expect_;
    std::size_t max_read_size_;
    std::size_t max_write_size_;

    std::error_code
    consume_match_() noexcept
    {
        if(write_data_.empty() || expect_.empty())
            return {};
        std::size_t const n = (std::min)(write_data_.size(), expect_.size());
        if(std::string_view(write_data_.data(), n) !=
            std::string_view(expect_.data(), n))
            return error::test_failure;
        write_data_.erase(0, n);
        expect_.erase(0, n);
        return {};
    }

public:
    /** Construct a stream.

        @param f The fuse used to inject errors during operations.

        @param max_read_size Maximum bytes returned per read.
        Use to simulate chunked network delivery.

        @param max_write_size Maximum bytes transferred per write.
        Use to simulate chunked network delivery.
    */
    explicit stream(
        fuse& f,
        std::size_t max_read_size = std::size_t(-1),
        std::size_t max_write_size = std::size_t(-1)) noexcept
        : f_(&f)
        , max_read_size_(max_read_size)
        , max_write_size_(max_write_size)
    {
    }

    //--------------------------------------------
    //
    // Read operations
    //
    //--------------------------------------------

    /** Append data to be returned by subsequent reads.

        Multiple calls accumulate data that @ref read_some returns.

        @param sv The data to append.
    */
    void
    provide(std::string_view sv)
    {
        read_data_.append(sv);
    }

    /// Clear all read data and reset the read position.
    void
    clear() noexcept
    {
        read_data_.clear();
        read_pos_ = 0;
    }

    /// Return the number of bytes available for reading.
    std::size_t
    available() const noexcept
    {
        return read_data_.size() - read_pos_;
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
            stream* self_;
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
                auto ec = self_->f_->maybe_fail();
                if(ec)
                    return {ec, 0};

                if(self_->read_pos_ >= self_->read_data_.size())
                    return {error::eof, 0};

                std::size_t avail = self_->read_data_.size() - self_->read_pos_;
                if(avail > self_->max_read_size_)
                    avail = self_->max_read_size_;
                auto src = make_buffer(
                    self_->read_data_.data() + self_->read_pos_, avail);
                std::size_t const n = buffer_copy(buffers_, src);
                self_->read_pos_ += n;
                return {{}, n};
            }
        };
        return awaitable{this, buffers};
    }

    //--------------------------------------------
    //
    // Write operations
    //
    //--------------------------------------------

    /// Return the written data as a string view.
    std::string_view
    data() const noexcept
    {
        return write_data_;
    }

    /** Set the expected data for subsequent writes.

        Stores the expected data and immediately tries to match
        against any data already written. Matched data is consumed
        from both buffers.

        @param sv The expected data.

        @return An error if existing data does not match.
    */
    std::error_code
    expect(std::string_view sv)
    {
        expect_.assign(sv);
        return consume_match_();
    }

    /// Return the number of bytes written.
    std::size_t
    size() const noexcept
    {
        return write_data_.size();
    }

    /** Asynchronously write data to the stream.

        Transfers up to `buffer_size( buffers )` bytes from the provided
        const buffer sequence to the internal buffer. Before every write,
        the attached @ref fuse is consulted to possibly inject an error
        for testing fault scenarios. The returned `std::size_t` is the
        number of bytes transferred.

        @par Effects
        On success, appends the written bytes to the internal buffer.
        If an error is injected by the fuse, the internal buffer remains
        unchanged.

        @par Exception Safety
        No-throw guarantee.

        @param buffers The const buffer sequence containing data to write.

        @return An awaitable yielding `(error_code,std::size_t)`.

        @see fuse
    */
    template<ConstBufferSequence CB>
    auto
    write_some(CB buffers)
    {
        struct awaitable
        {
            stream* self_;
            CB buffers_;

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
                auto ec = self_->f_->maybe_fail();
                if(ec)
                    return {ec, 0};

                std::size_t n = buffer_size(buffers_);
                n = (std::min)(n, self_->max_write_size_);
                if(n == 0)
                    return {{}, 0};

                std::size_t const old_size = self_->write_data_.size();
                self_->write_data_.resize(old_size + n);
                buffer_copy(make_buffer(
                    self_->write_data_.data() + old_size, n), buffers_, n);

                ec = self_->consume_match_();
                if(ec)
                    return {ec, n};

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
