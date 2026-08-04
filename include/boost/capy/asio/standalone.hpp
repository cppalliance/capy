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

#include <algorithm>
#include <boost/capy/concept/io_awaitable.hpp>
#include <boost/capy/concept/decomposes_to.hpp>


#include <boost/capy/asio/as_io_awaitable.hpp>
#include <boost/capy/asio/buffers.hpp>
#include <boost/capy/asio/detail/completion_handler.hpp>
#include <boost/capy/asio/executor_adapter.hpp>
#include <boost/capy/asio/executor_from_asio.hpp>
#include <boost/capy/asio/spawn.hpp>
#include <boost/capy/concept/decomposes_to.hpp>
#include <asio/append.hpp>
#include <asio/associated_allocator.hpp>
#include <asio/associated_cancellation_slot.hpp>
#include <asio/associated_immediate_executor.hpp>
#include <asio/buffer.hpp>
#include <asio/dispatch.hpp>
#include <asio/cancellation_signal.hpp>
#include <asio/cancellation_type.hpp>
#include <asio/execution_context.hpp>
#include <asio/execution/allocator.hpp>
#include <asio/execution/blocking.hpp>
#include <asio/execution/context.hpp>
#include <asio/execution/outstanding_work.hpp>
#include <asio/execution/relationship.hpp>
#include <concepts>

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
 *  @tparam Awaitable The coroutine type
 *  @tparam Token A standalone Asio completion token
 *  @param exec The executor to run on
 *  @param awaitable The coroutine to spawn
 *  @param token The completion token
 *  @return Depends on the token type
 */
template<Executor ExecutorType,
         IoAwaitable Awaitable,
         ::asio::completion_token_for<
              detail::completion_signature_for_io_awaitable<Awaitable>
                                     > Token>
auto asio_spawn(ExecutorType exec, Awaitable && awaitable, Token token)
{
  return asio_spawn(exec, std::forward<Awaitable>(awaitable))(std::move(token));
}

/** @brief Spawns a capy coroutine with a standalone Asio completion token (context overload).
 *
 *  Convenience overload that extracts the executor from a context.
 *
 *  @tparam Context The execution context type
 *  @tparam Awaitable The coroutine type
 *  @tparam Token A standalone Asio completion token
 *  @param ctx The execution context
 *  @param awaitable The coroutine to spawn
 *  @param token The completion token
 *  @return Depends on the token type
 */
template<ExecutionContext Context,
         IoAwaitable Awaitable,
         ::asio::completion_token_for<
            detail::completion_signature_for_io_awaitable<Awaitable>
          > Token
        >
auto asio_spawn(Context & ctx, Awaitable && awaitable, Token token)
{
  return asio_spawn(ctx.get_executor(), std::forward<Awaitable>(awaitable))
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
  template<typename Handler, Executor Ex, IoAwaitable Awaitable>
  void * operator new   (std::size_t n, boost_asio_standalone_init &, 
                         Handler & handler, 
                         Ex &, Awaitable &)
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

template<>
struct boost_asio_standalone_promise_type_allocator_base<std::allocator<void>>
{
};


template<typename Handler, Executor Ex, IoAwaitable Awaitable>
struct boost_asio_standalone_init_promise_type
    : boost_asio_standalone_promise_type_allocator_base<
            ::asio::associated_allocator_t<Handler>
          >
{
    using args_type = completion_tuple_for_io_awaitable<Awaitable>;

    boost_asio_standalone_init_promise_type(
          boost_asio_standalone_init &, 
          Handler & h, 
          Ex & exec, 
          Awaitable &)  
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
        auto handler_ =         
            std::apply( 
              [&](auto ... args) 
              {
                return ::asio::append(std::move(handler), 
                                           std::move(args)...);
              },
              detail::decomposed_types(std::move(args)));

        auto exec = ::asio::get_associated_immediate_executor(
                          handler_, 
                          asio_executor_adapter(std::move(ex)));
        
        h.destroy();
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
      Awaitable r;
      const Ex &ex;
      io_env env;
      std::stop_source stop_src;
      ::asio::cancellation_slot cancel_slot;

      wrapper(Awaitable && r, const Ex &ex) 
          : r(std::move(r)), ex(ex)
      {
      }

      continuation c;

      bool await_ready() {return r.await_ready(); }

      auto await_suspend(
          std::coroutine_handle<boost_asio_standalone_init_promise_type> tr)
      {
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


        using suspend_kind = decltype(r.await_suspend(tr, &env));
        if constexpr (std::is_void_v<suspend_kind>)
          r.await_suspend(tr, &env);
        else if constexpr (std::same_as<suspend_kind, bool>)
          return r.await_suspend(tr, &env);
        else
        {
          c.h = r.await_suspend(tr, &env);
          return ex.dispatch(c);
        }
      }
      
      completion_tuple_for_io_awaitable<Awaitable> await_resume()
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

    wrapper await_transform(Awaitable & r)
    {
      return wrapper{std::move(r), ex};
    }
};


    
struct boost_asio_standalone_init
{
  template<typename Handler, Executor Ex, IoAwaitable Awaitable>
  void operator()(
                  Handler ,
                  Ex,
                  Awaitable awaitable)
  {
    auto res = co_await awaitable;
    co_yield std::move(res);
  }
};

template<typename Awaitable, typename Token>
  requires
    ::asio::completion_token_for<
          Token,
          completion_signature_for_io_awaitable<Awaitable>
        >
struct initialize_asio_standalone_spawn_helper<Awaitable, Token>
{
  template<typename Executor>
  static auto init(Executor ex, Awaitable r, Token && tk)
      -> decltype(::asio::async_initiate<
        Token,
        completion_signature_for_io_awaitable<Awaitable>>(
          boost_asio_standalone_init{},
          tk, std::move(ex), std::move(r)
          ))
  {
    return ::asio::async_initiate<
        Token,
        completion_signature_for_io_awaitable<Awaitable>>(
          boost_asio_standalone_init{},
          tk, std::move(ex), std::move(r)
          );
  }
};

}


template<typename Handler, typename Executor, typename Awaitable>
struct std::coroutine_traits<
              void,
              boost::capy::detail::boost_asio_standalone_init&,
              Handler,
              Executor,
              Awaitable>
{
  using promise_type
          = boost::capy::detail::boost_asio_standalone_init_promise_type<
                    Handler,
                    Executor,
                    Awaitable>;
};

namespace boost::capy
{


namespace detail
{


template<typename Sequence>
struct asio_standalone_buffer_sequence_wrapper
{
  Sequence seq;
  auto begin() const {return ::asio::buffer_sequence_begin(seq); }
  auto   end() const {return ::asio::buffer_sequence_end(seq); }
};

/** A bidirectional range that transforms buffer sequences using asio_buffer_transformer.
 *
 *  Wraps an asio buffer sequence and provides begin/end iterators that transform
 *  buffer elements via asio_buffer_transformer. Uses ::asio::buffer_sequence_begin/end.
 *
 *  @tparam Sequence The underlying buffer sequence type
 */
template<typename Sequence>
class asio_standalone_buffer_range
{
public:
    using sequence_type  = Sequence;
    using iterator       = asio_buffer_iterator<
                             decltype(::asio::buffer_sequence_begin(std::declval<const Sequence&>()))>;
    using const_iterator = iterator;

    asio_standalone_buffer_range() = default;

    explicit asio_standalone_buffer_range(Sequence seq)
        noexcept(std::is_nothrow_move_constructible_v<Sequence>)
        : seq_(std::move(seq))
    {
    }

    asio_standalone_buffer_range(const asio_standalone_buffer_range&) = default;
    asio_standalone_buffer_range(asio_standalone_buffer_range&&) = default;
    asio_standalone_buffer_range& operator=(const asio_standalone_buffer_range&) = default;
    asio_standalone_buffer_range& operator=(asio_standalone_buffer_range&&) = default;

    iterator begin() const
        noexcept(noexcept(::asio::buffer_sequence_begin(std::declval<const Sequence&>())))
    {
        return iterator(::asio::buffer_sequence_begin(seq_));
    }

    iterator end() const
        noexcept(noexcept(::asio::buffer_sequence_end(std::declval<const Sequence&>())))
    {
        return iterator(::asio::buffer_sequence_end(seq_));
    }

    /// Returns the underlying sequence.
    const Sequence& base() const noexcept { return seq_; }

private:
    Sequence seq_{};
};

// Deduction guide
template<typename Sequence>
asio_standalone_buffer_range(Sequence) -> asio_standalone_buffer_range<Sequence>;


asio_mutable_buffer asio_buffer_transformer_t::
    operator()(const ::asio::mutable_buffer &mb) const noexcept
{
  return {mb.data(), mb.size()};
}

asio_const_buffer asio_buffer_transformer_t::
    operator()(const ::asio::const_buffer &cb) const noexcept
{
  return {cb.data(), cb.size()};
}

}

/** Convert a standalone Asio buffer sequence for bidirectional Asio/capy compatibility.

    Wraps a standalone Asio buffer sequence in a transforming range that converts
    each buffer element to asio_const_buffer or asio_mutable_buffer.
    The returned range satisfies both standalone Asio's buffer sequence requirements
    and capy's buffer sequence concepts.

    This overload uses `::asio::buffer_sequence_begin` and
    `::asio::buffer_sequence_end` for iteration.

    @par Example: Asio to Capy
    @code
    std::vector<asio::mutable_buffer> asio_bufs = ...;
    auto seq = capy::as_asio_buffer_sequence(asio_bufs);
    std::size_t total = capy::buffer_size(seq);  // Use capy algorithms
    @endcode

    @param seq The standalone Asio buffer sequence to convert
    @return A transforming range over the buffer sequence

    @see as_asio_buffer_sequence in buffers.hpp for capy buffer sequences
*/
template<typename T>
  requires requires (const T & seq)
  {
    {::asio::buffer_sequence_begin(seq)}  -> std::bidirectional_iterator;
    {::asio::buffer_sequence_end(seq)}    -> std::bidirectional_iterator;
    {*::asio::buffer_sequence_begin(seq)} -> std::convertible_to<::asio::const_buffer>;
    {*::asio::buffer_sequence_end(seq)}   -> std::convertible_to<::asio::const_buffer>;
  }
auto as_asio_buffer_sequence(const T & seq)
{
  return detail::asio_standalone_buffer_range(seq);
}

asio_const_buffer::operator ::asio::const_buffer() const 
{
  return {data(), size()};
}

asio_mutable_buffer::operator ::asio::const_buffer() const 
{
  return {data(), size()};
}

asio_mutable_buffer::operator ::asio::mutable_buffer() const 
{
  return {data(), size()};
}


}

#endif // BOOST_CAPY_ASIO_STANDALONE_HPP

