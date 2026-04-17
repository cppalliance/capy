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

/** @file standalone.hpp
 *  @brief Standalone Asio integration for capy coroutines.
 *
 *  This header provides complete integration between capy's coroutine framework
 *  and standalone Asio (without Boost). Include this header when using capy
 *  with standalone Asio.
 *
 *  @par Features
 *  - Property query/require support for `asio_executor_adapter`
 *  - `asio_standalone_standard_executor` wrapper for standalone Asio executors
 *  - `async_result` specialization for `as_io_awaitable_t`
 *  - Three-argument `asio_spawn` overloads with completion tokens
 *
 *  @par Example
 *  @code
 *  #include <boost/capy/asio/standalone.hpp>
 *  #include <asio.hpp>
 *
 *  capy::io_task<void> my_coro(asio::ip::tcp::socket& sock) {
 *      char buf[1024];
 *      auto [n] = co_await sock.async_read_some(
 *          asio::buffer(buf), capy::as_io_awaitable);
 *      // ...
 *  }
 *
 *  int main() {
 *      asio::io_context io;
 *      auto exec = capy::wrap_asio_executor(io.get_executor());
 *      capy::asio_spawn(exec, my_coro(socket))(asio::detached);
 *      io.run();
 *  }
 *  @endcode
 *
 *  @see boost.hpp For Boost.Asio support
 *  @ingroup asio
 */

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

/** @addtogroup asio
 *  @{
 */

/// @name Execution Property Queries for asio_executor_adapter (Standalone Asio)
/// @{

/** @brief Queries the execution context from an asio_executor_adapter.
 *  @param exec The executor adapter to query
 *  @return Reference to the associated `::asio::execution_context`
 */
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

/** @brief Queries the blocking property.
 *  @param exec The executor adapter to query
 *  @return The current blocking property value
 */
template<typename Executor, typename Allocator, int Bits>
constexpr ::asio::execution::blocking_t
    query(const asio_executor_adapter<Executor, Allocator, Bits> &,
          ::asio::execution::blocking_t) noexcept
{
  using ex = asio_executor_adapter<Executor, Allocator, Bits>;
  switch (Bits & ex::blocking_mask)
  {
    case ex::blocking_never:    return ::asio::execution::blocking.never;
    case ex::blocking_always:   return ::asio::execution::blocking.always;
    case ex::blocking_possibly: return ::asio::execution::blocking.possibly;
    default: return {};
  }
}

/// @}

/// @name Execution Property Requirements for asio_executor_adapter (Standalone Asio)
/// @{

/** @brief Requires blocking.possibly property.
 *  @return New adapter with blocking.possibly set
 */
template<typename Executor, typename Allocator, int Bits>
constexpr auto
    require(const asio_executor_adapter<Executor, Allocator, Bits> & exec,
            ::asio::execution::blocking_t::possibly_t)
{
  using ex = asio_executor_adapter<Executor, Allocator, Bits>;
  constexpr int new_bits = (Bits & ~ex::blocking_mask) | ex::blocking_possibly;
  return asio_executor_adapter<Executor, Allocator, new_bits>(exec);
}

/** @brief Requires blocking.never property.
 *  @return New adapter that never blocks
 */
template<typename Executor, typename Allocator, int Bits>
constexpr auto
    require(const asio_executor_adapter<Executor, Allocator, Bits> & exec,
            ::asio::execution::blocking_t::never_t)
{
  using ex = asio_executor_adapter<Executor, Allocator, Bits>;
  constexpr int new_bits = (Bits & ~ex::blocking_mask) | ex::blocking_never;
  return asio_executor_adapter<Executor, Allocator, new_bits>(exec);
}

/** @brief Requires blocking.always property.
 *  @return New adapter that always blocks until execution completes
 */
template<typename Executor, typename Allocator, int Bits>
constexpr auto
    require(const asio_executor_adapter<Executor, Allocator, Bits> & exec,
            ::asio::execution::blocking_t::always_t)
{
  using ex = asio_executor_adapter<Executor, Allocator, Bits>;
  constexpr int new_bits = (Bits & ~ex::blocking_mask) | ex::blocking_always;
  return asio_executor_adapter<Executor, Allocator, new_bits>(exec);
}

/** @brief Queries the outstanding_work property.
 *  @param exec The executor adapter to query
 *  @return The current work tracking setting
 */
template<typename Executor, typename Allocator, int Bits>
static constexpr ::asio::execution::outstanding_work_t query(
    const asio_executor_adapter<Executor, Allocator, Bits> &,
    ::asio::execution::outstanding_work_t) noexcept
{
  using ex = asio_executor_adapter<Executor, Allocator, Bits>;
  switch (Bits & ex::work_mask)
  {
    case ex::work_tracked:
      return ::asio::execution::outstanding_work.tracked;
    case ex::work_untracked:
      return ::asio::execution::outstanding_work.untracked;
    default: return {};
  }
}

/** @brief Requires outstanding_work.tracked property.
 *  @return New adapter that tracks outstanding work
 */
template<typename Executor, typename Allocator, int Bits>
constexpr auto
    require(const asio_executor_adapter<Executor, Allocator, Bits> & exec,
            ::asio::execution::outstanding_work_t::tracked_t)
{
  using ex = asio_executor_adapter<Executor, Allocator, Bits>;
  constexpr int new_bits = (Bits & ~ex::work_mask) | ex::work_tracked;
  return asio_executor_adapter<Executor, Allocator, new_bits>(exec);
}

/** @brief Requires outstanding_work.untracked property.
 *  @return New adapter that does not track outstanding work
 */
template<typename Executor, typename Allocator, int Bits>
constexpr auto
    require(const asio_executor_adapter<Executor, Allocator, Bits> & exec,
            ::asio::execution::outstanding_work_t::untracked_t)
{
  using ex = asio_executor_adapter<Executor, Allocator, Bits>;
  constexpr int new_bits = (Bits & ~ex::work_mask) | ex::work_untracked;
  return asio_executor_adapter<Executor, Allocator, new_bits>(exec);
}

/** @brief Queries the allocator property.
 *  @return The adapter's current allocator
 */
template <typename Executor, typename Allocator, int Bits, typename OtherAlloc>
constexpr Allocator query(
    const asio_executor_adapter<Executor, Allocator, Bits> & exec,
    ::asio::execution::allocator_t<OtherAlloc>) noexcept
{
  return exec.get_allocator();
}

/** @brief Requires a specific allocator.
 *  @param a The allocator property containing the new allocator
 *  @return New adapter using the specified allocator
 */
template <typename Executor, typename Allocator, int Bits, typename OtherAlloc>
constexpr auto
    require(const asio_executor_adapter<Executor, Allocator, Bits> & exec,
            ::asio::execution::allocator_t<OtherAlloc> a)
{
  return asio_executor_adapter<Executor, OtherAlloc, Bits>(
      exec, a.value()
    );
}

/** @brief Requires the default allocator (uses frame allocator).
 *  @return New adapter using the frame allocator from the context
 */
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

/// @}

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

  std::mutex mutex;
  std::size_t work = 0u;

  void shutdown()
  {
    std::lock_guard _(mutex);
    if (std::exchange(work, 0) > 0u)
      reinterpret_cast<tracked_executor*>(buffer)->~tracked_executor();
  }


  void work_started(const Executor & exec)
  {
    std::lock_guard _(mutex);
    if (work ++ == 0u)
      new (buffer) tracked_executor(
        ::asio::prefer(exec, 
        ::asio::execution::outstanding_work.tracked));
  }

  void work_finished() 
  {
      std::lock_guard _(mutex);
      if (--work == 0u)
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

  /** @brief Notifies that work has started. */
  void on_work_started() const noexcept
  {
    auto & ec = ::asio::query(executor_, ::asio::execution::context);
    ::asio::use_service<
              detail::asio_standalone_work_tracker_service<Executor>
                       >(ec).work_started(executor_);
  }

  /** @brief Notifies that work has finished. */
  void on_work_finished() const noexcept
  {
    auto & ec = ::asio::query(executor_, ::asio::execution::context);
    ::asio::use_service<
              detail::asio_standalone_work_tracker_service<Executor>
                       >(ec).work_finished();
  }

  /** @brief Dispatches a continuation for execution.
   *  @param c The continuation to dispatch
   *  @return A noop coroutine handle
   */
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

  /** @brief Posts a continuation for deferred execution.
   *  @param c The continuation to post
   */
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

  /** @brief Equality comparison. */
  bool operator==(
          const asio_standalone_standard_executor & rhs) const noexcept
  {
    return executor_  == rhs.executor_;
  }

  /** @brief Inequality comparison. */
  bool operator!=(
          const asio_standalone_standard_executor & rhs) const noexcept
  {
    return executor_  != rhs.executor_;
  }

 private:
  Executor executor_;
};


/** @brief Spawns a capy coroutine with a standalone Asio completion token (executor overload).
 *
 *  Convenience overload that combines the two-step spawn process into one call.
 *
 *  @tparam ExecutorType The executor type
 *  @tparam Runnable The coroutine type
 *  @tparam Token A standalone Asio completion token
 *  @param exec The executor to run on
 *  @param runnable The coroutine to spawn
 *  @param token The completion token
 *  @return Depends on the token type
 */
template<Executor ExecutorType,
         IoRunnable Runnable,
         ::asio::completion_token_for<
              detail::completion_signature_for_io_runnable<Runnable>
                                     > Token>
auto asio_spawn(ExecutorType exec, Runnable && runnable, Token token)
{
  return asio_spawn(exec, std::forward<Runnable>(runnable))(std::move(token));
}

/** @brief Spawns a capy coroutine with a standalone Asio completion token (context overload).
 *
 *  Convenience overload that extracts the executor from a context.
 *
 *  @tparam Context The execution context type
 *  @tparam Runnable The coroutine type
 *  @tparam Token A standalone Asio completion token
 *  @param ctx The execution context
 *  @param runnable The coroutine to spawn
 *  @param token The completion token
 *  @return Depends on the token type
 */
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

/** @} */ // end of asio group

}

/** @brief Standalone Asio async_result specialization for as_io_awaitable_t.
 *
 *  This specialization enables `as_io_awaitable` to be used as a completion
 *  token with any standalone Asio async operation.
 *
 *  @tparam Ts The completion signature argument types
 */
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
                         Ex &, Runnable &)
  {
    using allocator_type = std::allocator_traits<Allocator>
                              ::template rebind_alloc<char>;
    allocator_type allocator(::asio::get_associated_allocator(handler));

    // round n up to max_align
    if (const auto d = n % sizeof(std::max_align_t); d > 0u)
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
    if (const auto d = n % sizeof(std::max_align_t); d > 0u)
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
          Runnable &)  
            : handler(h), ex(exec) {}

    Handler & handler;
    Ex &ex;

    void get_return_object() {}
    void unhandled_exception() {throw;}
    void return_void() {}

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

        auto handler_ =         
            std::apply( 
              [&](auto ... args) 
              {
                return ::asio::append(std::move(h_), std::move(args)...);
              },
              args_);

        auto exec = ::asio::get_associated_immediate_executor(handler_, ex_);
        ::asio::dispatch(exec, std::move(handler_));                  
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

      wrapper(Runnable && r, const Ex &ex) 
          : r(std::move(r)), ex(ex)
      {
      }

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
                  Handler ,
                  Ex,
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
      -> decltype(::asio::async_initiate<
        Token,
        completion_signature_for_io_runnable<Runnable>>(
          boost_asio_standalone_init{},
          tk, std::move(ex), std::move(r)
          ))
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

