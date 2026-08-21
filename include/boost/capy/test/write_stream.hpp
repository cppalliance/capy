//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
// Copyright (c) 2026 Michael Vandeberg
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_TEST_WRITE_STREAM_HPP
#define BOOST_CAPY_TEST_WRITE_STREAM_HPP

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
#include <string>
#include <string_view>

namespace boost {
namespace capy {
namespace test {

/** Captures bytes passed to `write_some`, retrievable afterward through `data`.

    Use this to verify code that performs writes without needing
    real I/O. Call @ref write_some to write data, then @ref data
    to retrieve what was written. The associated @ref fuse enables
    error injection at controlled points. An optional
    `max_write_size` constructor parameter limits bytes per write
    to simulate chunked delivery.

    This class satisfies the @ref WriteStream concept.

    @par Thread Safety
    Not thread-safe.

    @par Example
    @par !example example


    @see fuse, WriteStream
*/
class write_stream
{
    fuse f_;
    std::string data_;
    std::string expect_;
    std::size_t max_write_size_;

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
    /** Construct a write stream.

        @param f The fuse used to inject errors during writes.

        @param max_write_size Maximum bytes transferred per write.
        Use to simulate chunked network delivery.
    */
    explicit write_stream(
        fuse f = {},
        std::size_t max_write_size = std::size_t(-1)) noexcept
        : f_(std::move(f))
        , max_write_size_(max_write_size)
    {
    }

    /** Return the written data as a string view.

        @return A view of bytes written but not yet matched by @ref expect.
    */
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

    /** Return the number of bytes written.

        @return The number of bytes written but not yet matched by @ref expect.
    */
    std::size_t
    size() const noexcept
    {
        return data_.size();
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
        Injected I/O conditions are reported via the `error_code`
        component of the result. Throws `std::system_error` only when
        the attached @ref fuse is in exception mode and reaches its
        failure point; no-throw otherwise.

        @par Cancellation
        If the environment's stop token is requested, the write
        completes immediately with `error::canceled` and transfers no
        data. An empty buffer sequence is a no-op that completes
        successfully regardless of the stop token.

        @param buffers The const buffer sequence containing data to write.

        @return An awaitable that await-returns `(error_code,std::size_t)`.

        @throws std::system_error When the attached @ref fuse is in
        exception mode and reaches its failure point.

        @see fuse
    */
    template<ConstBufferSequence CB>
    auto
    write_some(CB buffers)
    {
        struct awaitable
        {
            write_stream* self_;
            CB buffers_;
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

            [[nodiscard]] io_result<std::size_t>
            await_resume()
            {
                if(buffer_empty(buffers_))
                    return {std::error_code(), 0};

                if(canceled_)
                    return {error::canceled, 0};

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
