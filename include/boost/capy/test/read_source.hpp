//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_TEST_READ_SOURCE_HPP
#define BOOST_CAPY_TEST_READ_SOURCE_HPP

#include <boost/capy/detail/config.hpp>
#include <boost/capy/buffers.hpp>
#include <boost/capy/buffers/buffer_copy.hpp>
#include <boost/capy/buffers/make_buffer.hpp>
#include <coroutine>
#include <boost/capy/ex/io_env.hpp>
#include <boost/capy/io_result.hpp>
#include <boost/capy/error.hpp>
#include <boost/capy/test/fuse.hpp>

#include <string>
#include <string_view>

namespace boost {
namespace capy {
namespace test {

/** A mock source for testing read operations.

    Use this to verify code that performs complete reads without needing
    real I/O. Call @ref provide to supply data, then @ref read
    to consume it. The associated @ref fuse enables error injection
    at controlled points.

    This class satisfies the @ref ReadSource concept by providing both
    partial reads via `read_some` (satisfying @ref ReadStream) and
    complete reads via `read` that fill the entire buffer sequence
    before returning.

    @par Thread Safety
    Not thread-safe.

    @par Example
    @code
    fuse f;
    read_source rs( f );
    rs.provide( "Hello, " );
    rs.provide( "World!" );

    auto r = f.armed( [&]( fuse& ) -> task<void> {
        char buf[32];
        auto [ec, n] = co_await rs.read(
            mutable_buffer( buf, sizeof( buf ) ) );
        if( ec )
            co_return;
        // buf contains "Hello, World!"
    } );
    @endcode

    @see fuse, ReadSource
*/
class read_source
{
    fuse f_;
    std::string data_;
    std::size_t pos_ = 0;
    std::size_t max_read_size_;

public:
    /** Construct a read source.

        @param f The fuse used to inject errors during reads.

        @param max_read_size Maximum bytes returned per read.
        Use to simulate chunked delivery.
    */
    explicit read_source(
        fuse f = {},
        std::size_t max_read_size = std::size_t(-1)) noexcept
        : f_(std::move(f))
        , max_read_size_(max_read_size)
    {
    }

    /** Append data to be returned by subsequent reads.

        Multiple calls accumulate data that @ref read returns.

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

    /** Asynchronously read some data from the source.

        Transfers up to `buffer_size( buffers )` bytes from the internal
        buffer to the provided mutable buffer sequence. If no data
        remains, returns `error::eof`. Before every read, the attached
        @ref fuse is consulted to possibly inject an error for testing
        fault scenarios.

        @param buffers The mutable buffer sequence to receive data.

        @return An awaitable that await-returns `(error_code,std::size_t)`.

        @see fuse
    */
    template<MutableBufferSequence MB>
    auto
    read_some(MB buffers)
    {
        struct awaitable
        {
            read_source* self_;
            MB buffers_;

            bool await_ready() const noexcept { return true; }

            void await_suspend(
                std::coroutine_handle<>,
                io_env const*) const noexcept
            {
            }

            io_result<std::size_t>
            await_resume()
            {
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

    /** Asynchronously read data from the source.

        Fills the entire buffer sequence from the internal data.
        If the available data is less than the buffer size, returns
        `error::eof` with the number of bytes transferred. Before
        every read, the attached @ref fuse is consulted to possibly
        inject an error for testing fault scenarios.

        Unlike @ref read_some, this ignores `max_read_size` and
        transfers all available data in a single operation, matching
        the @ref ReadSource semantic contract.

        @param buffers The mutable buffer sequence to receive data.

        @return An awaitable that await-returns `(error_code,std::size_t)`.

        @see fuse
    */
    template<MutableBufferSequence MB>
    auto
    read(MB buffers)
    {
        struct awaitable
        {
            read_source* self_;
            MB buffers_;

            bool await_ready() const noexcept { return true; }

            void await_suspend(
                std::coroutine_handle<>,
                io_env const*) const noexcept
            {
            }

            io_result<std::size_t>
            await_resume()
            {
                if(buffer_empty(buffers_))
                    return {{}, 0};

                auto ec = self_->f_.maybe_fail();
                if(ec)
                    return {ec, 0};

                if(self_->pos_ >= self_->data_.size())
                    return {error::eof, 0};

                std::size_t avail = self_->data_.size() - self_->pos_;
                auto src = make_buffer(self_->data_.data() + self_->pos_, avail);
                std::size_t const n = buffer_copy(buffers_, src);
                self_->pos_ += n;

                if(n < buffer_size(buffers_))
                    return {error::eof, n};
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
