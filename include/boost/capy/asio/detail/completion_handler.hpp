//
// Copyright (c) 2026 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_ASIO_DETAIL_COMPLETION_HANDLER
#define BOOST_CAPY_ASIO_DETAIL_COMPLETION_HANDLER

#include <boost/capy/asio/detail/asio_coroutine_unique_handle.hpp>
#include <boost/capy/asio/executor_adapter.hpp>
#include <boost/capy/ex/executor_ref.hpp>
#include <boost/capy/ex/io_env.hpp>

#include <boost/asio/cancellation_signal.hpp>
#include <boost/asio/post.hpp>

#include <memory_resource>
#include <optional>
#include <tuple>

namespace boost::capy::detail
{


struct asio_immediate_executor_helper
{
  enum completed_immediately_t
  {
    no, maybe, yes, initiating
  };

  asio_executor_adapter<executor_ref> exec;
  completed_immediately_t * completed_immediately = nullptr;

  template<typename Fn>
  void execute(Fn && fn) const
  {
    // only allow it when we're still initializing
    if (completed_immediately &&
        ((*completed_immediately == initiating)
      || (*completed_immediately == maybe)))
    {
      // only use this indicator if the fn will actually call our completion-handler
      // otherwise this was a single op in a composed operation
      *completed_immediately = maybe;
      fn();
      
      if (*completed_immediately != yes)
        *completed_immediately = initiating;
    }
    else
    {
      boost::asio::post(exec, std::forward<Fn>(fn));
    }
  }
  
  friend bool operator==(const asio_immediate_executor_helper& lhs,
                         const asio_immediate_executor_helper& rhs) noexcept
  {
    return lhs.exec == rhs.exec;
  }

  friend bool operator!=(const asio_immediate_executor_helper& lhs,
                         const asio_immediate_executor_helper& rhs) noexcept
  {
    return lhs.exec != rhs.exec;
  }
  
  asio_immediate_executor_helper(const asio_immediate_executor_helper & rhs) noexcept = default;
  asio_immediate_executor_helper(executor_ref inner, completed_immediately_t * completed_immediately)
        : exec(std::move(inner)), completed_immediately(completed_immediately)
  {
  }
};


template<typename ... Args>
struct asio_coroutine_completion_handler
{
  struct deleter 
  {
    deleter() = default;
    void operator()(void * h) const
    {
      std::coroutine_handle<void>::from_address(h).destroy();
    }
  };
  asio_coroutine_unique_handle handle;
  std::optional<std::tuple<Args...>> & result;
  capy::io_env * env;
  boost::asio::cancellation_slot slot;
  asio_immediate_executor_helper::completed_immediately_t * completed_immediately = nullptr;
  
  using allocator_type = std::pmr::polymorphic_allocator<void>;
  allocator_type get_allocator() const {return env->frame_allocator;}

  using executor_type = asio_executor_adapter<executor_ref>;
  executor_type get_executor() const {return env->executor;}

  using cancellation_slot_type = boost::asio::cancellation_slot;
  cancellation_slot_type get_cancellation_slot() const {return slot;}

  using immediate_executor_type = asio_immediate_executor_helper;
  immediate_executor_type get_immediate_executor() const
  {
    return immediate_executor_type{env->executor, completed_immediately };
  };

  asio_coroutine_completion_handler(
    std::coroutine_handle<void> h, 
    std::optional<std::tuple<Args...>> & result,
    capy::io_env * env,
    boost::asio::cancellation_slot slot = {},
    asio_immediate_executor_helper::completed_immediately_t * ci = nullptr)
    : handle(h), result(result), env(env), slot(slot), completed_immediately(ci) {}

  asio_coroutine_completion_handler(
      asio_coroutine_completion_handler &&
      ) noexcept = default;
      
  void operator()(Args ... args)
  {
    result.emplace(std::forward<Args>(args)...);
    std::move(handle)();
  }
};

}

#endif //BOOST_CAPY_ASIO_DETAIL_COMPLETION_HANDLER
