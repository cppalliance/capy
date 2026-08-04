//
// Copyright (c) 2026 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//


#ifndef BOOST_CAPY_ASIO_DETAIL_ASIO_COROUTINE_UNIQUE_HANDLE
#define BOOST_CAPY_ASIO_DETAIL_ASIO_COROUTINE_UNIQUE_HANDLE

#include <coroutine>
#include <memory>

namespace boost::capy::detail 
{

struct asio_coroutine_unique_handle
{
  struct deleter 
  {
    deleter() = default;
    void operator()(void * h) const
    {
      std::coroutine_handle<void>::from_address(h).destroy();
    }
  };
  std::unique_ptr<void, deleter> handle;

  asio_coroutine_unique_handle(
    std::coroutine_handle<void> h) : handle(h.address()) {}

  asio_coroutine_unique_handle(
      asio_coroutine_unique_handle &&
      ) noexcept = default;
      
  void operator()()
  {    
    release().resume();
  }

  std::coroutine_handle<> release()
  {
    return std::coroutine_handle<void>::from_address(
        handle.release()
        );
  }
};

}

#endif 

