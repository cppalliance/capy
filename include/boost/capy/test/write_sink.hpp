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
#include <boost/capy/buffers/copy.hpp>
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

/** A mock sink for testing write operations.

    Use this to verify code that performs complete writes without needing
    real I/O. Call @ref write to write data, then @ref data to retrieve
    what was written. The associated @ref fuse enables error injection
    at controlled points.

    Unlike @ref write_stream which provides partial writes via `write_some`,
    this class satisfies the @ref WriteSink concept by providing complete
    writes and EOF signaling.

    @par Thread Safety
    Not thread-safe.

    @par Example
    @code
    fuse f;
    write_sink ws( f );

    auto r = f.armed( [&]( fuse& ) -> task<void> {
        auto [ec, n] = co_await ws.write(
            const_buffer( "Hello", 5 ) );
        if( ec.failed() )
            co_return;
        auto [ec2] = co_await ws.write_eof();
        if( ec2.failed() )
            co_return;
        // ws.data() returns "Hello"
    } );
    @endcode

    @see fuse, WriteSink
*/
class write_sink
{
    fuse& f_;
    std::string data_;
    std::string expect_;
    bool eof_called_ = false;

    system::error_code
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
    */
    explicit write_sink(fuse& f) noexcept
        : f_(f)
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
    system::error_code
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

    /** Asynchronously write data to the sink.

        Transfers all bytes from the provided const buffer sequence to
        the internal buffer. Before every write, the attached @ref fuse
        is consulted to possibly inject an error for testing fault
        scenarios. The returned `std::size_t` is the number of bytes
        transferred.

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
    write(CB const& buffers)
    {
        struct awaitable
        {
            write_sink* self_;
            CB const& buffers_;

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
                auto ec = self_->f_.maybe_fail();
                if(ec.failed())
                    return {ec, 0};

                std::size_t const n = buffer_size(buffers_);
                if(n == 0)
                    return {{}, 0};

                std::size_t const old_size = self_->data_.size();
                self_->data_.resize(old_size + n);
                copy(make_buffer(
                    self_->data_.data() + old_size, n), buffers_);

                ec = self_->consume_match_();
                if(ec.failed())
                    return {ec, n};

                return {{}, n};
            }
        };
        return awaitable{this, buffers};
    }

    /** Asynchronously write data to the sink with optional EOF.

        Transfers all bytes from the provided const buffer sequence to
        the internal buffer, optionally signaling end-of-stream. Before
        every write, the attached @ref fuse is consulted to possibly
        inject an error for testing fault scenarios. The returned
        `std::size_t` is the number of bytes transferred.

        @par Effects
        On success, appends the written bytes to the internal buffer.
        If `eof` is `true`, marks the sink as finalized.
        If an error is injected by the fuse, the internal buffer remains
        unchanged.

        @par Exception Safety
        No-throw guarantee.

        @param buffers The const buffer sequence containing data to write.
        @param eof If true, signals end-of-stream after writing.

        @return An awaitable yielding `(error_code,std::size_t)`.

        @see fuse
    */
    template<ConstBufferSequence CB>
    auto
    write(CB const& buffers, bool eof)
    {
        struct awaitable
        {
            write_sink* self_;
            CB const& buffers_;
            bool eof_;

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
                auto ec = self_->f_.maybe_fail();
                if(ec.failed())
                    return {ec, 0};

                std::size_t const n = buffer_size(buffers_);
                if(n > 0)
                {
                    std::size_t const old_size = self_->data_.size();
                    self_->data_.resize(old_size + n);
                    copy(make_buffer(
                        self_->data_.data() + old_size, n), buffers_);

                    ec = self_->consume_match_();
                    if(ec.failed())
                        return {ec, n};
                }

                if(eof_)
                    self_->eof_called_ = true;

                return {{}, n};
            }
        };
        return awaitable{this, buffers, eof};
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

            void await_suspend(
                coro,
                executor_ref,
                std::stop_token) const noexcept
            {
            }

            io_result<>
            await_resume()
            {
                auto ec = self_->f_.maybe_fail();
                if(ec.failed())
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
