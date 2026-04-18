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
#include <boost/capy/concept/io_awaitable.hpp>


namespace boost::capy
{

/** @addtogroup asio
 *  @{
 */

namespace detail
{

/** @brief Helper for initializing spawned coroutines with Boost.Asio tokens.
 *  @internal
 *
 *  Specialized in `boost.hpp` for actual Boost.Asio completion tokens.
 */
template<typename Awaitable, typename Token>
struct initialize_asio_spawn_helper;

/** @brief Concept for valid Boost.Asio spawn completion tokens.
 *  @internal
 *
 *  A token satisfies this concept if `initialize_asio_spawn_helper` can
 *  initialize the spawn operation with it.
 */
template<typename Token, typename Executor, typename Awaitable>
concept asio_spawn_token =
  requires (Token && tk, Executor ex, Awaitable rn)
  {
    initialize_asio_spawn_helper<Awaitable, Token>::
        init(std::move(ex), std::move(rn), std::forward<Token>(tk));
  };

/** @brief Helper for initializing spawned coroutines with standalone Asio tokens.
 *  @internal
 *
 *  Specialized in `standalone.hpp` for actual standalone Asio completion tokens.
 */
template<typename Awaitable, typename Token>
struct initialize_asio_standalone_spawn_helper;

/** @brief Concept for valid standalone Asio spawn completion tokens.
 *  @internal
 *
 *  A token satisfies this concept if `initialize_asio_standalone_spawn_helper`
 *  can initialize the spawn operation with it.
 */
template<typename Token, typename Executor, typename Awaitable>
concept asio_standalone_spawn_token =
  requires (Token && tk, Executor ex, Awaitable rn)
  {
    initialize_asio_standalone_spawn_helper<Awaitable, Token>::
        init(std::move(ex), std::move(rn), std::forward<Token>(tk));
  };


}

/** @brief Deferred spawn operation that can be initiated with a completion token.
 *
 *  This class represents a spawn operation that has captured an executor and
 *  awaitable but hasn't been initiated yet. Call `operator()` with a completion
 *  token to start the operation.
 *
 *  @tparam Executor The executor type for running the coroutine
 *  @tparam Awaitable The coroutine type (must satisfy `IoAwaitable`)
 *
 *  @par Example
 *  @code
 *  auto op = asio_spawn(executor, my_coroutine());
 *
 *  // Initiate with different tokens:
 *  op(asio::detached);                    // Fire and forget
 *  op([](std::exception_ptr) { ... });    // Callback on completion
 *  co_await op(asio::use_awaitable);      // Await in another coroutine
 *  @endcode
 *
 *  @see asio_spawn Factory function to create spawn operations
 */
template<Executor Executor, IoAwaitable Awaitable>
struct asio_spawn_op
{
  /** @brief Constructs the spawn operation.
   *  @param executor The executor to run on
   *  @param awaitable The coroutine to spawn
   */
  asio_spawn_op(Executor executor, Awaitable awaitable)
    : executor_(std::move(executor)), awaitable_(std::move(awaitable))
  {}

  /** @brief Initiates the spawn with a Boost.Asio completion token.
   *
   *  @tparam Token A valid Boost.Asio completion token
   *  @param token The completion token determining how to handle completion
   *  @return Depends on the token (e.g., void for callbacks, awaitable for use_awaitable)
   */
  template<detail::asio_spawn_token<Executor, Awaitable> Token>
  auto operator()(Token && token)
  {
    return detail::initialize_asio_spawn_helper<Awaitable, Token>::init(
            std::move(executor_),
            std::move(awaitable_),
            std::forward<Token>(token)
          );
  }

  /** @brief Initiates the spawn with a standalone Asio completion token.
   *
   *  @tparam Token A valid standalone Asio completion token
   *  @param token The completion token determining how to handle completion
   *  @return Depends on the token
   */
  template<detail::asio_standalone_spawn_token<Executor, Awaitable> Token>
  auto operator()(Token && token)
  {
    return detail::initialize_asio_standalone_spawn_helper<Awaitable, Token>::init
          (
            std::move(executor_),
            std::move(awaitable_),
            std::forward<Token>(token)
          );
  }

 private:
  Executor executor_;
  Awaitable awaitable_;
};


/** @brief Spawns a capy coroutine for execution with an Asio completion token.
 *
 *  Creates a deferred spawn operation that can be initiated with any Asio
 *  completion token. The coroutine will run on the specified executor.
 *
 *  @tparam ExecutorType The executor type (must satisfy `Executor`)
 *  @tparam Awaitable The coroutine type (must satisfy `IoAwaitable`)
 *  @param exec The executor to run the coroutine on
 *  @param awaitable The coroutine to spawn
 *  @return An `asio_spawn_op` that can be called with a completion token
 *
 *  @par Completion Signature
 *  The completion signature depends on the coroutine's return type:
 *  - `void` return, noexcept: `void()`
 *  - `void` return, may throw: `void(std::exception_ptr)`
 *  - `T` return, noexcept: `void(T)`
 *  - `T` return, may throw: `void(std::exception_ptr, T)`
 *
 *  @par Example
 *  @code
 *  capy::io_task<int> compute() { co_return 42; }
 *
 *  // Using with Boost.Asio
 *  asio_spawn(executor, compute())(
 *      [](std::exception_ptr ep, int result) {
 *          if (!ep) std::cout << "Result: " << result << "\n";
 *      });
 *
 *  // Using with asio::use_awaitable
 *  auto [ep, result] = co_await asio_spawn(executor, compute())(asio::use_awaitable);
 *  @endcode
 *
 *  @see asio_spawn_op The returned operation type
 */
template<Executor ExecutorType, IoAwaitable Awaitable>
auto asio_spawn(ExecutorType exec, Awaitable && awaitable)
{
  return asio_spawn_op(std::move(exec), std::forward<Awaitable>(awaitable));
}

/** @brief Spawns a capy coroutine using a context's executor.
 *
 *  Convenience overload that extracts the executor from an execution context.
 *
 *  @tparam Context The execution context type (must satisfy `ExecutionContext`)
 *  @tparam Awaitable The coroutine type (must satisfy `IoAwaitable`)
 *  @param ctx The execution context providing the executor
 *  @param awaitable The coroutine to spawn
 *  @return An `asio_spawn_op` that can be called with a completion token
 *
 *  @par Example
 *  @code
 *  boost::asio::io_context io;
 *  asio_spawn(io, my_coroutine())(asio::detached);
 *  io.run();
 *  @endcode
 */
template<ExecutionContext Context, IoAwaitable Awaitable>
auto asio_spawn(Context & ctx, Awaitable && awaitable)
{
  return asio_spawn_op(ctx.get_executor(), std::forward<Awaitable>(awaitable));
}

/** @} */ // end of asio group

}

#endif
