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

/** @addtogroup asio
 *  @{
 */

namespace detail
{

/** @brief Service that bridges capy's execution_context with Asio's.
 *  @internal
 *
 *  This service inherits from both capy's service and the target Asio
 *  execution context, allowing capy executors wrapped in `asio_executor_adapter`
 *  to be queried for their Asio execution context.
 *
 *  @tparam ExecutionContext The Asio execution_context type (boost::asio or standalone)
 */
template<typename ExecutionContext>
struct asio_adapter_context_service
    : execution_context::service,
      // shutdown is protected
      ExecutionContext
{
    asio_adapter_context_service(boost::capy::execution_context &) {}
    void shutdown() override {ExecutionContext::shutdown();}
};

}


/** @brief Adapts a capy executor to be usable with Asio.
 *
 *  `asio_executor_adapter` wraps a capy executor and exposes it as an
 *  Asio-compatible executor. This allows capy coroutines and executors to
 *  interoperate seamlessly with Asio's async operations.
 *
 *  The adapter tracks execution properties (blocking behavior and work tracking)
 *  as compile-time template parameters for zero-overhead property queries.
 *
 *  @tparam Executor The underlying capy executor type (default: `capy::any_executor`)
 *  @tparam Allocator The allocator type for handler allocation
 *                    (default: `std::pmr::polymorphic_allocator<void>`)
 *  @tparam Bits Compile-time bitfield encoding blocking and work-tracking properties
 *
 *  @par Execution Properties
 *  The adapter supports the standard Asio execution properties:
 *  - `blocking`: `possibly` (default), `never`, or `always`
 *  - `outstanding_work`: `untracked` (default) or `tracked`
 *  - `allocator`: Custom allocator for handler allocation
 *  - `context`: Returns the associated capy execution_context
 *
 *  @par Example
 *  @code
 *  // Wrap a capy executor for use with Asio
 *  capy::any_executor capy_exec = ...;
 *  asio_executor_adapter<> asio_exec(capy_exec);
 *
 *  // Use with Asio operations
 *  asio::post(asio_exec, []{ std::cout << "Hello from capy!\n"; });
 *
 *  // Require non-blocking execution
 *  auto never_blocking = asio::require(asio_exec,
 *      asio::execution::blocking.never);
 *  @endcode
 *
 *  @see wrap_asio_executor For the reverse direction (Asio to capy)
 */
template<typename Executor     = capy::any_executor,
         typename Allocator    = std::pmr::polymorphic_allocator<void>,
         int Bits = 0>
struct asio_executor_adapter
{
  /// @name Blocking Property Constants
  /// @{
  constexpr static int blocking_possibly  = 0b000;  ///< May block the caller
  constexpr static int blocking_never     = 0b001;  ///< Never blocks the caller
  constexpr static int blocking_always    = 0b010;  ///< Always blocks until complete
  constexpr static int blocking_mask      = 0b011;  ///< Mask for blocking bits
  /// @}

  /// @name Work Tracking Property Constants
  /// @{
  constexpr static int work_untracked     = 0b000;  ///< Work is not tracked
  constexpr static int work_tracked       = 0b100;  ///< Outstanding work is tracked
  constexpr static int work_mask          = 0b100;  ///< Mask for work tracking bit
  /// @}
  

  /// @name Constructors
  /// @{

  /** @brief Copy constructor from adapter with different property bits.
   *
   *  Creates a copy with potentially different execution properties.
   *  If this adapter tracks work, `on_work_started()` is called.
   *
   *  @tparam Bits_ The source adapter's property bits
   *  @param rhs The source adapter to copy from
   */
  template<int Bits_>
  asio_executor_adapter(
      const asio_executor_adapter<Executor, Allocator, Bits_> & rhs)
        noexcept(std::is_nothrow_copy_constructible_v<Executor>)
        : executor_(rhs.executor_), allocator_(rhs.allocator_)
  {
    if constexpr((Bits & work_mask) == work_tracked)
      executor_.on_work_started();
  }

  /** @brief Move constructor from adapter with different property bits.
   *
   *  Moves from another adapter with potentially different properties.
   *  If this adapter tracks work, `on_work_started()` is called.
   *
   *  @tparam Bits_ The source adapter's property bits
   *  @param rhs The source adapter to move from
   */
  template<int Bits_>
  asio_executor_adapter(
      asio_executor_adapter<Executor, Allocator, Bits_> && rhs)
        noexcept(std::is_nothrow_move_constructible_v<Executor>)
        : executor_(std::move(rhs.executor_))
        , allocator_(std::move(rhs.allocator_))
  {
    if constexpr((Bits & work_mask) == work_tracked)
      executor_.on_work_started();
  }

  /** @brief Constructs from executor and allocator.
   *
   *  @param executor The capy executor to wrap
   *  @param alloc The allocator for handler allocation
   */
  asio_executor_adapter(Executor executor, const Allocator & alloc)
        noexcept(std::is_nothrow_move_constructible_v<Executor>
             && std::is_nothrow_copy_constructible_v<Allocator>)
      : executor_(std::move(executor)), allocator_(alloc)
  {
    if constexpr((Bits & work_mask) == work_tracked)
      executor_.on_work_started();
  }

  /** @brief Constructs from adapter with different allocator.
   *
   *  @tparam OtherAllocator The source adapter's allocator type
   *  @param executor The source adapter
   *  @param alloc The new allocator to use
   */
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


  /** @brief Constructs from a capy executor.
   *
   *  The allocator is obtained from the executor's context frame allocator.
   *
   *  @param executor The capy executor to wrap
   */
  asio_executor_adapter(Executor executor)
        noexcept(std::is_nothrow_move_constructible_v<Executor>)
        : executor_(std::move(executor))
        , allocator_(executor_.context().get_frame_allocator())
  {
    if constexpr((Bits & work_mask) == work_tracked)
      executor_.on_work_started();
  }

  /** @brief Destructor.
   *
   *  If work tracking is enabled, calls `on_work_finished()`.
   */
  ~asio_executor_adapter()
  {
    if constexpr((Bits & work_mask) == work_tracked)
      executor_.on_work_finished();
  }

  /// @}

  /// @name Assignment
  /// @{

  /** @brief Copy assignment from adapter with different property bits.
   *
   *  Properly handles work tracking when changing executors.
   *
   *  @tparam Bits_ The source adapter's property bits
   *  @param rhs The source adapter
   *  @return Reference to this adapter
   */
  template<int Bits_>
  asio_executor_adapter & operator=(
      const asio_executor_adapter<Executor, Allocator, Bits_> & rhs)
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

  /// @}

  /// @name Comparison
  /// @{

  /** @brief Equality comparison.
   *  @param rhs The adapter to compare with
   *  @return `true` if both executor and allocator are equal
   */
  bool operator==(const asio_executor_adapter & rhs) const noexcept
  {
    return executor_  == rhs.executor_
        && allocator_ == rhs.allocator_;
  }

  /** @brief Inequality comparison.
   *  @param rhs The adapter to compare with
   *  @return `true` if executor or allocator differs
   */
  bool operator!=(const asio_executor_adapter & rhs) const noexcept
  {
    return executor_  != rhs.executor_
        && allocator_ != rhs.allocator_;
  }

  /// @}

  /// @name Execution
  /// @{

  /** @brief Executes a function according to the blocking property.
   *
   *  The execution behavior depends on the `Bits` template parameter:
   *  - `blocking_never`: Posts the function for deferred execution
   *  - `blocking_possibly`: Dispatches (may run inline or post)
   *  - `blocking_always`: Executes the function inline immediately
   *
   *  @tparam Function The callable type
   *  @param f The function to execute
   */
  template <typename Function>
  void execute(Function&& f) const
  {
    if constexpr ((Bits & blocking_mask)  == blocking_never)
      executor_.post(
          detail::make_continuation(std::forward<Function>(f), allocator_));
    else if constexpr((Bits & blocking_mask) == blocking_possibly)
      executor_.dispatch(
          detail::make_continuation(std::forward<Function>(f), allocator_)
        ).resume();
    else if constexpr((Bits & blocking_mask) == blocking_always)
      std::forward<Function>(f)();
  }

  /// @}

  /// @name Accessors
  /// @{

  /** @brief Returns the associated execution context.
   *  @return Reference to the capy execution_context
   */
  execution_context & context() const {return executor_.context(); }

  /** @brief Returns the associated allocator.
   *  @return Copy of the allocator
   */
  Allocator get_allocator() const noexcept {return allocator_;}

  /** @brief Returns the underlying capy executor.
   *  @return Copy of the wrapped executor
   */
  const Executor get_capy_executor() const {return executor_;}

  /// @} 
 private:  

  template<typename, typename, int>
  friend struct asio_executor_adapter;
  Executor executor_;
#if __has_cpp_attribute(no_unique_address)
  [[no_unique_address]] 
#endif
  Allocator allocator_;

};

/** @} */ // end of asio group

}
}


#endif //BOOST_CAPY_ASIO_EXECUTOR_ADAPTER_HPP

