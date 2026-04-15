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

#include <boost/capy/asio/detail/continuation.hpp>
#include <boost/capy/ex/any_executor.hpp>
#include <boost/capy/ex/execution_context.hpp>
#include <memory_resource>


namespace boost {
namespace capy {
namespace detail
{

template<typename ExecutionContext>
struct asio_adapter_context_service 
    : execution_context::service, 
      // shutdown is protected
      ExecutionContext
{
    asio_adapter_context_service(boost::capy::execution_context & ctx) {}
    void shutdown() override {ExecutionContext::shutdown();}
};

}


template<typename Executor     = capy::any_executor,
         typename Allocator    = std::pmr::polymorphic_allocator<void>, 
         int Bits = 0>
struct asio_executor_adapter
{
  constexpr static int blocking_possibly  = 0b000;
  constexpr static int blocking_never     = 0b001;
  constexpr static int blocking_always    = 0b010;
  constexpr static int blocking_mask      = 0b011;
  constexpr static int work_untracked     = 0b000;
  constexpr static int work_tracked       = 0b100;
  constexpr static int work_mask          = 0b100;
  

  template<int Bits_>
  asio_executor_adapter(const asio_executor_adapter<Executor, Allocator, Bits_> & rhs) 
      noexcept(std::is_nothrow_copy_constructible_v<Executor>)
      : executor_(rhs.executor_), allocator_(rhs.allocator_)
  {  
    if constexpr((Bits & work_mask) == work_tracked)
      executor_.on_work_started();
  }
  
  template<int Bits_>
  asio_executor_adapter(asio_executor_adapter<Executor, Allocator, Bits_> && rhs) 
    noexcept(std::is_nothrow_move_constructible_v<Executor>)
      : executor_(std::move(rhs.executor_)), allocator_(std::move(rhs.allocator_))
  {
    if constexpr((Bits & work_mask) == work_tracked)
      executor_.on_work_started();
  }
  
  asio_executor_adapter(Executor executor, const Allocator & alloc) 
        noexcept(std::is_nothrow_move_constructible_v<Executor> 
             && std::is_nothrow_copy_constructible_v<Allocator>)
      : executor_(std::move(executor)), allocator_(alloc)
  {
    if constexpr((Bits & work_mask) == work_tracked)
      executor_.on_work_started();
  }

  template<typename OtherAllocator>
  explicit asio_executor_adapter(
            asio_executor_adapter<Executor, OtherAllocator, Bits> executor, 
            const Allocator & alloc) 
        noexcept(std::is_nothrow_move_constructible_v<Executor> && 
                 std::is_nothrow_copy_constructible_v<Allocator>)
      : executor_(std::move(executor.executor_)), allocator_(alloc)
  {
    if constexpr((Bits & work_mask) == work_tracked)
      executor_.on_work_started();
  }
  
  
  asio_executor_adapter(Executor executor) noexcept(std::is_nothrow_move_constructible_v<Executor>)
        : executor_(std::move(executor)), allocator_(executor_.context().get_frame_allocator())
  {
    if constexpr((Bits & work_mask) == work_tracked)
      executor_.on_work_started();
  }

  ~asio_executor_adapter()
  {
    if constexpr((Bits & work_mask) == work_tracked)
      executor_.on_work_finished();
  }

  template<int Bits_>
  asio_executor_adapter & operator=(const asio_executor_adapter<Executor, Allocator, Bits_> & rhs) 
  {
    
    if constexpr((Bits & work_mask) == work_tracked)        
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

  template <typename Function>
  void execute(Function&& f) const
  {
    if constexpr ((Bits & blocking_mask)  == blocking_never)
      executor_.post(detail::make_continuation(std::forward<Function>(f), allocator_));
    else if constexpr((Bits & blocking_mask) == blocking_possibly)
      executor_.dispatch(detail::make_continuation(std::forward<Function>(f), allocator_)).resume();
    else if constexpr((Bits & blocking_mask) == blocking_always)
      std::forward<Function>(f)();    
  }

  execution_context & context() const {return executor_.context(); }
  Allocator get_allocator() const noexcept {return allocator_;}

  const Executor get_capy_executor() const {return executor_;} 
 private:  

  template<typename, typename, int>
  friend struct asio_executor_adapter;
  Executor executor_;
  [[no_unique_address]] Allocator allocator_;
  
};




}
}


#endif //BOOST_CAPY_ASIO_EXECUTOR_ADAPTER_HPP
