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
#include <boost/capy/asio/detail/completion_handler.hpp>
#include <boost/capy/ex/frame_allocator.hpp>

#include <boost/asio/dispatch.hpp>
#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/io_context.hpp>

namespace boost {
namespace capy {



template<typename Executor = boost::asio::any_io_executor>
  requires requires (Executor exec)
  {
    {
      boost::asio::prefer(
        std::move(exec), 
        boost::asio::execution::outstanding_work.tracked
        )
    } -> std::convertible_to<Executor>;
    {
      boost::asio::prefer(
        std::move(exec), 
        boost::asio::execution::outstanding_work.untracked
        )
    } -> std::convertible_to<Executor>;
  }
struct executor_from_asio_properties
{
  executor_from_asio_properties(Executor executor) 
      noexcept(std::is_nothrow_move_constructible_v<Executor>) 
      : executor_(std::move(executor)) 
  {
  }
  executor_from_asio_properties(executor_from_asio_properties && rhs) 
      noexcept(std::is_nothrow_move_constructible_v<Executor>) 
      : executor_(std::move(rhs.executor_)) 
  {
  }
  executor_from_asio_properties(const executor_from_asio_properties & rhs) 
      noexcept(std::is_nothrow_copy_constructible_v<Executor>) 
      : executor_(rhs.executor_)
  {
  }

  execution_context& context() const noexcept
  {
    auto & ec = boost::asio::query(executor_, boost::asio::execution::context);
    return boost::asio::use_service<detail::asio_context_service<boost::asio::execution_context>>(ec); 
  }

  void on_work_started() const noexcept
  {

    using boost::asio::execution::outstanding_work;
    if (boost::asio::query(executor_, outstanding_work) == outstanding_work.untracked)
        executor_ = boost::asio::prefer(
          std::move(executor_), outstanding_work.tracked);
  }

  void on_work_finished() const noexcept
  {
    using boost::asio::execution::outstanding_work;
    if (boost::asio::query(executor_, outstanding_work) == outstanding_work.tracked)
      executor_ = boost::asio::prefer(
          std::move(executor_), outstanding_work.untracked);
  }

  std::coroutine_handle<> dispatch(continuation & c) const
  {
    boost::asio::dispatch(
      executor_, 
      detail::asio_coroutine_unique_handle(c.h)
    );
    return std::noop_coroutine();
  }

  void post(continuation & c) const
  {
    boost::asio::post(
      executor_, 
      detail::asio_coroutine_unique_handle(c.h)
    );
  }

  bool operator==(const executor_from_asio_properties & rhs) const noexcept 
  { 
    return executor_  == rhs.executor_;
  }
  bool operator!=(const executor_from_asio_properties & rhs) const noexcept 
  {
    return executor_  != rhs.executor_;
  }

 private:
  mutable Executor executor_;
};


template<typename Executor>
  requires requires (Executor exec)
  {
    exec.on_work_started();
    exec.on_work_finished();
  }
struct executor_from_asio_net_ts
{
  executor_from_asio_net_ts(Executor executor) 
      noexcept(std::is_nothrow_move_constructible_v<Executor>) 
      : executor_(std::move(executor)) 
  {
  }
  executor_from_asio_net_ts(executor_from_asio_net_ts && rhs) 
      noexcept(std::is_nothrow_move_constructible_v<Executor>) 
      : executor_(std::move(rhs.executor_)) 
  {
  }
  executor_from_asio_net_ts(const executor_from_asio_net_ts & rhs) 
      noexcept(std::is_nothrow_copy_constructible_v<Executor>) 
      : executor_(rhs.executor_)
  {
  }

  execution_context& context() const noexcept
  {
      return boost::asio::use_service<detail::asio_context_service<boost::asio::execution_context>>
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

  bool operator==(const executor_from_asio_net_ts & rhs) const noexcept 
  { 
    return executor_  == rhs.executor_;
  }
  bool operator!=(const executor_from_asio_net_ts & rhs) const noexcept 
  {
    return executor_  != rhs.executor_;
  }

 private:
  Executor executor_;
};


namespace detail
{


struct executor_from_asio_net_ts_helper
{
  template<typename Executor>
  using impl = executor_from_asio_net_ts<Executor>;
};

struct executor_from_asio_properties_helper
{
  template<typename Executor>
  using impl = executor_from_asio_properties<Executor>;
};

template<typename Executor>
using executor_from_asio_helper = 
  std::conditional_t<
      requires (Executor exec) {{exec.on_work_started()};},
      executor_from_asio_net_ts_helper, 
      executor_from_asio_properties_helper>
    ::template impl<Executor>;

}

template<typename Executor = boost::asio::any_io_executor>
struct executor_from_asio : detail::executor_from_asio_helper<Executor>
{
    using detail::executor_from_asio_helper<Executor>::executor_from_asio_helper;
};

template<typename Executor>
executor_from_asio(Executor) -> executor_from_asio<Executor>;

}
}


#endif //BOOST_CAPY_ASIO_EXECUTOR_ADAPTER_HPP
