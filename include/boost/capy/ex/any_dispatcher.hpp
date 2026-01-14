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
#include <boost/capy/ex/coro.hpp>
#include <boost/capy/concept/dispatcher.hpp>

#include <concepts>
#include <type_traits>

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

    @par Example
    @code
    void store_dispatcher(any_dispatcher d)
    {
        // Can store any dispatcher type uniformly
        auto h = d(some_coroutine);  // Invoke through type-erased interface
    }

    executor_base const& ex = get_executor();
    store_dispatcher(ex);  // Implicitly converts to any_dispatcher
    @endcode

    @see dispatcher
    @see executor_base
*/
class any_dispatcher
{
    void const* d_ = nullptr;
    coro(*f_)(void const*, coro) = nullptr;

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
        , f_([](void const* pd, coro h) {
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
    coro operator()(coro h) const
    {
        return f_(d_, h);
    }
};

} // capy
} // boost

#endif
