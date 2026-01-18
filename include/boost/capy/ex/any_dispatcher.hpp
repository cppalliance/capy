//
// Copyright (c) 2025 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_ANY_DISPATCHER_HPP
#define BOOST_CAPY_ANY_DISPATCHER_HPP

#include <boost/capy/detail/config.hpp>
#include <boost/capy/ex/any_coro.hpp>
#include <boost/capy/concept/dispatcher.hpp>
#include <boost/capy/concept/executor.hpp>

#include <concepts>
#include <type_traits>
#include <utility>

namespace boost {
namespace capy {

/** A type-erased wrapper for dispatcher objects.

    This class provides type erasure for any type satisfying the `dispatcher`
    concept, enabling runtime polymorphism without virtual functions. It stores
    a pointer to the original dispatcher and a function pointer to invoke it,
    allowing dispatchers of different types to be stored uniformly.

    @par Thread Safety
    The `any_dispatcher` itself is not thread-safe for concurrent modification,
    but `operator()` is const and safe to call concurrently if the underlying
    dispatcher supports concurrent dispatch.

    @par Lifetime
    The `any_dispatcher` stores a pointer to the original dispatcher object.
    The caller must ensure the referenced dispatcher outlives the `any_dispatcher`
    instance. This is typically satisfied when the dispatcher is an executor
    stored in a coroutine promise or service provider.

    @see dispatcher
*/
class any_dispatcher
{
    void const* d_ = nullptr;
    any_coro(*f_)(void const*, any_coro) = nullptr;

public:
    /** Default constructor.

        Constructs an empty `any_dispatcher`. Calling `operator()` on a
        default-constructed instance results in undefined behavior.
    */
    any_dispatcher() = default;

    /** Copy constructor.

        Copies the internal pointer and function, preserving identity.
        This enables the same-dispatcher optimization when passing
        any_dispatcher through coroutine chains.
    */
    any_dispatcher(any_dispatcher const&) = default;

    /** Copy assignment operator. */
    any_dispatcher& operator=(any_dispatcher const&) = default;

    /** Constructs from any dispatcher type.

        Captures a reference to the given dispatcher and stores a type-erased
        invocation function. The dispatcher must remain valid for the lifetime
        of this `any_dispatcher` instance.

        @param d The dispatcher to wrap. Must satisfy the `dispatcher` concept.
                 A pointer to this object is stored internally; the dispatcher
                 must outlive this wrapper.
    */
    template<dispatcher D>
        requires (!std::same_as<std::decay_t<D>, any_dispatcher>)
    any_dispatcher(D const& d)
        : d_(&d)
        , f_([](void const* pd, any_coro h) {
                return static_cast<D const*>(pd)->operator()(h);
            })
    {
    }

    /** Returns true if this instance holds a valid dispatcher.

        @return `true` if constructed with a dispatcher, `false` if
                default-constructed.
    */
    explicit operator bool() const noexcept
    {
        return d_ != nullptr;
    }

    /** Compares two dispatchers for identity.

        Two `any_dispatcher` instances are equal if they wrap the same
        underlying dispatcher object (pointer equality). This enables
        the affinity optimization: when `caller_dispatcher == my_dispatcher`,
        symmetric transfer can proceed without a `running_in_this_thread()`
        check.

        @param other The dispatcher to compare against.

        @return `true` if both wrap the same dispatcher object.
    */
    bool operator==(any_dispatcher const& other) const noexcept
    {
        return d_ == other.d_;
    }

    /** Dispatches a coroutine handle through the wrapped dispatcher.

        Invokes the stored dispatcher with the given coroutine handle,
        returning a handle suitable for symmetric transfer.

        @param h The coroutine handle to dispatch for resumption.

        @return A coroutine handle that the caller may use for symmetric
                transfer, or `std::noop_coroutine()` if the dispatcher
                posted the work for later execution.

        @pre This instance was constructed with a valid dispatcher
             (not default-constructed).
    */
    any_coro operator()(any_coro h) const
    {
        return f_(d_, h);
    }
};

//------------------------------------------------------------------------------

/** A dispatcher that calls executor::post().

    Adapts an executor's post() operation to the dispatcher
    interface. When invoked, posts the coroutine and returns
    noop_coroutine for the caller to transfer to.

    @tparam Executor The executor type.
*/
template<executor Executor>
class post_dispatcher
{
    Executor ex_;

public:
    explicit post_dispatcher(Executor ex) noexcept
        : ex_(std::move(ex))
    {}

    Executor const& get_inner_executor() const noexcept { return ex_; }

    any_coro operator()(any_coro h) const
    {
        ex_.post(h);
        return std::noop_coroutine();
    }
};

/** A dispatcher that calls executor::defer().

    Adapts an executor's defer() operation to the dispatcher
    interface. When invoked, defers the coroutine and returns
    noop_coroutine for the caller to transfer to.

    @tparam Executor The executor type.
*/
template<executor Executor>
class defer_dispatcher
{
    Executor ex_;

public:
    explicit defer_dispatcher(Executor ex) noexcept
        : ex_(std::move(ex))
    {}

    Executor const& get_inner_executor() const noexcept { return ex_; }

    any_coro operator()(any_coro h) const
    {
        ex_.defer(h);
        return std::noop_coroutine();
    }
};

template<executor E> post_dispatcher(E) -> post_dispatcher<E>;
template<executor E> defer_dispatcher(E) -> defer_dispatcher<E>;

//------------------------------------------------------------------------------

/** A type-erased dispatcher that calls executor::post().

    This class type-erases the executor while implementing the `dispatcher`
    concept. It stores a pointer to the original executor and a function
    pointer to invoke its `post()` operation, allowing executors of different
    types to be used uniformly for posting coroutines.

    @par Thread Safety
    The `any_post_dispatcher` itself is not thread-safe for concurrent
    modification, but `operator()` is const and safe to call concurrently
    if the underlying executor supports concurrent post operations.

    @par Lifetime
    The `any_post_dispatcher` stores a pointer to the original executor object.
    The caller must ensure the referenced executor outlives the
    `any_post_dispatcher` instance. This is typically satisfied when the
    executor is stored in a derived class or coroutine frame.

    @see executor, dispatcher
*/
class any_post_dispatcher
{
    void const* ex_ = nullptr;
    void(*post_fn_)(void const*, any_coro) = nullptr;

public:
    /** Default constructor.

        Constructs an empty `any_post_dispatcher`. Calling `operator()` on a
        default-constructed instance results in undefined behavior.
    */
    any_post_dispatcher() = default;

    /** Copy constructor. */
    any_post_dispatcher(any_post_dispatcher const&) = default;

    /** Copy assignment operator. */
    any_post_dispatcher& operator=(any_post_dispatcher const&) = default;

    /** Constructs from any executor type.

        Captures a reference to the given executor and stores a type-erased
        function to invoke its `post()` operation. The executor must remain
        valid for the lifetime of this `any_post_dispatcher` instance.

        @param ex The executor to wrap. Must satisfy the `executor` concept.
                  A pointer to this object is stored internally; the executor
                  must outlive this wrapper.
    */
    template<class E>
        requires (!std::same_as<std::decay_t<E>, any_post_dispatcher>) && executor<E>
    any_post_dispatcher(E const& ex) noexcept
        : ex_(&ex)
        , post_fn_([](void const* p, any_coro h) {
                static_cast<E const*>(p)->post(h);
            })
    {
    }

    /** Returns true if this instance holds a valid executor.

        @return `true` if constructed with an executor, `false` if
                default-constructed.
    */
    explicit operator bool() const noexcept
    {
        return ex_ != nullptr;
    }

    /** Dispatches a coroutine handle by posting to the wrapped executor.

        Posts the coroutine handle to the executor for later execution
        and returns `noop_coroutine()` for the caller to transfer to.

        @param h The coroutine handle to post for resumption.

        @return `std::noop_coroutine()` since the work is posted for
                later execution.

        @pre This instance was constructed with a valid executor
             (not default-constructed).
    */
    any_coro operator()(any_coro h) const
    {
        post_fn_(ex_, h);
        return std::noop_coroutine();
    }
};

//------------------------------------------------------------------------------

/** A type-erased dispatcher that calls executor::defer().

    This class type-erases the executor while implementing the `dispatcher`
    concept. It stores a pointer to the original executor and a function
    pointer to invoke its `defer()` operation, allowing executors of different
    types to be used uniformly for deferring coroutines.

    @par Thread Safety
    The `any_defer_dispatcher` itself is not thread-safe for concurrent
    modification, but `operator()` is const and safe to call concurrently
    if the underlying executor supports concurrent defer operations.

    @par Lifetime
    The `any_defer_dispatcher` stores a pointer to the original executor object.
    The caller must ensure the referenced executor outlives the
    `any_defer_dispatcher` instance. This is typically satisfied when the
    executor is stored in a derived class or coroutine frame.

    @see executor, dispatcher
*/
class any_defer_dispatcher
{
    void const* ex_ = nullptr;
    void(*defer_fn_)(void const*, any_coro) = nullptr;

public:
    /** Default constructor.

        Constructs an empty `any_defer_dispatcher`. Calling `operator()` on a
        default-constructed instance results in undefined behavior.
    */
    any_defer_dispatcher() = default;

    /** Copy constructor. */
    any_defer_dispatcher(any_defer_dispatcher const&) = default;

    /** Copy assignment operator. */
    any_defer_dispatcher& operator=(any_defer_dispatcher const&) = default;

    /** Constructs from any executor type.

        Captures a reference to the given executor and stores a type-erased
        function to invoke its `defer()` operation. The executor must remain
        valid for the lifetime of this `any_defer_dispatcher` instance.

        @param ex The executor to wrap. Must satisfy the `executor` concept.
                  A pointer to this object is stored internally; the executor
                  must outlive this wrapper.
    */
    template<class E>
        requires (!std::same_as<std::decay_t<E>, any_defer_dispatcher>) && executor<E>
    any_defer_dispatcher(E const& ex) noexcept
        : ex_(&ex)
        , defer_fn_([](void const* p, any_coro h) {
                static_cast<E const*>(p)->defer(h);
            })
    {
    }

    /** Returns true if this instance holds a valid executor.

        @return `true` if constructed with an executor, `false` if
                default-constructed.
    */
    explicit operator bool() const noexcept
    {
        return ex_ != nullptr;
    }

    /** Dispatches a coroutine handle by deferring to the wrapped executor.

        Defers the coroutine handle to the executor for later execution
        and returns `noop_coroutine()` for the caller to transfer to.

        @param h The coroutine handle to defer for resumption.

        @return `std::noop_coroutine()` since the work is deferred for
                later execution.

        @pre This instance was constructed with a valid executor
             (not default-constructed).
    */
    any_coro operator()(any_coro h) const
    {
        defer_fn_(ex_, h);
        return std::noop_coroutine();
    }
};

} // capy
} // boost

#endif
