//
// Copyright (c) 2026 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_ASIO_EXECUTOR_FROM_ASIO_HPP
#define BOOST_CAPY_ASIO_EXECUTOR_FROM_ASIO_HPP

#include <boost/capy/asio/detail/asio_context_service.hpp>
#include <boost/capy/asio/detail/asio_coroutine_unique_handle.hpp>
#include <boost/capy/asio/detail/fwd.hpp>
#include <boost/capy/ex/frame_allocator.hpp>


#include <memory_resource>
#include <type_traits>

namespace boost {
namespace capy {

/** @addtogroup asio
 *  @{
 */

namespace detail
{

/** @brief Concept for legacy Networking TS style Asio executors.
 *  @internal
 *
 *  Matches executors that provide the original Networking TS executor interface
 *  with explicit work counting (`on_work_started`/`on_work_finished`) and
 *  `dispatch`/`post` methods that take a handler and allocator.
 *
 *  @tparam Executor The type to check
 */
template<typename Executor>
concept AsioNetTsExecutor =  requires (Executor exec,
                     std::coroutine_handle<> h,
                     std::pmr::polymorphic_allocator<void> a)
    {
      exec.on_work_started();
      exec.on_work_finished();
      exec.dispatch(h, a);
      exec.post(h, a);
      exec.context();
    } ;

/** @brief Concept for Boost.Asio standard executors.
 *  @internal
 *
 *  Matches executors compatible with the P0443/P2300 executor model as
 *  implemented in Boost.Asio. These executors use the property query/require
 *  mechanism and return `boost::asio::execution_context&` from context queries.
 *
 *  @tparam Executor The type to check
 */
template<typename Executor>
concept AsioBoostStandardExecutor = std::same_as<
      typename boost::asio::query_result<
        Executor,
        boost::asio::execution::detail::context_t<0>>::type,
        boost::asio::execution_context&>;

/** @brief Concept for standalone Asio standard executors.
 *  @internal
 *
 *  Matches executors compatible with the P0443/P2300 executor model as
 *  implemented in standalone Asio. These executors return
 *  `::asio::execution_context&` from context queries.
 *
 *  @tparam Executor The type to check
 */
template<typename Executor>
concept AsioStandaloneStandardExecutor = std::same_as<
      typename ::asio::query_result<
        Executor,
        ::asio::execution::detail::context_t<0>>::type,
        ::asio::execution_context&>;

}


/** @brief Wraps a legacy Networking TS executor for use with capy.
 *
 *  This class adapts Asio executors that follow the original Networking TS
 *  executor model (with `on_work_started`/`on_work_finished` and
 *  `dispatch`/`post` methods) to be usable as capy executors.
 *
 *  @tparam Executor An executor type satisfying `AsioNetTsExecutor`
 *
 *  @par Example
 *  @code
 *  boost::asio::io_context io;
 *  auto wrapped = asio_net_ts_executor(io.get_executor());
 *
 *  // Use with capy coroutines
 *  capy::run(wrapped, my_coroutine());
 *  @endcode
 *
 *  @see wrap_asio_executor For automatic executor type detection
 */
template<detail::AsioNetTsExecutor Executor>
struct asio_net_ts_executor
{
  /** @brief Constructs from an Asio Net.TS executor.
   *  @param executor The Asio executor to wrap
   */
  asio_net_ts_executor(Executor executor)
      noexcept(std::is_nothrow_move_constructible_v<Executor>)
      : executor_(std::move(executor))
  {
  }

  /** @brief Move constructor. */
  asio_net_ts_executor(asio_net_ts_executor && rhs)
      noexcept(std::is_nothrow_move_constructible_v<Executor>)
      : executor_(std::move(rhs.executor_))
  {
  }

  /** @brief Copy constructor. */
  asio_net_ts_executor(const asio_net_ts_executor & rhs)
      noexcept(std::is_nothrow_copy_constructible_v<Executor>)
      : executor_(rhs.executor_)
  {
  }

  /** @brief Returns the associated capy execution context.
   *
   *  The context is obtained via an `asio_context_service` registered
   *  with the underlying Asio execution context.
   *
   *  @return Reference to the capy execution_context
   */
  execution_context& context() const noexcept
  {
      using ex_t = std::remove_reference_t<decltype(executor_.context())>;
      return use_service<detail::asio_context_service<ex_t>>
            (
              executor_.context()
            );
  }

  /** @brief Notifies that work has started.
   *
   *  Forwards to the underlying executor's `on_work_started()`.
   */
  void on_work_started() const noexcept
  {
    executor_.on_work_started();
  }

  /** @brief Notifies that work has finished.
   *
   *  Forwards to the underlying executor's `on_work_finished()`.
   */
  void on_work_finished() const noexcept
  {
    executor_.on_work_finished();
  }

  /** @brief Dispatches a continuation for execution.
   *
   *  May execute inline if allowed by the executor, otherwise posts.
   *
   *  @param c The continuation to dispatch
   *  @return A noop coroutine handle (execution is delegated to Asio)
   */
  std::coroutine_handle<> dispatch(continuation & c) const
  {
    executor_.dispatch(
      detail::asio_coroutine_unique_handle(c.h),
      std::pmr::polymorphic_allocator<void>(
        boost::capy::get_current_frame_allocator()));

    return std::noop_coroutine();
  }

  /** @brief Posts a continuation for deferred execution.
   *
   *  The continuation will never be executed inline.
   *
   *  @param c The continuation to post
   */
  void post(continuation & c) const
  {
    executor_.post(
      detail::asio_coroutine_unique_handle(c.h),
      std::pmr::polymorphic_allocator<void>(
        boost::capy::get_current_frame_allocator()));
  }

  /** @brief Equality comparison. */
  bool operator==(const asio_net_ts_executor & rhs) const noexcept
  {
    return executor_  == rhs.executor_;
  }

  /** @brief Inequality comparison. */
  bool operator!=(const asio_net_ts_executor & rhs) const noexcept
  {
    return executor_  != rhs.executor_;
  }

 private:
  Executor executor_;
};


/** @brief Wraps a Boost.Asio standard executor for use with capy.
 *
 *  Forward declaration; defined in `boost.hpp`.
 *
 *  @tparam Executor An executor type satisfying `AsioBoostStandardExecutor`
 *  @see asio_net_ts_executor For legacy executor wrapping
 */
template<detail::AsioBoostStandardExecutor Executor>
struct asio_boost_standard_executor;

/** @brief Wraps a standalone Asio standard executor for use with capy.
 *
 *  Forward declaration; defined in `standalone.hpp`.
 *
 *  @tparam Executor An executor type satisfying `AsioStandaloneStandardExecutor`
 *  @see asio_net_ts_executor For legacy executor wrapping
 */
template<detail::AsioStandaloneStandardExecutor Executor>
struct asio_standalone_standard_executor;


/** @brief Automatically wraps any Asio executor for use with capy.
 *
 *  This function detects the type of Asio executor and returns the appropriate
 *  capy-compatible wrapper:
 *  - Legacy Net.TS executors -> `asio_net_ts_executor`
 *  - Boost.Asio standard executors -> `asio_boost_standard_executor`
 *  - Standalone Asio standard executors -> `asio_standalone_standard_executor`
 *
 *  @tparam Executor The Asio executor type (deduced)
 *  @param exec The Asio executor to wrap
 *  @return A capy-compatible executor wrapping the input
 *
 *  @par Example
 *  @code
 *  boost::asio::io_context io;
 *  auto capy_exec = wrap_asio_executor(io.get_executor());
 *
 *  // Now use with capy
 *  capy::run(capy_exec, my_io_task());
 *  @endcode
 *
 *  @note Fails to compile with a static_assert if the executor type is not recognized.
 */
template<typename Executor>
auto wrap_asio_executor(Executor && exec)
{
  using executor_t = std::decay_t<Executor>;
  if constexpr (detail::AsioNetTsExecutor<executor_t>)
    return asio_net_ts_executor<executor_t>(
              std::forward<Executor>(exec)
            );
  else if constexpr (detail::AsioBoostStandardExecutor<executor_t>)
    return asio_boost_standard_executor<executor_t>(
              std::forward<Executor>(exec)
            );
  else if constexpr (detail::AsioStandaloneStandardExecutor<executor_t>)
    return asio_standalone_standard_executor<executor_t>(
              std::forward<Executor>(exec)
            );
  else
    static_assert(sizeof(Executor) == 0, "Unknown executor type");
};


/** @brief Type alias for the result of `wrap_asio_executor`.
 *
 *  Given an Asio executor type, this alias yields the corresponding
 *  capy wrapper type.
 *
 *  @tparam Executor The Asio executor type
 */
template<typename Executor>
using wrap_asio_executor_t
        = decltype(wrap_asio_executor(std::declval<const Executor &>()));

/** @} */ // end of asio group



}
}


#endif //BOOST_CAPY_ASIO_EXECUTOR_ADAPTER_HPP
