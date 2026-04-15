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


namespace boost::capy
{

namespace detail
{

template<typename Runnable, typename Token> 
struct initialize_asio_spawn_helper;


template<typename Token, typename Executor, typename Runnable>
concept asio_spawn_token = 
  requires (Token && tk, Executor ex, Runnable rn)
  {
    initialize_asio_spawn_helper<Runnable, Token>::
        init(std::move(ex), std::move(rn), std::forward<Token>(tk));
  };

template<typename Runnable, typename Token> 
struct initialize_asio_standalone_spawn_helper;

template<typename Token, typename Executor, typename Runnable>
concept asio_standalone_spawn_token = 
  requires (Token && tk, Executor ex, Runnable rn)
  {
    initialize_asio_standalone_spawn_helper<Runnable, Token>::
        init(std::move(ex), std::move(rn), std::forward<Token>(tk));
  };


}

template<Executor Executor, IoRunnable Runnable>
struct asio_spawn_op
{
  asio_spawn_op(Executor executor, Runnable runnable) 
    : executor_(std::move(executor)), runnable_(std::move(runnable)) 
  {}

  template<detail::asio_spawn_token<Executor, Runnable> Token>
  auto operator()(Token && token)
  {
    return detail::initialize_asio_spawn_helper<Runnable, Token>::init(
            std::move(executor_), 
            std::move(runnable_), 
            std::forward<Token>(token)
          );
  }
  
  template<detail::asio_standalone_spawn_token<Executor, Runnable> Token>
  auto operator()(Token && token)
  {
    return detail::initialize_asio_standalone_spawn_helper<Runnable, Token>::init(
            std::move(executor_), 
            std::move(runnable_), 
            std::forward<Token>(token)
          );
  }
  
 private:
  Executor executor_;
  Runnable runnable_;   
};


template<Executor ExecutorType, IoRunnable Runnable>
auto asio_spawn(ExecutorType exec, Runnable && runnable)
{
  return asio_spawn_op(std::move(exec), std::forward<Runnable>(runnable));
}

template<ExecutionContext Context, IoRunnable Runnable>
auto asio_spawn(Context & ctx, Runnable && runnable)
{
  return asio_spawn_op(ctx.get_executor(), std::forward<Runnable>(runnable));
}

}

#endif
