//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
// Copyright (c) 2026 Michael Vandeberg
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_TEST_BUFFER_SINK_HPP
#define BOOST_CAPY_TEST_BUFFER_SINK_HPP

#include <boost/capy/detail/config.hpp>
#include <boost/capy/buffers.hpp>
#include <boost/capy/buffers/make_buffer.hpp>
#include <coroutine>
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

/** A mock buffer sink for testing callee-owns-buffers write operations.

    Use this to verify code that writes data using the callee-owns-buffers
    pattern without needing real I/O. Call @ref prepare to get writable
    buffers, write into them, then call @ref commit to finalize. The
    associated @ref fuse enables error injection at controlled points.

    This class satisfies the @ref BufferSink concept by providing
    internal storage that callers write into directly.

    @par Thread Safety
    Not thread-safe.

    @par Example
    @code
    fuse f;
    buffer_sink bs( f );

    auto r = f.armed( [&]( fuse& ) -> task<void> {
        mutable_buffer arr[16];
        auto bufs = bs.prepare( arr );
        if( bufs.empty() )
            co_return;

        // Write data into the first prepared buffer
        std::memcpy( bufs[0].data(), "Hello", 5 );

        auto [ec] = co_await bs.commit( 5 );
        if( ec )
            co_return;

        auto [ec2] = co_await bs.commit_eof( 0 );
        // bs.data() returns "Hello"
    } );
    @endcode

    @see fuse, BufferSink
*/
class buffer_sink
{
    fuse f_;
    std::string data_;
    std::string prepare_buf_;
    std::size_t prepare_size_ = 0;
    std::size_t max_prepare_size_;
    bool eof_called_ = false;

public:
    /** Construct a buffer sink.

        @param f The fuse used to inject errors during commits.

        @param max_prepare_size Maximum bytes available per prepare.
        Use to simulate limited buffer space.
    */
    explicit buffer_sink(
        fuse f = {},
        std::size_t max_prepare_size = 4096) noexcept
        : f_(std::move(f))
        , max_prepare_size_(max_prepare_size)
    {
        prepare_buf_.resize(max_prepare_size_);
    }

    /// Return the written data as a string view.
    std::string_view
    data() const noexcept
    {
        return data_;
    }

    /// Return the number of bytes written.
    std::size_t
    size() const noexcept
    {
        return data_.size();
    }

    /// Return whether commit_eof has been called.
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
        prepare_size_ = 0;
        eof_called_ = false;
    }

    /** Prepare writable buffers.

        Fills the provided span with mutable buffer descriptors pointing
        to internal storage. The caller writes data into these buffers,
        then calls @ref commit to finalize.

        @param dest Span of mutable_buffer to fill.

        @return A span of filled buffers (empty or 1 buffer in this implementation).
    */
    std::span<mutable_buffer>
    prepare(std::span<mutable_buffer> dest)
    {
        if(dest.empty())
            return {};

        prepare_size_ = max_prepare_size_;
        dest[0] = make_buffer(prepare_buf_.data(), prepare_size_);
        return dest.first(1);
    }

    /** Commit bytes written to the prepared buffers.

        Transfers `n` bytes from the prepared buffer to the internal
        data buffer. Before committing, the attached @ref fuse is
        consulted to possibly inject an error for testing fault scenarios.

        @param n The number of bytes to commit.

        @return An awaitable that await-returns `(error_code)`.

        @par Cancellation
        If the environment's stop token has been requested, the commit
        completes immediately with `error::canceled` and commits no data.

        @see fuse
    */
    auto
    commit(std::size_t n)
    {
        struct awaitable
        {
            buffer_sink* self_;
            std::size_t n_;
            bool canceled_ = false;

            bool await_ready() const noexcept { return false; }

            // The operation completes synchronously, but await_suspend is
            // the only place io_env is delivered (the promise's
            // transform_awaiter forwards it here). Returning false means
            // the coroutine does not actually suspend; it resumes
            // immediately, having observed the stop token. See io_env,
            // IoAwaitable.
            bool
            await_suspend(
                std::coroutine_handle<>,
                io_env const* env) noexcept
            {
                canceled_ = env->stop_token.stop_requested();
                return false;
            }

            io_result<>
            await_resume()
            {
                if(canceled_)
                    return {error::canceled};

                auto ec = self_->f_.maybe_fail();
                if(ec)
                    return {ec};

                std::size_t to_commit = (std::min)(n_, self_->prepare_size_);
                self_->data_.append(self_->prepare_buf_.data(), to_commit);
                self_->prepare_size_ = 0;

                return {};
            }
        };
        return awaitable{this, n};
    }

    /** Commit final bytes and signal end-of-stream.

        Transfers `n` bytes from the prepared buffer to the internal
        data buffer and marks the sink as finalized. Before committing,
        the attached @ref fuse is consulted to possibly inject an error
        for testing fault scenarios.

        @param n The number of bytes to commit.

        @return An awaitable that await-returns `(error_code)`.

        @par Cancellation
        If the environment's stop token has been requested, the operation
        completes immediately with `error::canceled`, commits no data, and
        does not signal end-of-stream.

        @see fuse
    */
    auto
    commit_eof(std::size_t n)
    {
        struct awaitable
        {
            buffer_sink* self_;
            std::size_t n_;
            bool canceled_ = false;

            bool await_ready() const noexcept { return false; }

            // Reads the stop token without suspending; see the comment
            // on commit() for details.
            bool
            await_suspend(
                std::coroutine_handle<>,
                io_env const* env) noexcept
            {
                canceled_ = env->stop_token.stop_requested();
                return false;
            }

            io_result<>
            await_resume()
            {
                if(canceled_)
                    return {error::canceled};

                auto ec = self_->f_.maybe_fail();
                if(ec)
                    return {ec};

                std::size_t to_commit = (std::min)(n_, self_->prepare_size_);
                self_->data_.append(self_->prepare_buf_.data(), to_commit);
                self_->prepare_size_ = 0;

                self_->eof_called_ = true;
                return {};
            }
        };
        return awaitable{this, n};
    }
};

} // test
} // capy
} // boost

#endif
