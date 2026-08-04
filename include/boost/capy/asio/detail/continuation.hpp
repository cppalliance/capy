//
// Copyright (c) 2026 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_ASIO_CONTINUATION_HPP
#define BOOST_CAPY_ASIO_CONTINUATION_HPP

#include <boost/capy/continuation.hpp>
#include <boost/capy/concept/executor.hpp>

#include <memory>

namespace boost::capy
{

namespace detail
{

template<typename Allocator>
struct continuation_handle_promise_base_
{
  using alloc_t = std::allocator_traits<Allocator>
                      ::template rebind_alloc<char>;
                      
  template<typename Func>
  void * operator new(std::size_t n, Func &, Allocator &allocator_)
  {
    alloc_t alloc(allocator_);
    std::size_t m = n;
    if (n % alignof(alloc_t) > 0)
      m += alignof(alloc_t) - (n % alignof(alloc_t));

    char * mem = alloc.allocate(m + sizeof(alloc));

    new (mem + m) alloc_t(std::move(alloc));
    return mem;
  }

  void operator delete(void * p, std::size_t n)
  {
    std::size_t m = n;
    if (n % alignof(alloc_t) > 0)
      m += alignof(alloc_t) - (n % alignof(alloc_t));

    auto * a = reinterpret_cast<alloc_t*>(static_cast<char*>(p) + m);

    alloc_t alloc(std::move(*a));
    a->~alloc_t();

    alloc.deallocate(static_cast<char*>(p), n);
  }
};


template<>
struct continuation_handle_promise_base_<std::allocator<void>>
{
};

template<typename Allocator>
struct continuation_handle_promise_type 
    : continuation_handle_promise_base_<Allocator>
{

  struct initial_aw_t
  {
    bool await_ready() const {return false;}
    void await_suspend(
      std::coroutine_handle<continuation_handle_promise_type<Allocator>> h)
    {
      auto & c = h.promise().cont;
      c.h = h;
      c.next = nullptr;
    }
    void await_resume() {}
  };

  initial_aw_t initial_suspend() const noexcept  
  {
    return initial_aw_t{};
  }
  std::suspend_never    final_suspend() const noexcept  {return {};}

  template<typename Function>
  auto yield_value(Function & func)
  {
    struct yielder
    {
      Function func;

      bool await_ready() const {return false;}
      void await_suspend(std::coroutine_handle<> h)
      {
        auto f = std::move(func);
        h.destroy();
#if defined(__GNUC__) && !defined(__clang__) && (__GNUC__ <= 16)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Warray-bounds"
#endif
        std::move(f)();
#if defined(__GNUC__) && !defined(__clang__) && (__GNUC__ <= 16)
#pragma GCC diagnostic pop
#endif
        
      }
      void await_resume() {}
    };

    return yielder{std::move(func)};
  }

  void unhandled_exception() { throw; }
  void return_void() {}
  
  continuation cont;

  struct helper 
  {
    continuation * cont;
    using promise_type = continuation_handle_promise_type;
  };
  
  helper get_return_object() 
  {
    return helper{&cont};
  }
};


template<std::invocable<> Function, typename Allocator>
auto make_continuation_helper(
    Function func, 
    Allocator)
    -> continuation_handle_promise_type<Allocator>::helper
{
  co_yield func;
}

template<std::invocable<> Function, typename Allocator>
continuation & make_continuation(
    Function && func, 
    Allocator && alloc)
{
  continuation * c = detail::make_continuation_helper(
          std::forward<Function>(func), 
          std::forward<Allocator>(alloc)).cont;
  return *c;
}

}


}

#endif 

