//
// Copyright (c) 2026 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_ASIO_SPAWN_HPP
#define BOOST_CAPY_ASIO_SPAWN_HPP


#include <boost/capy/asio/executor_adapter.hpp>
#include <boost/capy/asio/detail/completion_traits.hpp>
#include <boost/capy/ex/frame_allocator.hpp>
#include <boost/capy/concept/execution_context.hpp>
#include <boost/capy/concept/executor.hpp>
#include <boost/capy/concept/io_runnable.hpp>


#include <boost/asio/associated_allocator.hpp>
#include <boost/asio/associated_cancellation_slot.hpp>
#include <boost/asio/associated_immediate_executor.hpp>
#include <boost/asio/append.hpp>
#include <boost/asio/default_completion_token.hpp>
#include <boost/asio/dispatch.hpp>


namespace boost::capy
{

namespace detail
{

struct boost_asio_init;



template<typename Allocator>
struct boost_asio_promise_type_allocator_base
{
  template<typename Handler, Executor Ex, IoRunnable Runnable>
  void * operator new   (std::size_t n, boost_asio_init &, 
                         Handler & handler, 
                         Ex & exec, Runnable & r)
  {
    using allocator_type = std::allocator_traits<Allocator>::template rebind_alloc<char>;
    allocator_type allocator(boost::asio::get_associated_allocator(handler));
    using traits = std::allocator_traits<allocator_type>; //::rebind_alloc<char>()

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
  
    using allocator_type = std::allocator_traits<Allocator>::template rebind_alloc<char>;
    auto allocator_p = reinterpret_cast<allocator_type*>(static_cast<char*>(ptr) + n);
    auto allocator = std::move(allocator_p);

    allocator_p->~allocator_type();
    allocator.deallocate(ptr, n + sizeof(allocator_type));
  }
};


template<>
struct boost_asio_promise_type_allocator_base<std::allocator<void>>
{
};

template<typename Handler, Executor Ex, IoRunnable Runnable>
struct boost_asio_init_promise_type 
  : boost_asio_promise_type_allocator_base<boost::asio::associated_allocator_t<Handler>>
{
    using args_type = completion_tuple_for_io_runnable<Runnable>;
  
    boost_asio_init_promise_type(boost_asio_init &, Handler & h, Ex & exec, Runnable & r)  
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
      Handler &handler;
      asio_executor_adapter<Ex> ex;
      args_type args;
      
      bool await_ready() const {return false;}
      void await_suspend(std::coroutine_handle<boost_asio_init_promise_type> h)
      {
        auto h_ = std::move(handler);
        auto args_ = std::move(args);
        h.destroy();

        auto handler =         
            std::apply( 
              [&](auto ... args) {return boost::asio::append(std::move(h_), std::move(args)...);},
              args_);
        
        auto exec = boost::asio::get_associated_immediate_executor(handler, ex);
        boost::asio::dispatch(exec, std::move(handler));                  
      }
      void await_resume() const {}
    };

    completer yield_value(args_type value)
    {
      return {handler, ex, std::move(value)};
    }

    struct wrapper
    {
      Runnable r;
      Ex ex;
      io_env env;
      std::stop_source stop_src;
      boost::asio::cancellation_slot cancel_slot;

      continuation c;

      bool await_ready() {return r.await_ready(); }

      std::coroutine_handle<> await_suspend(std::coroutine_handle<boost_asio_init_promise_type> tr)
      {
        // always post in
        auto h = r.handle();
        auto & p = h.promise();
        p.set_continuation(tr);
        env.executor = ex;

        env.stop_token = stop_src.get_token();
        cancel_slot = boost::asio::get_associated_cancellation_slot(tr.promise().handler);
        if (cancel_slot.is_connected())
          cancel_slot.assign(
              [this](boost::asio::cancellation_type ct)
              {
                if ((ct & boost::asio::cancellation_type::terminal)
                  != boost::asio::cancellation_type::none)
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
        if constexpr (noexcept(r.await_resume()))
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
              return {r.await_resume(), std::exception_ptr()};
            }
            catch (...)
            {
              return {type(), std::current_exception()};
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
      return wrapper{std::move(r), std::move(ex)};
    }

};

struct boost_asio_init
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

}

template<Executor ExecutorType, IoRunnable Runnable,
         boost::asio::completion_token_for<detail::completion_signature_for_io_runnable<Runnable>> Token
            = boost::asio::default_completion_token_t<ExecutorType>>
auto asio_spawn(ExecutorType exec, Runnable && runnable, Token token)
{
  return boost::asio::async_initiate<Token, detail::completion_signature_for_io_runnable<Runnable>>(
            detail::boost_asio_init{},
            token, std::move(exec), std::move(runnable));  
}


template<ExecutionContext Context, IoRunnable Runnable,
         boost::asio::completion_token_for<detail::completion_signature_for_io_runnable<Runnable>> Token 
          = boost::asio::default_completion_token_t<typename Context::executor_type>>
auto asio_spawn(Context & ctx, Runnable && runnable, Token token)
{

    return boost::asio::async_initiate<Token, detail::completion_signature_for_io_runnable<Runnable>>(
            detail::boost_asio_init{},
            token, ctx.get_executor(), std::move(runnable));
}


}

template<typename Handler, typename Executor, typename Runnable>
struct std::coroutine_traits<void, boost::capy::detail::boost_asio_init&, Handler, Executor, Runnable>
{
  using promise_type = boost::capy::detail::boost_asio_init_promise_type<Handler, Executor, Runnable>;
};

#endif
