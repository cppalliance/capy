//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_TEST_WRITE_SINK_HPP
#define BOOST_CAPY_TEST_WRITE_SINK_HPP

#include <boost/capy/detail/config.hpp>
#include <boost/capy/buffers.hpp>
#include <boost/capy/buffers/buffer_copy.hpp>
#include <boost/capy/buffers/make_buffer.hpp>
#include <coroutine>
#include <boost/capy/ex/io_env.hpp>
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

/** A mock sink for testing write operations.

    Use this to verify code that performs complete writes without needing
    real I/O. Call @ref write to write data, then @ref data to retrieve
    what was written. The associated @ref fuse enables error injection
    at controlled points.

    This class satisfies the @ref WriteSink concept by providing partial
    writes via `write_some` (satisfying @ref WriteStream), complete
    writes via `write`, and EOF signaling via `write_eof`.

    @par Thread Safety
    Not thread-safe.

    @par Example
    @code
    fuse f;
    write_sink ws( f );

    auto r = f.armed( [&]( fuse& ) -> task<void> {
        auto [ec, n] = co_await ws.write(
            const_buffer( "Hello", 5 ) );
        if( ec )
            co_return;
        auto [ec2] = co_await ws.write_eof();
        if( ec2 )
            co_return;
        // ws.data() returns "Hello"
    } );
    @endcode

    @see fuse, WriteSink
*/
class write_sink
{
    fuse f_;
    std::string data_;
    std::string expect_;
    std::size_t max_write_size_;
    bool eof_called_ = false;

    std::error_code
    consume_match_() noexcept
    {
        if(data_.empty() || expect_.empty())
            return {};
        std::size_t const n = (std::min)(data_.size(), expect_.size());
        if(std::string_view(data_.data(), n) !=
            std::string_view(expect_.data(), n))
            return error::test_failure;
        data_.erase(0, n);
        expect_.erase(0, n);
        return {};
    }

public:
    /** Construct a write sink.

        @param f The fuse used to inject errors during writes.

        @param max_write_size Maximum bytes transferred per write.
        Use to simulate chunked delivery.
    */
    explicit write_sink(
        fuse f = {},
        std::size_t max_write_size = std::size_t(-1)) noexcept
        : f_(std::move(f))
        , max_write_size_(max_write_size)
    {
    }

    /// Return the written data as a string view.
    std::string_view
    data() const noexcept
    {
        return data_;
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
        return data_.size();
    }

    /// Return whether write_eof has been called.
    bool
    eof_called() const noexcept
    {
        return eof_called_;
    }

    /// Clear all data and reset state.
    void
    clear() noexcept
    {
        data_.clear();
        expect_.clear();
        eof_called_ = false;
    }

    /** Asynchronously write some data to the sink.

        Transfers up to `buffer_size( buffers )` bytes from the provided
        const buffer sequence to the internal buffer. Before every write,
        the attached @ref fuse is consulted to possibly inject an error.

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
            write_sink* self_;
            CB buffers_;

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

                std::size_t n = buffer_size(buffers_);
                n = (std::min)(n, self_->max_write_size_);

                std::size_t const old_size = self_->data_.size();
                self_->data_.resize(old_size + n);
                buffer_copy(make_buffer(
                    self_->data_.data() + old_size, n), buffers_, n);

                ec = self_->consume_match_();
                if(ec)
                {
                    self_->data_.resize(old_size);
                    return {ec, 0};
                }

                return {{}, n};
            }
        };
        return awaitable{this, buffers};
    }

    /** Asynchronously write data to the sink.

        Transfers all bytes from the provided const buffer sequence
        to the internal buffer. Unlike @ref write_some, this ignores
        `max_write_size` and writes all available data, matching the
        @ref WriteSink semantic contract.

        @param buffers The const buffer sequence containing data to write.

        @return An awaitable yielding `(error_code,std::size_t)`.

        @see fuse
    */
    template<ConstBufferSequence CB>
    auto
    write(CB buffers)
    {
        struct awaitable
        {
            write_sink* self_;
            CB buffers_;

            bool await_ready() const noexcept { return true; }

            void await_suspend(
                std::coroutine_handle<>,
                io_env const*) const noexcept
            {
            }

            io_result<std::size_t>
            await_resume()
            {
                auto ec = self_->f_.maybe_fail();
                if(ec)
                    return {ec, 0};

                std::size_t n = buffer_size(buffers_);
                if(n == 0)
                    return {{}, 0};

                std::size_t const old_size = self_->data_.size();
                self_->data_.resize(old_size + n);
                buffer_copy(make_buffer(
                    self_->data_.data() + old_size, n), buffers_);

                ec = self_->consume_match_();
                if(ec)
                    return {ec, n};

                return {{}, n};
            }
        };
        return awaitable{this, buffers};
    }

    /** Atomically write data and signal end-of-stream.

        Transfers all bytes from the provided const buffer sequence to
        the internal buffer and signals end-of-stream. Before the write,
        the attached @ref fuse is consulted to possibly inject an error
        for testing fault scenarios.

        @par Effects
        On success, appends the written bytes to the internal buffer
        and marks the sink as finalized.
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
    write_eof(CB buffers)
    {
        struct awaitable
        {
            write_sink* self_;
            CB buffers_;

            bool await_ready() const noexcept { return true; }

            void await_suspend(
                std::coroutine_handle<>,
                io_env const*) const noexcept
            {
            }

            io_result<std::size_t>
            await_resume()
            {
                auto ec = self_->f_.maybe_fail();
                if(ec)
                    return {ec, 0};

                std::size_t n = buffer_size(buffers_);
                if(n > 0)
                {
                    std::size_t const old_size = self_->data_.size();
                    self_->data_.resize(old_size + n);
                    buffer_copy(make_buffer(
                        self_->data_.data() + old_size, n), buffers_);

                    ec = self_->consume_match_();
                    if(ec)
                        return {ec, n};
                }

                self_->eof_called_ = true;

                return {{}, n};
            }
        };
        return awaitable{this, buffers};
    }

    /** Signal end-of-stream.

        Marks the sink as finalized, indicating no more data will be
        written. Before signaling, the attached @ref fuse is consulted
        to possibly inject an error for testing fault scenarios.

        @par Effects
        On success, marks the sink as finalized.
        If an error is injected by the fuse, the state remains unchanged.

        @par Exception Safety
        No-throw guarantee.

        @return An awaitable yielding `(error_code)`.

        @see fuse
    */
    auto
    write_eof()
    {
        struct awaitable
        {
            write_sink* self_;

            bool await_ready() const noexcept { return true; }

            // This method is required to satisfy Capy's IoAwaitable concept,
            // but is never called because await_ready() returns true.
            // See the comment on write(CB buffers) for a detailed explanation.
            void await_suspend(
                std::coroutine_handle<>,
                io_env const*) const noexcept
            {
            }

            io_result<>
            await_resume()
            {
                auto ec = self_->f_.maybe_fail();
                if(ec)
                    return {ec};

                self_->eof_called_ = true;
                return {};
            }
        };
        return awaitable{this};
    }
};

} // test
} // capy
} // boost

#endif
