//
// Copyright (c) 2026 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef CAPY_EXAMPLE_UNI_STREAM_HPP
#define CAPY_EXAMPLE_UNI_STREAM_HPP

#include <boost/asio/associated_executor.hpp>
#include <boost/asio/async_result.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/post.hpp>

#include "capy_streams.hpp"

#include <boost/capy/buffers/asio.hpp>
#include <boost/capy/io/any_stream.hpp>
#include <boost/capy/ex/run_async.hpp>
#include <boost/capy/task.hpp>

#include <utility>

namespace detail {

template<class AsioExecutor>
class asio_executor_wrapper
{
    AsioExecutor ex_;
    asio_context* ctx_;

public:
    asio_executor_wrapper(
        AsioExecutor ex,
        asio_context* ctx) noexcept
        : ex_(std::move(ex))
        , ctx_(ctx)
    {
    }

    bool operator==(asio_executor_wrapper const& other) const noexcept
    {
        return ex_ == other.ex_;
    }

    asio_context& context() const noexcept
    {
        return *ctx_;
    }

    void on_work_started() const noexcept {}
    void on_work_finished() const noexcept {}

    std::coroutine_handle<> dispatch(std::coroutine_handle<> h) const
    {
        net::post(ex_, [h]{ h.resume(); });
        return std::noop_coroutine();
    }

    void post(std::coroutine_handle<> h) const
    {
        net::post(ex_, [h]{ h.resume(); });
    }
};

} // namespace detail

/** Asio-compatible stream wrapper for capy::any_stream.

    This class wraps a capy::any_stream and provides async_read_some
    and async_write_some member functions that conform to Asio's
    Universal Async Model, accepting completion tokens.

    @par Example
    @code
    asio_context ctx;
    auto [s1, s2] = make_stream_pair(ctx);
    uni_stream stream(std::move(s1), ctx);

    // Use with completion handler
    stream.async_read_some(net::buffer(data, size),
        [](boost::system::error_code ec, std::size_t n) {
            // handle completion
        });

    // Use with use_awaitable
    auto [ec, n] = co_await stream.async_read_some(
        net::buffer(data, size), net::as_tuple(net::use_awaitable));
    @endcode
*/
class uni_stream
{
    capy::any_stream stream_;
    asio_context* ctx_;

    class initiate_async_read_some
    {
        uni_stream* self_;

    public:
        explicit initiate_async_read_some(uni_stream* self) noexcept
            : self_(self)
        {
        }

        template<class ReadHandler, class MutableBufferSequence>
        void operator()(
            ReadHandler&& handler,
            MutableBufferSequence const& buffers) const
        {
            auto ex = net::get_associated_executor(
                handler, self_->ctx_->context().get_executor());

            detail::asio_executor_wrapper wrapper(std::move(ex), self_->ctx_);

            capy::run_async(wrapper)(
                [](capy::any_stream* stream,
                   MutableBufferSequence bufs,
                   ReadHandler h) -> capy::task<>
                {
                    auto [ec, n] = co_await stream->read_some(
                        capy::from_asio(bufs));
                    std::move(h)(ec, n);
                }(&self_->stream_, buffers, std::forward<ReadHandler>(handler)));
        }
    };

    class initiate_async_write_some
    {
        uni_stream* self_;

    public:
        explicit initiate_async_write_some(uni_stream* self) noexcept
            : self_(self)
        {
        }

        template<class WriteHandler, class ConstBufferSequence>
        void operator()(
            WriteHandler&& handler,
            ConstBufferSequence const& buffers) const
        {
            auto ex = net::get_associated_executor(
                handler, self_->ctx_->context().get_executor());

            detail::asio_executor_wrapper wrapper(std::move(ex), self_->ctx_);

            capy::run_async(wrapper)(
                [](capy::any_stream* stream,
                   ConstBufferSequence bufs,
                   WriteHandler h) -> capy::task<>
                {
                    auto [ec, n] = co_await stream->write_some(
                        capy::from_asio(bufs));
                    std::move(h)(ec, n);
                }(&self_->stream_, buffers, std::forward<WriteHandler>(handler)));
        }
    };

public:
    /// The executor type associated with the stream.
    using executor_type = net::io_context::executor_type;

    /** Constructor.

        @param stream The any_stream to wrap (moved).
        @param ctx The asio_context for executor operations.
    */
    uni_stream(
        capy::any_stream stream,
        asio_context& ctx) noexcept
        : stream_(std::move(stream))
        , ctx_(&ctx)
    {
    }

    uni_stream(uni_stream const&) = delete;
    uni_stream& operator=(uni_stream const&) = delete;

    uni_stream(uni_stream&&) = default;
    uni_stream& operator=(uni_stream&&) = default;

    /** Returns the executor associated with the stream.
    */
    executor_type get_executor() const noexcept
    {
        return ctx_->context().get_executor();
    }

    /** Start an asynchronous read operation.

        This function reads data into the provided buffer sequence.
        The operation completes when at least one byte has been read,
        or an error occurs.

        @param buffers The buffer sequence to read into.
        @param token The completion token. The completion handler
            signature is `void(boost::system::error_code, std::size_t)`.

        @return The return value depends on the completion token type.
    */
    template<class MutableBufferSequence,
        BOOST_ASIO_COMPLETION_TOKEN_FOR(
            void(boost::system::error_code, std::size_t)) ReadToken>
    auto
    async_read_some(
        MutableBufferSequence const& buffers,
        ReadToken&& token)
    {
        return net::async_initiate<ReadToken,
            void(boost::system::error_code, std::size_t)>(
                initiate_async_read_some(this),
                token,
                buffers);
    }

    /** Start an asynchronous write operation.

        This function writes data from the provided buffer sequence.
        The operation completes when at least one byte has been written,
        or an error occurs.

        @param buffers The buffer sequence to write from.
        @param token The completion token. The completion handler
            signature is `void(boost::system::error_code, std::size_t)`.

        @return The return value depends on the completion token type.
    */
    template<class ConstBufferSequence,
        BOOST_ASIO_COMPLETION_TOKEN_FOR(
            void(boost::system::error_code, std::size_t)) WriteToken>
    auto
    async_write_some(
        ConstBufferSequence const& buffers,
        WriteToken&& token)
    {
        return net::async_initiate<WriteToken,
            void(boost::system::error_code, std::size_t)>(
                initiate_async_write_some(this),
                token,
                buffers);
    }
};

/** Create a connected pair of uni_stream.

    @param ctx The asio_context for both streams.
    @return A pair of connected uni_stream objects.
*/
inline
std::pair<uni_stream, uni_stream>
make_uni_pair(asio_context& ctx)
{
    auto [s1, s2] = make_stream_pair(ctx);
    return {
        uni_stream(std::move(s1), ctx),
        uni_stream(std::move(s2), ctx)
    };
}

#endif
