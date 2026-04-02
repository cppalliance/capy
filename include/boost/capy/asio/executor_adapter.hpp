//
// Copyright (c) 2026 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_ASIO_EXECUTOR_ADAPTER_HPP
#define BOOST_CAPY_ASIO_EXECUTOR_ADAPTER_HPP

#include <boost/capy/ex/any_executor.hpp>
#include <boost/capy/ex/execution_context.hpp>

#include <boost/asio/execution_context.hpp>
#include <boost/asio/execution/allocator.hpp>
#include <boost/asio/execution/blocking.hpp>
#include <boost/asio/execution/context.hpp>
#include <boost/asio/execution/outstanding_work.hpp>
#include <boost/asio/execution/relationship.hpp>
#include <cstddef>

namespace boost {
namespace capy {
namespace detail
{

struct asio_adapter_context_service 
    : execution_context::service, 
      // shutdown is protected
      boost::asio::execution_context
{
    asio_adapter_context_service(boost::capy::execution_context & ctx) {}
    void shutdown() override {boost::asio::execution_context::shutdown();}
};

}


template<typename Executor     = capy::any_executor,
         typename Allocator    = std::pmr::polymorphic_allocator<void>, 
         typename Blocking     = boost::asio::execution::blocking_t::possibly_t,
         typename Outstanding  = boost::asio::execution::outstanding_work_t::untracked_t>
struct asio_executor_adapter
{
  template<typename Blocking_, typename Outstanding_>
  asio_executor_adapter(const asio_executor_adapter<Executor, Allocator, Blocking_,  Outstanding_> & rhs) 
      noexcept(std::is_nothrow_copy_constructible_v<Executor>)
      : executor_(rhs.executor_), allocator_(rhs.allocator_)
  {  
    if constexpr(Outstanding() == boost::asio::execution::outstanding_work.tracked)
      executor_.on_work_started();
  }
  
  template<typename Blocking_, typename Outstanding_>
  asio_executor_adapter(asio_executor_adapter<Executor, Allocator, Blocking_,  Outstanding_> && rhs) 
    noexcept(std::is_nothrow_move_constructible_v<Executor>)
      : executor_(std::move(rhs.executor_)), allocator_(std::move(rhs.allocator_))
  {
    if constexpr(Outstanding() == boost::asio::execution::outstanding_work.tracked)
      executor_.on_work_started();
  }
  
  asio_executor_adapter(Executor executor, const Allocator & alloc) 
        noexcept(std::is_nothrow_move_constructible_v<Executor>)
      : executor_(std::move(executor)), allocator_(alloc)
  {
    if constexpr(Outstanding() == boost::asio::execution::outstanding_work.tracked)
        executor_.on_work_started();
  }
  
  asio_executor_adapter(Executor executor) noexcept(std::is_nothrow_move_constructible_v<Executor>)
        : executor_(std::move(executor)), allocator_(executor_.context().get_frame_allocator())
  {
      if constexpr(Outstanding() == boost::asio::execution::outstanding_work.tracked)
        executor_.on_work_started();
  }

  ~asio_executor_adapter()
  {
    if constexpr(Outstanding() == boost::asio::execution::outstanding_work.tracked)
          executor_.on_work_finished();
  }

  template<typename Blocking_, typename Outstanding_>
  asio_executor_adapter & operator=(const asio_executor_adapter<Executor, Allocator, Blocking_,  Outstanding_> & rhs) 
  {
    
    if constexpr (Outstanding_() == boost::asio::execution::outstanding_work.tracked)
      if (rhs.executor_ != executor_)
      {
        rhs.executor_.on_work_started();
        executor_.on_work_finished();
      }

    executor_ = rhs.executor_;
    allocator_ = rhs.allocator_;
  }

  bool operator==(const asio_executor_adapter & rhs) const noexcept 
  { 
    return executor_  == rhs.executor_
        && allocator_ == rhs.allocator_;
  }
  bool operator!=(const asio_executor_adapter & rhs) const noexcept 
  {
    return executor_  != rhs.executor_
        && allocator_ != rhs.allocator_;
  }

  boost::asio::execution_context& 
      query(boost::asio::execution::context_t) const noexcept
  {
    return context();
  }

  constexpr boost::asio::execution::blocking_t 
      query(boost::asio::execution::blocking_t) const noexcept
  {
    return Blocking();
  }

  constexpr asio_executor_adapter<Executor, Allocator, boost::asio::execution::blocking_t::possibly_t, Outstanding>
      require(boost::asio::execution::blocking_t::possibly_t) const
  {
    return *this;
  }

  constexpr asio_executor_adapter<Executor, Allocator, boost::asio::execution::blocking_t::never_t, Outstanding>
      require(boost::asio::execution::blocking_t::never_t) const
  {
    return *this;
  }
  
  constexpr asio_executor_adapter<Executor, Allocator, boost::asio::execution::blocking_t::always_t, Outstanding>
      require(boost::asio::execution::blocking_t::always_t) const
  {
    return *this;
  }
  
  static constexpr boost::asio::execution::outstanding_work_t query(
      boost::asio::execution::outstanding_work_t) noexcept
  {
    return Outstanding();
  }

  constexpr asio_executor_adapter<Executor, Allocator, Blocking, 
      boost::asio::execution::outstanding_work_t::tracked_t>
      require(boost::asio::execution::outstanding_work_t::tracked_t) const
  {
    return *this;
  }
  
  constexpr asio_executor_adapter<Executor, Allocator, Blocking, 
      boost::asio::execution::outstanding_work_t::untracked_t>
      require(boost::asio::execution::outstanding_work_t::untracked_t) const
  {
    return *this;
  }
  

  template <typename OtherAllocator>
  constexpr Allocator query(
      boost::asio::execution::allocator_t<OtherAllocator>) const noexcept
  {
    return allocator_;
  }
  template <typename OtherAllocator>
  constexpr asio_executor_adapter<Executor, OtherAllocator, Blocking, Outstanding>
      require(boost::asio::execution::allocator_t<OtherAllocator> a) const
  {
    return asio_executor_adapter<Executor, OtherAllocator, Blocking, Outstanding>(
        executor_, a.value()
      );
  }

  boost::asio::execution_context & context() const noexcept 
  {
    return executor_.context().template use_service<detail::asio_adapter_context_service>();
  }

  template <typename Function>
  void execute(Function&& f) const
  {
    constexpr static boost::asio::execution::blocking_t b;
    
    if constexpr (Blocking() == b.never)
      executor_.post(make_handle_(std::forward<Function>(f)).cont);
    else if constexpr(Blocking() == b.possibly)
      executor_.dispatch(make_handle_(std::forward<Function>(f)).cont).resume();
    else if constexpr(Blocking() == b.always)
      std::forward<Function>(f)();    
  }


 private:  

  struct handler_promise_base_empty_ {};
  struct handler_promise_base_
  {
    using alloc_t = std::allocator_traits<Allocator>::template rebind_alloc<char>;
    template<typename Func>
    void * operator new(std::size_t n, const asio_executor_adapter & adapter, Func &)
    {
      alloc_t alloc(adapter.allocator_);
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

  struct handler_promise_ : std::conditional_t<
    std::same_as<Allocator, std::allocator<void>>,
    handler_promise_base_empty_,
    handler_promise_base_>
  {
    std::suspend_always initial_suspend() const noexcept  {return {};}
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
          std::move(f)();
        }
        void await_resume() {}
      };

      return yielder{std::move(func)};
    }

    continuation cont;

    void unhandled_exception() { throw; }
    continuation &  get_return_object() 
    {
      cont.h = std::coroutine_handle<handler_promise_>::from_promise(*this);
      cont.next = nullptr;
      return cont;
    }
  };

  struct helper_
  {
    capy::continuation &cont;
    helper_(continuation & cont) noexcept : cont(cont) {}
    using promise_type = handler_promise_;
  };
  
  template<typename Function>
  helper_ make_handle_(Function func) const
  {
    co_yield func;
  }
 
  template<typename, typename, typename, typename>
  friend struct asio_executor_adapter;
  Executor executor_;
  [[no_unique_address]] Allocator allocator_;
  
};




}
}


#endif //BOOST_CAPY_ASIO_EXECUTOR_ADAPTER_HPP
