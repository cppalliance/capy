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


struct as_io_awaitable_t 
{
  /// Default constructor.
  constexpr as_io_awaitable_t()
  {
  }

  /// Adapts an executor to add the @c use_op_t completion token as the
  /// default.
  template <typename InnerExecutor>
  struct executor_with_default : InnerExecutor
  {
    /// Specify @c use_op_t as the default completion token type.
    typedef as_io_awaitable_t default_completion_token_type;

    executor_with_default(const InnerExecutor& ex) noexcept
        : InnerExecutor(ex)
    {
    }

    /// Construct the adapted executor from the inner executor type.
    template <typename InnerExecutor1>
    executor_with_default(const InnerExecutor1& ex,
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

  /// Type alias to adapt an I/O object to use @c use_op_t as its
  /// default completion token type.
  template <typename T>
  using as_default_on_t = typename T::template rebind_executor<
        executor_with_default<typename T::executor_type> >::other;

  /// Function helper to adapt an I/O object to use @c use_op_t as its
  /// default completion token type.
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
constexpr as_io_awaitable_t as_io_awaitable;


}

#endif
