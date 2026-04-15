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
namespace detail
{

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

template<typename Executor>
concept AsioBoostStandardExecutor = std::same_as<
      typename boost::asio::query_result<
        Executor, 
        boost::asio::execution::detail::context_t<0>>::type, 
        boost::asio::execution_context&>;

template<typename Executor>
concept AsioStandaloneStandardExecutor = std::same_as<
      typename ::asio::query_result<
        Executor, 
        ::asio::execution::detail::context_t<0>>::type, 
        ::asio::execution_context&>;

}


template<detail::AsioNetTsExecutor Executor>
struct asio_net_ts_executor
{
  asio_net_ts_executor(Executor executor) 
      noexcept(std::is_nothrow_move_constructible_v<Executor>) 
      : executor_(std::move(executor)) 
  {
  }
  asio_net_ts_executor(asio_net_ts_executor && rhs) 
      noexcept(std::is_nothrow_move_constructible_v<Executor>) 
      : executor_(std::move(rhs.executor_)) 
  {
  }
  asio_net_ts_executor(const asio_net_ts_executor & rhs) 
      noexcept(std::is_nothrow_copy_constructible_v<Executor>) 
      : executor_(rhs.executor_)
  {
  }

  execution_context& context() const noexcept
  {
      using ex_t = std::remove_reference_t<decltype(executor_.context())>;
      return use_service<detail::asio_context_service<ex_t>>
            (
              executor_.context()
            ); 
  }

  void on_work_started() const noexcept
  {
    executor_.on_work_started();
  }

  void on_work_finished() const noexcept
  {
    executor_.on_work_finished();
  }

  std::coroutine_handle<> dispatch(continuation & c) const
  {
    executor_.dispatch(
      detail::asio_coroutine_unique_handle(c.h), 
      std::pmr::polymorphic_allocator<void>(
        boost::capy::get_current_frame_allocator()));
      
    return std::noop_coroutine();
  }

  void post(continuation & c) const
  {
    executor_.post(
      detail::asio_coroutine_unique_handle(c.h),
      std::pmr::polymorphic_allocator<void>(
        boost::capy::get_current_frame_allocator()));
  }

  bool operator==(const asio_net_ts_executor & rhs) const noexcept 
  { 
    return executor_  == rhs.executor_;
  }
  bool operator!=(const asio_net_ts_executor & rhs) const noexcept 
  {
    return executor_  != rhs.executor_;
  }

 private:
  Executor executor_;
};


template<detail::AsioBoostStandardExecutor Executor>
struct asio_boost_standard_executor;

template<detail::AsioStandaloneStandardExecutor Executor>
struct asio_standalone_standard_executor;


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


template<typename Executor>
using wrap_asio_executor_t 
        = decltype(wrap_asio_executor(std::declval<const Executor &>()));



}
}


#endif //BOOST_CAPY_ASIO_EXECUTOR_ADAPTER_HPP
