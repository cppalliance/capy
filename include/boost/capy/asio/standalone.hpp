//
// Copyright (c) 2026 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_ASIO_STANDALONE_HPP
#define BOOST_CAPY_ASIO_STANDALONE_HPP


#include <boost/capy/asio/as_io_awaitable.hpp>
#include <boost/capy/concept/io_runnable.hpp>
#include <boost/capy/asio/detail/completion_handler.hpp>
#include <boost/capy/asio/executor_adapter.hpp>
#include <boost/capy/asio/executor_from_asio.hpp>
#include <boost/capy/asio/spawn.hpp>


#include <asio/append.hpp>
#include <asio/associated_allocator.hpp>
#include <asio/associated_cancellation_slot.hpp>
#include <asio/associated_immediate_executor.hpp>
#include <asio/dispatch.hpp>
#include <asio/cancellation_signal.hpp>
#include <asio/cancellation_type.hpp>
#include <asio/execution_context.hpp>
#include <asio/execution/allocator.hpp>
#include <asio/execution/blocking.hpp>
#include <asio/execution/context.hpp>
#include <asio/execution/outstanding_work.hpp>
#include <asio/execution/relationship.hpp>

namespace boost::capy
{


template<typename Executor, typename Allocator, int Bits>
::asio::execution_context& 
    query(const asio_executor_adapter<Executor, Allocator, Bits> & exec, 
          ::asio::execution::context_t) noexcept
{
  using service = detail::asio_adapter_context_service<
                                ::asio::execution_context>;
  return exec.context().
        template use_service<service>();
}

template<typename Executor, typename Allocator, int Bits>
constexpr ::asio::execution::blocking_t 
    query(const asio_executor_adapter<Executor, Allocator, Bits> & exec,
          ::asio::execution::blocking_t) noexcept
{
  switch (Bits & exec.blocking_mask)
  {
    case exec.blocking_never:    return ::asio::execution::blocking.never;
    case exec.blocking_always:   return ::asio::execution::blocking.always;
    case exec.blocking_possibly: return ::asio::execution::blocking.possibly;
    default: return {};
  }
}

template<typename Executor, typename Allocator, int Bits>
constexpr auto
    require(const asio_executor_adapter<Executor, Allocator, Bits> & exec, 
            ::asio::execution::blocking_t::possibly_t)
{
  constexpr int nb = (Bits & ~exec.blocking_mask) | exec.blocking_possibly;
  return asio_executor_adapter<Executor, Allocator, nb>(exec);
}

template<typename Executor, typename Allocator, int Bits>
constexpr auto
    require(const asio_executor_adapter<Executor, Allocator, Bits> & exec,
            ::asio::execution::blocking_t::never_t) 
{
  constexpr int nb = (Bits & ~exec.blocking_mask) | exec.blocking_never;
  return asio_executor_adapter<Executor, Allocator, nb>(exec);
}

template<typename Executor, typename Allocator, int Bits>
constexpr auto
    require(const asio_executor_adapter<Executor, Allocator, Bits> & exec, 
            ::asio::execution::blocking_t::always_t)
{
  constexpr int new_bits = (Bits & ~exec.blocking_mask) | exec.blocking_always;
  return asio_executor_adapter<Executor, Allocator, new_bits>(exec);
}

template<typename Executor, typename Allocator, int Bits>
static constexpr ::asio::execution::outstanding_work_t query(
    const asio_executor_adapter<Executor, Allocator, Bits> & exec, 
    ::asio::execution::outstanding_work_t) noexcept
{
  switch (Bits & exec.work_mask)
  {
    case exec.work_tracked:   
      return ::asio::execution::outstanding_work.tracked;
    case exec.work_untracked: 
      return ::asio::execution::outstanding_work.untracked;
    default: return {};
  }
}

template<typename Executor, typename Allocator, int Bits>
constexpr auto
    require(const asio_executor_adapter<Executor, Allocator, Bits> & exec, 
            ::asio::execution::outstanding_work_t::tracked_t) 
{
  constexpr int new_bits = (Bits & ~exec.work_mask) | exec.work_tracked;
  return asio_executor_adapter<Executor, Allocator, new_bits>(exec);
}

template<typename Executor, typename Allocator, int Bits>
constexpr auto
    require(const asio_executor_adapter<Executor, Allocator, Bits> & exec, 
            ::asio::execution::outstanding_work_t::untracked_t) 
{
  constexpr int new_bits = (Bits & ~exec.work_mask) | exec.work_untracked;
  return asio_executor_adapter<Executor, Allocator, new_bits>(exec);
}


template <typename Executor, typename Allocator, int Bits, typename OtherAlloc>
constexpr Allocator query(
    const asio_executor_adapter<Executor, Allocator, Bits> & exec, 
    ::asio::execution::allocator_t<OtherAlloc>) noexcept
{
  return exec.get_allocator();
}

template <typename Executor, typename Allocator, int Bits, typename OtherAlloc>
constexpr auto
    require(const asio_executor_adapter<Executor, Allocator, Bits> & exec,
            ::asio::execution::allocator_t<OtherAlloc> a) 
{
  return asio_executor_adapter<Executor, OtherAlloc, Bits>(
      exec, a.value()
    );
}

template <typename Executor, typename Allocator, int Bits>
constexpr auto
    require(const asio_executor_adapter<Executor, Allocator, Bits> & exec,
            ::asio::execution::allocator_t<void> a) 
              noexcept(std::is_nothrow_move_constructible_v<Executor>)
{
  return asio_executor_adapter<
            Executor, 
            std::pmr::polymorphic_allocator<void>, 
            Bits>
        (
          exec,
          exec.context().get_frame_allocator()
        );
}

namespace detail
{

template<typename Executor>
struct asio_standalone_work_tracker_service : 
            ::asio::execution_context::service
{
  static ::asio::execution_context::id id;
  
  asio_standalone_work_tracker_service(::asio::execution_context & ctx) 
        : ::asio::execution_context::service(ctx) {}

  using tracked_executor =
    typename ::asio::prefer_result<
      Executor,
      ::asio::execution::outstanding_work_t::tracked_t
      >::type;
      
  alignas(tracked_executor) char buffer[sizeof(tracked_executor) ];
  
  std::atomic_size_t work = 0u;

  void shutdown()
  {
    if (work.exchange(0) > 0u)
      reinterpret_cast<tracked_executor*>(buffer)->~tracked_executor();
  }


  void work_started(const Executor & exec)
  {
    if (work.fetch_add(1u) == 0u)
      new (buffer) tracked_executor(
        ::asio::prefer(exec, 
        ::asio::execution::outstanding_work.tracked));
  }

  void work_finished() 
  {
      if (work.fetch_sub(1u) == 1u)
        reinterpret_cast<tracked_executor*>(buffer)->~tracked_executor();
  }
};


template<typename Executor>
::asio::execution_context::id 
      asio_standalone_work_tracker_service<Executor>::id;


}

template<detail::AsioStandaloneStandardExecutor Executor>
struct asio_standalone_standard_executor
{

  asio_standalone_standard_executor(Executor executor) 
      noexcept(std::is_nothrow_move_constructible_v<Executor>) 
      : executor_(std::move(executor)) 
  {
  }
  asio_standalone_standard_executor(asio_standalone_standard_executor && rhs)
      noexcept(std::is_nothrow_move_constructible_v<Executor>) 
      : executor_(std::move(rhs.executor_)) 
  {
  }
  asio_standalone_standard_executor(
    const asio_standalone_standard_executor & rhs)
      noexcept(std::is_nothrow_copy_constructible_v<Executor>) 
      : executor_(rhs.executor_)
  {
  }

  execution_context& context() const noexcept
  {
    auto & ec = ::asio::query(executor_, ::asio::execution::context);
    return ::asio::use_service<
                    detail::asio_context_service<::asio::execution_context>
                              >(ec); 
  }

  void on_work_started() const noexcept
  {
    auto & ec = ::asio::query(executor_, ::asio::execution::context);
    ::asio::use_service<
              detail::asio_standalone_work_tracker_service<Executor>
                       >(ec).work_started(executor_);
  }

  void on_work_finished() const noexcept
  {
    auto & ec = ::asio::query(executor_, ::asio::execution::context);
    ::asio::use_service<
              detail::asio_standalone_work_tracker_service<Executor>
                       >(ec).work_finished();
  }


  std::coroutine_handle<> dispatch(continuation & c) const
  {
    ::asio::prefer(
        executor_,
        ::asio::execution::allocator(
          std::pmr::polymorphic_allocator<void>(
            context().get_frame_allocator()
            )
          )
        ).execute(detail::asio_coroutine_unique_handle(c.h));
    return std::noop_coroutine();
  }

  void post(continuation & c) const
  {
    ::asio::prefer(
        ::asio::require(executor_, ::asio::execution::blocking.never),
        ::asio::execution::relationship.fork,
        ::asio::execution::allocator(
          std::pmr::polymorphic_allocator<void>(
            context().get_frame_allocator()
            )
          )
        ).execute(detail::asio_coroutine_unique_handle(c.h));
  }
  bool operator==(
          const asio_standalone_standard_executor & rhs) const noexcept 
  { 
    return executor_  == rhs.executor_;
  }
  bool operator!=(
          const asio_standalone_standard_executor & rhs) const noexcept 
  {
    return executor_  != rhs.executor_;
  }

 private:
  Executor executor_;
};


template<Executor ExecutorType, 
         IoRunnable Runnable,
         ::asio::completion_token_for<
              detail::completion_signature_for_io_runnable<Runnable>
                                     > Token>
auto asio_spawn(ExecutorType exec, Runnable && runnable, Token token)
{
  return asio_spawn(exec, std::forward<Runnable>(runnable))(std::move(token));
}

template<ExecutionContext Context, 
         IoRunnable Runnable,
         ::asio::completion_token_for<
            detail::completion_signature_for_io_runnable<Runnable>
          > Token
        >
auto asio_spawn(Context & ctx, Runnable && runnable, Token token)
{
  return asio_spawn(ctx.get_executor(), std::forward<Runnable>(runnable))
                    (std::move(token));
}

}

template<typename ... Ts>
struct asio::async_result<boost::capy::as_io_awaitable_t, void(Ts...)>
      : boost::capy::detail::async_result_impl
                <
                  ::asio::cancellation_signal, 
                  ::asio::cancellation_type, 
                  Ts...
                >
{
};


namespace boost::capy::detail
{



struct boost_asio_standalone_init;

template<typename Allocator>
struct boost_asio_standalone_promise_type_allocator_base
{
  template<typename Handler, Executor Ex, IoRunnable Runnable>
  void * operator new   (std::size_t n, boost_asio_standalone_init &, 
                         Handler & handler, 
                         Ex & exec, Runnable & r)
  {
    using allocator_type = std::allocator_traits<Allocator>
                              ::template rebind_alloc<char>;
    allocator_type allocator(::asio::get_associated_allocator(handler));
    using traits = std::allocator_traits<allocator_type>;

    // round n up to max_align
    if (const auto d = n % sizeof(std::max_align_t); d >= 0u)
      n += (sizeof(std::max_align_t) - d);

    auto mem = std::allocator_traits<allocator_type>::
                template rebind_traits<char>::
                allocate(allocator, n + sizeof(allocator_type));

    void* p = static_cast<char*>(mem) + n;
    new (p) allocator_type(std::move(allocator));
    return mem;
  }
  void   operator delete(void * ptr, std::size_t n)
  {
    if (const auto d = n % sizeof(std::max_align_t); d >= 0u)
      n += (sizeof(std::max_align_t) - d);
  
    using allocator_type = std::allocator_traits<Allocator>
                              ::template rebind_alloc<char>;
    auto allocator_p = reinterpret_cast<allocator_type*>(
                                      static_cast<char*>(ptr) + n);
    auto allocator = std::move(*allocator_p);

    allocator_p->~allocator_type();
    allocator.deallocate(static_cast<char*>(ptr), n + sizeof(allocator_type));
  }
};


template<typename Handler, Executor Ex, IoRunnable Runnable>
struct boost_asio_standalone_init_promise_type 
    : boost_asio_standalone_promise_type_allocator_base<
            ::asio::associated_allocator_t<Handler>
          >
{
    using args_type = completion_tuple_for_io_runnable<Runnable>;
  
    boost_asio_standalone_init_promise_type(
          boost_asio_standalone_init &, 
          Handler & h, 
          Ex & exec, 
          Runnable & r)  
            : handler(h), ex(exec) {}

    Handler & handler;
    Ex &ex;

    void get_return_object() {}
    void unhandled_exception() {throw;}
    void return_value() {}

    std::suspend_never initial_suspend() noexcept {return {};}
    std::suspend_never   final_suspend() noexcept {return {};}

    struct completer 
    {
      Handler handler;
      Ex ex;
      args_type args;
      
      bool await_ready() const {return false;}
      void await_suspend(
        std::coroutine_handle<boost_asio_standalone_init_promise_type> h)
      {
        auto h_ = std::move(handler);
        auto args_ = std::move(args);
        asio_executor_adapter ex_ = std::move(ex);
        h.destroy();

        auto handler =         
            std::apply( 
              [&](auto ... args) 
              {
                return ::asio::append(std::move(h_), std::move(args)...);
              },
              args_);

        asio_executor_adapter aex(ex);
        auto exec = ::asio::get_associated_immediate_executor(handler, ex_);
        ::asio::dispatch(exec, std::move(handler));                  
      }
      void await_resume() const {}
    };

    completer yield_value(args_type value)
    {
      return {std::move(handler), std::move(ex), std::move(value)};
    }
        
    struct wrapper
    {
      Runnable r;
      const Ex &ex;
      io_env env;
      std::stop_source stop_src;
      ::asio::cancellation_slot cancel_slot;

      continuation c;

      bool await_ready() {return r.await_ready(); }

      std::coroutine_handle<> await_suspend(
          std::coroutine_handle<boost_asio_standalone_init_promise_type> tr)
      {
        // always post in
        auto h = r.handle();
        auto & p = h.promise();
        p.set_continuation(tr);
        env.executor = ex;

        env.stop_token = stop_src.get_token();
        cancel_slot = 
            ::asio::get_associated_cancellation_slot(tr.promise().handler);
        if (cancel_slot.is_connected())
          cancel_slot.assign(
              [this](::asio::cancellation_type ct)
              {
                if ((ct & ::asio::cancellation_type::terminal)
                  != ::asio::cancellation_type::none)
                  stop_src.request_stop(); 
              });
        env.frame_allocator = get_current_frame_allocator();


        p.set_environment(&env);
        c.h = h;
        return ex.dispatch(c);
      }
      
      completion_tuple_for_io_runnable<Runnable> await_resume()
      {
        if (cancel_slot.is_connected())
          cancel_slot.clear();
          
        using type = decltype(r.await_resume());
        if constexpr (!noexcept(r.await_resume()))
        {
          if constexpr (std::is_void_v<type>)
            try 
            {
              r.await_resume();
              return {std::exception_ptr()};
            }
            catch (...)
            {
              return std::current_exception();
            }
          else
            try 
            {
              return {std::exception_ptr(), r.await_resume()};
            }
            catch (...)
            {
              return {std::current_exception(), type()};
            }
        }
        else
        {
          if constexpr (std::is_void_v<type>)
          {
            r.await_resume();
            return {};
          }
          else
            return {r.await_resume()};
        }
      }
    };

    wrapper await_transform(Runnable & r)
    {
      return wrapper{std::move(r), ex};
    }
};


    
struct boost_asio_standalone_init
{
  template<typename Handler, Executor Ex, IoRunnable Runnable> 
  void operator()(
                  Handler h, 
                  Ex executor,
                  Runnable runnable)
  {
    auto res = co_await runnable;
    co_yield std::move(res);
  }
};

template<typename Runnable, typename Token> 
  requires
    ::asio::completion_token_for<
          Token, 
          completion_signature_for_io_runnable<Runnable>
        >
struct initialize_asio_standalone_spawn_helper<Runnable, Token>
{
  template<typename Executor>
  static auto init(Executor ex, Runnable r, Token && tk)
  {
    return ::asio::async_initiate<
        Token, 
        completion_signature_for_io_runnable<Runnable>>(
          boost_asio_standalone_init{},
          tk, std::move(ex), std::move(r)
          );
  }
};

}


template<typename Handler, typename Executor, typename Runnable>
struct std::coroutine_traits<
              void, 
              boost::capy::detail::boost_asio_standalone_init&, 
              Handler, 
              Executor, 
              Runnable>
{
  using promise_type 
          = boost::capy::detail::boost_asio_standalone_init_promise_type<
                    Handler, 
                    Executor, 
                    Runnable>;
}; 

#endif // BOOST_CAPY_ASIO_STANDALONE_HPP

