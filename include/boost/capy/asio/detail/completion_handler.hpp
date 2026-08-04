//
// Copyright (c) 2026 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_ASIO_DETAIL_COMPLETION_HANDLER_HPP
#define BOOST_CAPY_ASIO_DETAIL_COMPLETION_HANDLER_HPP

#include <boost/capy/asio/detail/asio_coroutine_unique_handle.hpp>
#include <boost/capy/asio/executor_adapter.hpp>
#include <boost/capy/ex/executor_ref.hpp>
#include <boost/capy/ex/io_env.hpp>
#include <boost/capy/io_result.hpp>


#include <memory_resource>
#include <optional>
#include <system_error>
#include <tuple>
#include <type_traits>

namespace boost::capy::detail
{

struct asio_immediate_executor_helper
{
  enum completed_immediately_t
  {
    no, maybe, yes, initiating
  };

  executor_ref exec;
  completed_immediately_t * completed_immediately = nullptr;

  template<typename Fn>
  void execute(Fn && fn) const
  {
    // only allow it when we're still initializing
    if (completed_immediately &&
        ((*completed_immediately == initiating)
      || (*completed_immediately == maybe)))
    {
      // only use this indicator if the fn will actually call our handler
      // otherwise this was a single op in a composed operation
      *completed_immediately = maybe;
      fn();
      
      if (*completed_immediately != yes)
        *completed_immediately = initiating;
    }
    else
    {
      exec.post(
        make_continuation(
          std::forward<Fn>(fn), 
          std::pmr::polymorphic_allocator<void>(
            exec.context().get_frame_allocator())));
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
  
  asio_immediate_executor_helper(
      const asio_immediate_executor_helper & rhs) noexcept = default;
      
  asio_immediate_executor_helper(
      executor_ref inner, 
      completed_immediately_t * completed_immediately
      ) : exec(std::move(inner)), completed_immediately(completed_immediately)
  {
  }
};


template<typename CancellationSlot, typename ... Args>
struct asio_coroutine_completion_handler
{
  asio_coroutine_unique_handle handle;
  std::optional<std::tuple<Args...>> & result;
  const capy::io_env * env;
  CancellationSlot slot;
  using completed_immediately_t = asio_immediate_executor_helper::completed_immediately_t;
  
  completed_immediately_t * completed_immediately = nullptr;
  
  using allocator_type = std::pmr::polymorphic_allocator<void>;
  allocator_type get_allocator() const {return env->frame_allocator;}

  using executor_type = asio_executor_adapter<executor_ref>;
  executor_type get_executor() const {return env->executor;}

  using cancellation_slot_type = CancellationSlot;
  cancellation_slot_type get_cancellation_slot() const {return slot;}

  using immediate_executor_type = asio_immediate_executor_helper;
  immediate_executor_type get_immediate_executor() const
  {
    return immediate_executor_type{env->executor, completed_immediately };
  };

  asio_coroutine_completion_handler(
    std::coroutine_handle<void> h, 
    std::optional<std::tuple<Args...>> & result,
    const capy::io_env * env,
    CancellationSlot slot = {},
    completed_immediately_t * ci = nullptr)
    : handle(h)
    , result(result)
    , env(env)
    , slot(slot), completed_immediately(ci)
  {}

  asio_coroutine_completion_handler(
      asio_coroutine_completion_handler &&
      ) noexcept = default;
      
  void operator()(Args ... args)
  {
    result.emplace(std::forward<Args>(args)...);

    auto h = handle.release();

    if (completed_immediately != nullptr
    && *completed_immediately == completed_immediately_t::maybe)
    {
      *completed_immediately = completed_immediately_t::yes;
    }
    else
      h();
  }
};

template<typename ... Ts>
struct async_result_impl_result_tuple
{
  using type = std::tuple<Ts...>;
};


template<typename T0, typename ... Ts>
  requires std::constructible_from<io_result<Ts...>, T0, Ts...>
struct async_result_impl_result_tuple<T0, Ts...>
{
  using type = io_result<Ts...>;
};

inline std::tuple<> make_async_result(const std::tuple<> &) { return {}; }

template<typename E, typename ... Ts>
inline auto make_async_result(std::tuple<E, Ts...> && tup) 
{
  if constexpr (std::convertible_to<E, std::error_code>) 
    return std::apply(
              [](auto &&e, auto &&... args)
              {
                  return io_result(std::move(e), std::move(args)...);
              },
              std::move(tup));
  
  else
    return std::move(tup);
}


template<typename CancellationSignal, typename CancellationType, typename ... Ts> 
struct async_result_impl
{
    template<typename Initiation, typename... Args>
    struct awaitable_t
    {
        using completed_immediately_t 
            = asio_immediate_executor_helper::completed_immediately_t;
    
        CancellationSignal signal;
        completed_immediately_t completed_immediately;
        
        struct cb
        {
          CancellationSignal &signal;
          cb(CancellationSignal &signal) : signal(signal) {}
          void operator()() {signal.emit(CancellationType::terminal); }
        };
        std::optional<std::stop_callback<cb>> stopper;
        
        bool await_ready() const {return false;}

        bool await_suspend(std::coroutine_handle<> h, const capy::io_env * env)
        {
          completed_immediately = completed_immediately_t::initiating;
          stopper.emplace(env->stop_token, signal);
          using slot_t = std::decay_t<decltype(CancellationSignal().slot())>;
          capy::detail::asio_coroutine_completion_handler<slot_t, Ts...> ch(
            h, result_, env, 
            signal.slot(), 
            &completed_immediately);

          std::apply(
            [&](auto ... args) 
            {
              std::move(init_)(
                std::move(ch), 
                std::move(args)...);
            }, 
            std::move(args_));

          if (completed_immediately == completed_immediately_t::initiating)
            completed_immediately = completed_immediately_t::no;
          return completed_immediately != completed_immediately_t::yes;
        }

        auto await_resume() 
        {
          return make_async_result(std::move(*result_)); 
        }


        awaitable_t(Initiation init, std::tuple<Args...> args) 
              : init_(std::move(init)), args_(std::move(args)) 
        {
        }
        
        awaitable_t(awaitable_t && rhs) noexcept 
            : init_(std::move(rhs.init_))
            , args_(std::move(rhs.args_))
            , result_(std::move(rhs.result_)) {}
      private:
        Initiation init_;
        // if Args<0> == error_code this needs to be an io_result.
        typename async_result_impl_result_tuple<Args...>::type args_;
        std::optional<std::tuple<Ts...>> result_;
    };

    template <typename Initiation, typename RawToken, typename... Args>
    static auto initiate(Initiation&& initiation,
        RawToken&&, Args&&... args)
    {
      return awaitable_t<
            std::decay_t<Initiation>, 
            std::decay_t<Args>...>(
            std::forward<Initiation>(initiation),
            std::make_tuple(std::forward<Args>(args)...));
    }
};


}

#endif //BOOST_CAPY_ASIO_DETAIL_COMPLETION_HANDLER

