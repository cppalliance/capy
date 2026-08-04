// Copyright (c) 2026 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_ASIO_DETAIL_AS_IO_AWAITABLE_HPP
#define BOOST_CAPY_ASIO_DETAIL_AS_IO_AWAITABLE_HPP

#include <type_traits>
#include <utility>

namespace boost::capy
{

/** @defgroup asio Asio Integration
 *  @brief Components for integrating capy coroutines with Asio
 *
 *  This module provides seamless integration between capy's coroutine
 *  framework and both Boost.Asio and standalone Asio. It enables:
 *  - Using capy coroutines as Asio completion tokens
 *  - Wrapping Asio executors for use with capy
 *  - Spawning capy coroutines from Asio contexts
 *  @{
 */

/** @brief Completion token for awaiting Asio async operations in capy coroutines.
 *
 *  `as_io_awaitable_t` is a completion token type that allows Asio async
 *  operations to be `co_await`ed within capy's IO coroutines. When used as a
 *  completion token, the async operation returns an io-awaitable.
 *
 *  @par Example
 *  @code
 *  capy::io_task<std::size_t> read_some(asio::ip::tcp::socket& socket)
 *  {
 *      std::array<char, 1024> buffer;
 *      auto [ec, n] = co_await socket.async_read_some(
 *          asio::buffer(buffer),
 *          capy::as_io_awaitable);
 *      if (ec)
 *          throw std::system_error(ec);
 *      co_return n;
 *  }
 *  @endcode
 *
 *  @par Cancellation
 *  When the capy coroutine receives a stop request, a terminal cancellation
 *  signal is emitted to the underlying Asio operation.
 *
 *  @see as_io_awaitable The global instance of this type
 *  @see executor_with_default For setting this as the default token
 */
struct as_io_awaitable_t
{
  /// Default constructor.
  constexpr as_io_awaitable_t()
  {
  }

  /** @brief Executor adapter that sets `as_io_awaitable_t` as the default token.
   *
   *  This nested class template wraps an executor and specifies
   *  `as_io_awaitable_t` as the default completion token type. I/O objects
   *  using this executor will default to returning awaitables when no
   *  completion token is explicitly provided.
   *
   *  @tparam InnerExecutor The underlying executor type to wrap
   *
   *  @par Example
   *  @code
   *  using socket_type = asio::basic_stream_socket<
   *      asio::ip::tcp,
   *      as_io_awaitable_t::executor_with_default<asio::any_io_executor>>;
   *
   *  // Now async operations default to returning awaitables:
   *  auto bytes = co_await socket.async_read_some(buffer);
   *  @endcode
   */
  template <typename InnerExecutor>
  struct executor_with_default : InnerExecutor
  {
    /// The default completion token type for I/O objects using this executor.
    typedef as_io_awaitable_t default_completion_token_type;

    executor_with_default(const InnerExecutor& ex) noexcept
        : InnerExecutor(ex)
    {
    }

    /// Construct the adapted executor from the inner executor type.
    template <typename InnerExecutor1>
    executor_with_default(
      const InnerExecutor1& ex,
      typename std::enable_if<
        std::conditional<
          !std::is_same<InnerExecutor1, executor_with_default>::value,
          std::is_convertible<InnerExecutor1, InnerExecutor>,
          std::false_type
        >::type::value>::type = 0) noexcept
      : InnerExecutor(ex)
    {
    }
  };

  /** @brief Type alias to rebind an I/O object to use `as_io_awaitable_t` as default.
   *
   *  Given an I/O object type `T` (e.g., `asio::ip::tcp::socket`), this alias
   *  produces a new type that uses `executor_with_default` and thus defaults
   *  to `as_io_awaitable_t` for all async operations.
   *
   *  @tparam T The I/O object type to adapt (must support `rebind_executor`)
   *
   *  @par Example
   *  @code
   *  using awaitable_socket = as_io_awaitable_t::as_default_on_t<
   *      asio::ip::tcp::socket>;
   *  @endcode
   */
  template <typename T>
  using as_default_on_t = typename T::template rebind_executor<
        executor_with_default<typename T::executor_type> >::other;

  /** @brief Adapts an I/O object instance to use `as_io_awaitable_t` as default.
   *
   *  This function takes an existing I/O object and returns a new object of
   *  the same kind but rebound to use `executor_with_default`, making
   *  `as_io_awaitable_t` the default completion token.
   *
   *  @tparam T The I/O object type (deduced)
   *  @param object The I/O object to adapt
   *  @return A new I/O object with `as_io_awaitable_t` as the default token
   *
   *  @par Example
   *  @code
   *  asio::ip::tcp::socket raw_socket(io_context);
   *  auto socket = as_io_awaitable_t::as_default_on(std::move(raw_socket));
   *
   *  // Now you can omit the completion token:
   *  auto bytes = co_await socket.async_read_some(buffer);
   *  @endcode
   */
  template <typename T>
  static typename std::decay_t<T>::template rebind_executor<
      executor_with_default<typename std::decay_t<T>::executor_type>
    >::other
  as_default_on(T && object)
  {
    return typename std::decay_t<T>::template rebind_executor<
        executor_with_default<typename std::decay_t<T>::executor_type>
      >::other(std::forward<T>(object));
  }
};

/** @brief Global instance of `as_io_awaitable_t` for convenient use.
 *
 *  Use this constant as a completion token to await Asio async operations.
 *
 *  @par Example
 *  @code
 *  auto result = co_await socket.async_read_some(buffer, as_io_awaitable);
 *  @endcode
 */
constexpr as_io_awaitable_t as_io_awaitable;

/** @} */ // end of asio group

}

#endif

