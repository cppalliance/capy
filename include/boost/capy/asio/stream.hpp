//
// Copyright (c) 2026 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//
#ifndef BOOST_CAPY_ASIO_STREAM_HPP
#define BOOST_CAPY_ASIO_STREAM_HPP

#include <boost/capy/asio/as_io_awaitable.hpp>
#include <boost/capy/asio/buffers.hpp>
#include <boost/capy/asio/executor_adapter.hpp>
#include <boost/capy/asio/spawn.hpp>
#include <boost/capy/concept/stream.hpp>
#include <boost/capy/concept/executor.hpp>
#include <boost/capy/ex/any_executor.hpp>
#include <boost/capy/io/any_stream.hpp>
#include <boost/capy/io/any_read_stream.hpp>
#include <boost/capy/io/any_write_stream.hpp>

namespace boost::capy
{

/** Wraps a capy ReadStream for use with Asio async operations.

    Adapts a capy `ReadStream` to provide Asio-style `async_read_some`
    operations with completion token support.

    @tparam Stream The underlying capy ReadStream type
    @tparam Exec The executor type

    @par Example
    @code
    capy::any_read_stream stream = ...;
    auto exec = capy::wrap_asio_executor(io.get_executor());
    capy::async_read_stream reader(std::move(stream), exec);

    auto [ec, n] = co_await reader.async_read_some(buffer, capy::as_io_awaitable);
    @endcode
*/
template<ReadStream Stream = any_stream, Executor Exec = any_executor>
struct async_read_stream
{
  using executor_type = asio_executor_adapter<Exec>;
  using next_layer_type = Stream;

  /// Default constructor.
  async_read_stream() = default;

  /// Construct from a stream and executor.
  async_read_stream(Stream stream, Exec executor)
      noexcept(std::is_nothrow_move_constructible_v<Stream> &&
               std::is_nothrow_move_constructible_v<Exec>)
      : stream_(std::move(stream))
      , executor_(std::move(executor))
  {
  }

  async_read_stream(const async_read_stream&)
      noexcept(std::is_nothrow_copy_constructible_v<Stream> &&
               std::is_nothrow_copy_constructible_v<Exec>) = default;
  async_read_stream(async_read_stream&&)
      noexcept(std::is_nothrow_move_constructible_v<Stream> &&
               std::is_nothrow_move_constructible_v<Exec>) = default;
  async_read_stream& operator=(const async_read_stream&)
      noexcept(std::is_nothrow_copy_assignable_v<Stream> &&
               std::is_nothrow_copy_assignable_v<Exec>) = default;
  async_read_stream& operator=(async_read_stream&&)
      noexcept(std::is_nothrow_move_assignable_v<Stream> &&
               std::is_nothrow_move_assignable_v<Exec>) = default;

  /// Returns the executor adapter for Asio compatibility.
  executor_type get_executor() const { return executor_; }

  /// Initiates an async read, returning a spawn handle.
  template<typename Buffer>
  auto async_read_some(Buffer && buffer)
  {
    return asio_spawn(
        executor_,
        stream_.read_some(as_asio_buffer_sequence(std::forward<Buffer>(buffer))));
  }

  /// Initiates an async read with a completion token.
  template<typename Buffer, typename CompletionToken>
  auto async_read_some(Buffer && buffer, CompletionToken && token)
  {
    return asio_spawn(
        executor_,
        stream_.read_some(as_asio_buffer_sequence(std::forward<Buffer>(buffer))),
        std::forward<CompletionToken>(token));
  }

  /// Returns a reference inter to the underlying stream.
        next_layer_type& next_layer()       { return stream_; }
  /// Returns a const reference to the underlying stream.
  const next_layer_type& next_layer() const { return stream_; }

 private:
  Stream stream_;
  Exec executor_;
};

// Deduction guides
template<ReadStream Stream, Executor Exec>
async_read_stream(Stream, Exec) -> async_read_stream<Stream, Exec>;


/** Wraps a capy WriteStream for use with Asio async operations.

    Adapts a capy `WriteStream` to provide Asio-style `async_write_some`
    operations with completion token support.

    @tparam Stream The underlying capy WriteStream type
    @tparam Exec The executor type

    @par Example
    @code
    capy::any_write_stream stream = ...;
    auto exec = capy::wrap_asio_executor(io.get_executor());
    capy::async_write_stream writer(std::move(stream), exec);

    auto [ec, n] = co_await writer.async_write_some(buffer, capy::as_io_awaitable);
    @endcode
*/
template<WriteStream Stream = any_stream, Executor Exec = any_executor>
struct async_write_stream
{
  using executor_type = asio_executor_adapter<Exec>;
  using next_layer_type = Stream;

  /// Default constructor.
  async_write_stream() = default;

  /// Construct from a stream and executor.
  async_write_stream(Stream stream, Exec executor)
      noexcept(std::is_nothrow_move_constructible_v<Stream> &&
               std::is_nothrow_move_constructible_v<Exec>)
      : stream_(std::move(stream))
      , executor_(std::move(executor))
  {
  }

  async_write_stream(const async_write_stream&)
      noexcept(std::is_nothrow_copy_constructible_v<Stream> &&
               std::is_nothrow_copy_constructible_v<Exec>) = default;
  async_write_stream(async_write_stream&&)
      noexcept(std::is_nothrow_move_constructible_v<Stream> &&
               std::is_nothrow_move_constructible_v<Exec>) = default;
  async_write_stream& operator=(const async_write_stream&)
      noexcept(std::is_nothrow_copy_assignable_v<Stream> &&
               std::is_nothrow_copy_assignable_v<Exec>) = default;
  async_write_stream& operator=(async_write_stream&&)
      noexcept(std::is_nothrow_move_assignable_v<Stream> &&
               std::is_nothrow_move_assignable_v<Exec>) = default;

  /// Returns the executor adapter for Asio compatibility.
  executor_type get_executor() const { return executor_; }

  /// Initiates an async write, returning a spawn handle.
  template<typename Buffer>
  auto async_write_some(Buffer && buffer)
  {
    
    return asio_spawn(
        executor_,
        stream_.write_some(as_asio_buffer_sequence(std::forward<Buffer>(buffer))));
  }

  /// Initiates an async write with a completion token.
  template<typename Buffer, typename CompletionToken>
  auto async_write_some(Buffer && buffer, CompletionToken && token)
  {
    return asio_spawn(
        executor_,
        stream_.write_some(as_asio_buffer_sequence(std::forward<Buffer>(buffer))),
        std::forward<CompletionToken>(token));
  }

  /// Returns a reference to the underlying stream.
        next_layer_type& next_layer()       { return stream_; }
  /// Returns a const reference to the underlying stream.
  const next_layer_type& next_layer() const { return stream_; }

 private:
  Stream stream_;
  Exec executor_;
};

// Deduction guides
template<WriteStream Stream, Executor Exec>
async_write_stream(Stream, Exec) -> async_write_stream<Stream, Exec>;


/** Wraps a capy Stream for use with Asio async operations.

    Adapts a capy `Stream` (supporting both read and write) to provide
    Asio-style `async_read_some` and `async_write_some` operations
    with completion token support.

    @tparam Stream The underlying capy Stream type
    @tparam Exec The executor type

    @par Example
    @code
    capy::any_stream stream = ...;
    auto exec = capy::wrap_asio_executor(io.get_executor());
    capy::async_stream sock(std::move(stream), exec);

    // Read
    auto [ec1, n1] = co_await sock.async_read_some(read_buf, capy::as_io_awaitable);

    // Write
    auto [ec2, n2] = co_await sock.async_write_some(write_buf, capy::as_io_awaitable);
    @endcode
*/
template<Stream Stream = any_stream, Executor Exec = any_executor>
struct async_stream
{
  using executor_type = asio_executor_adapter<Exec>;
  using next_layer_type = Stream;

  /// Default constructor.
  async_stream() = default;

  /// Construct from a stream and executor.
  async_stream(Stream stream, Exec executor)
      noexcept(std::is_nothrow_move_constructible_v<Stream> &&
               std::is_nothrow_move_constructible_v<Exec>)
      : stream_(std::move(stream))
      , executor_(std::move(executor))
  {
  }

  async_stream(const async_stream&)
      noexcept(std::is_nothrow_copy_constructible_v<Stream> &&
               std::is_nothrow_copy_constructible_v<Exec>) = default;
  async_stream(async_stream&&)
      noexcept(std::is_nothrow_move_constructible_v<Stream> &&
               std::is_nothrow_move_constructible_v<Exec>) = default;
  async_stream& operator=(const async_stream&)
      noexcept(std::is_nothrow_copy_assignable_v<Stream> &&
               std::is_nothrow_copy_assignable_v<Exec>) = default;
  async_stream& operator=(async_stream&&)
      noexcept(std::is_nothrow_move_assignable_v<Stream> &&
               std::is_nothrow_move_assignable_v<Exec>) = default;

  /// Returns the executor adapter for Asio compatibility.
  executor_type get_executor() const { return executor_; }

  /// Initiates an async read, returning a spawn handle.
  template<typename Buffer>
  auto async_read_some(Buffer && buffer)
  {
    return asio_spawn(
        executor_,
        stream_.read_some(as_asio_buffer_sequence(std::forward<Buffer>(buffer))));
  }

  /// Initiates an async read with a completion token.
  template<typename Buffer, typename CompletionToken>
  auto async_read_some(Buffer && buffer, CompletionToken && token)
  {
    return asio_spawn(
        executor_,
        stream_.read_some(as_asio_buffer_sequence(std::forward<Buffer>(buffer))),
        std::forward<CompletionToken>(token));
  }

  /// Initiates an async write, returning a spawn handle.
  template<typename Buffer>
  auto async_write_some(Buffer && buffer)
  {
    return asio_spawn(
        executor_,
        stream_.write_some(as_asio_buffer_sequence(std::forward<Buffer>(buffer))));
  }

  /// Initiates an async write with a completion token.
  template<typename Buffer, typename CompletionToken>
  auto async_write_some(Buffer && buffer, CompletionToken && token)
  {
    return asio_spawn(
        executor_,
        stream_.write_some(as_asio_buffer_sequence(std::forward<Buffer>(buffer))),
        std::forward<CompletionToken>(token));
  }

  /// Returns a reference to the underlying stream.
        next_layer_type& next_layer()       { return stream_; }
  /// Returns a const reference to the underlying stream.
  const next_layer_type& next_layer() const { return stream_; }

 private:
  Stream stream_;
  Exec executor_;
};

// Deduction guides
template<Stream StreamT, Executor Exec>
async_stream(StreamT, Exec) -> async_stream<StreamT, Exec>;

/** Wraps an Asio AsyncReadStream for use with capy's ReadStream concept.

    Adapts an Asio `AsyncReadStream` to provide capy-style `read_some`
    operations that return an IoAwaitable.

    @tparam AsyncReadStream The underlying Asio AsyncReadStream type

    @par Example
    @code
    boost::asio::ip::tcp::socket socket(io);
    capy::asio_read_stream reader(std::move(socket));

    auto result = co_await reader.read_some(buffer);
    @endcode
*/
template<typename AsyncReadStream>
struct asio_read_stream
{
  using executor_type = typename AsyncReadStream::executor_type;
  using next_layer_type = AsyncReadStream;

  /// Default constructor.
  asio_read_stream() = default;

  /// Construct from an Asio stream.
  explicit asio_read_stream(AsyncReadStream stream)
      noexcept(std::is_nothrow_move_constructible_v<AsyncReadStream>)
      : stream_(std::move(stream))
  {
  }

  asio_read_stream(const asio_read_stream&)
      noexcept(std::is_nothrow_copy_constructible_v<AsyncReadStream>) = default;
  asio_read_stream(asio_read_stream&&)
      noexcept(std::is_nothrow_move_constructible_v<AsyncReadStream>) = default;
  asio_read_stream& operator=(const asio_read_stream&)
      noexcept(std::is_nothrow_copy_assignable_v<AsyncReadStream>) = default;
  asio_read_stream& operator=(asio_read_stream&&)
      noexcept(std::is_nothrow_move_assignable_v<AsyncReadStream>) = default;

  /// Returns the executor from the underlying stream.
  executor_type get_executor() const { return stream_.get_executor(); }

  /// Initiates an async read, returning an IoAwaitable.
  template<MutableBufferSequence Seq>
  auto read_some(Seq buffers)
  {
    return stream_.async_read_some(as_asio_buffer_sequence(buffers), as_io_awaitable);
  }

  /// Returns a reference to the underlying stream.
        next_layer_type& next_layer()       { return stream_; }
  /// Returns a const reference to the underlying stream.
  const next_layer_type& next_layer() const { return stream_; }

 private:
  AsyncReadStream stream_;
};

// Deduction guide
template<typename AsyncReadStream>
asio_read_stream(AsyncReadStream) -> asio_read_stream<AsyncReadStream>;


/** Wraps an Asio AsyncWriteStream for use with capy's WriteStream concept.

    Adapts an Asio `AsyncWriteStream` to provide capy-style `write_some`
    operations that return an IoAwaitable.

    @tparam AsyncWriteStream The underlying Asio AsyncWriteStream type

    @par Example
    @code
    boost::asio::ip::tcp::socket socket(io);
    capy::asio_write_stream writer(std::move(socket));

    auto result = co_await writer.write_some(buffer);
    @endcode
*/
template<typename AsyncWriteStream>
struct asio_write_stream
{
  using executor_type = typename AsyncWriteStream::executor_type;
  using next_layer_type = AsyncWriteStream;

  /// Default constructor.
  asio_write_stream() = default;

  /// Construct from an Asio stream.
  explicit asio_write_stream(AsyncWriteStream stream)
      noexcept(std::is_nothrow_move_constructible_v<AsyncWriteStream>)
      : stream_(std::move(stream))
  {
  }

  asio_write_stream(const asio_write_stream&)
      noexcept(std::is_nothrow_copy_constructible_v<AsyncWriteStream>) = default;
  asio_write_stream(asio_write_stream&&)
      noexcept(std::is_nothrow_move_constructible_v<AsyncWriteStream>) = default;
  asio_write_stream& operator=(const asio_write_stream&)
      noexcept(std::is_nothrow_copy_assignable_v<AsyncWriteStream>) = default;
  asio_write_stream& operator=(asio_write_stream&&)
      noexcept(std::is_nothrow_move_assignable_v<AsyncWriteStream>) = default;

  /// Returns the executor from the underlying stream.
  executor_type get_executor() const { return stream_.get_executor(); }

  /// Initiates an async write, returning an IoAwaitable.
  template<ConstBufferSequence Seq>
  auto write_some(Seq buffers)
  {
    return stream_.async_write_some(as_asio_buffer_sequence(buffers), as_io_awaitable);
  }

  /// Returns a reference to the underlying stream.
        next_layer_type& next_layer()       { return stream_; }
  /// Returns a const reference to the underlying stream.
  const next_layer_type& next_layer() const { return stream_; }

 private:
  AsyncWriteStream stream_;
};

// Deduction guide
template<typename AsyncWriteStream>
asio_write_stream(AsyncWriteStream) -> asio_write_stream<AsyncWriteStream>;


/** Wraps an Asio AsyncStream for use with capy's Stream concept.

    Adapts an Asio stream (supporting both read and write) to provide
    capy-style `read_some` and `write_some` operations that return
    an IoAwaitable.

    @tparam AsyncStream The underlying Asio stream type

    @par Example
    @code
    boost::asio::ip::tcp::socket socket(io);
    capy::asio_stream stream(std::move(socket));

    // Read
    auto read_result = co_await stream.read_some(read_buf);

    // Write
    auto write_result = co_await stream.write_some(write_buf);
    @endcode
*/
template<typename AsyncStream>
struct asio_stream
{
  using executor_type = typename AsyncStream::executor_type;
  using next_layer_type = AsyncStream;

  /// Default constructor.
  asio_stream() = default;

  /// Construct from an Asio stream.
  explicit asio_stream(AsyncStream stream)
      noexcept(std::is_nothrow_move_constructible_v<AsyncStream>)
      : stream_(std::move(stream))
  {
  }

  asio_stream(const asio_stream&)
      noexcept(std::is_nothrow_copy_constructible_v<AsyncStream>) = default;
  asio_stream(asio_stream&&)
      noexcept(std::is_nothrow_move_constructible_v<AsyncStream>) = default;
  asio_stream& operator=(const asio_stream&)
      noexcept(std::is_nothrow_copy_assignable_v<AsyncStream>) = default;
  asio_stream& operator=(asio_stream&&)
      noexcept(std::is_nothrow_move_assignable_v<AsyncStream>) = default;

  /// Returns the executor from the underlying stream.
  executor_type get_executor() const { return stream_.get_executor(); }

  /// Initiates an async read, returning an IoAwaitable.
  template<MutableBufferSequence Seq>
  auto read_some(Seq buffers)
  {
    return stream_.async_read_some(as_asio_buffer_sequence(buffers), as_io_awaitable);
  }

  /// Initiates an async write, returning an IoAwaitable.
  template<ConstBufferSequence Seq>
  auto write_some(Seq buffers)
  {
    return stream_.async_write_some(as_asio_buffer_sequence(buffers), as_io_awaitable);
  }

  /// Returns a reference to the underlying stream.
        next_layer_type& next_layer()       { return stream_; }
  /// Returns a const reference to the underlying stream.
  const next_layer_type& next_layer() const { return stream_; }

 private:
  AsyncStream stream_;
};

// Deduction guide
template<typename AsyncStream>
asio_stream(AsyncStream) -> asio_stream<AsyncStream>;

}

#endif //BOOST_CAPY_ASIO_STREAM_HPP

