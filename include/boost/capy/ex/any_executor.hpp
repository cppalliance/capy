//
// Copyright (c) 2025 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_ANY_EXECUTOR_HPP
#define BOOST_CAPY_ANY_EXECUTOR_HPP

#include <boost/capy/detail/config.hpp>
#include <boost/capy/ex/any_coro.hpp>

#include <concepts>
#include <coroutine>
#include <memory>
#include <type_traits>
#include <typeinfo>

namespace boost {
namespace capy {

class execution_context;

/** A type-erased wrapper for executor objects.

    This class provides type erasure for any executor type, enabling
    runtime polymorphism with automatic memory management via shared
    ownership. It stores a shared pointer to a polymorphic wrapper,
    allowing executors of different types to be stored uniformly
    while satisfying the full `Executor` concept.

    @par Value Semantics

    This class has value semantics with shared ownership. Copy and
    move operations are cheap, simply copying the internal shared
    pointer. Multiple `any_executor` instances may share the same
    underlying executor. Move operations do not invalidate the
    source; there is no moved-from state.

    @par Default State

    A default-constructed `any_executor` holds no executor. Calling
    executor operations on a default-constructed instance results
    in undefined behavior. Use `operator bool()` to check validity.

    @par Thread Safety

    The `any_executor` itself is thread-safe for concurrent reads.
    Concurrent modification requires external synchronization.
    Executor operations are safe to call concurrently if the
    underlying executor supports it.

    @par Executor Concept

    This class satisfies the `Executor` concept, making it usable
    anywhere a concrete executor is expected.

    @see any_executor_ref, Executor
*/
class any_executor
{
    struct impl_base;

    std::shared_ptr<impl_base> p_;

    struct impl_base
    {
        virtual ~impl_base() = default;
        virtual execution_context& context() const noexcept = 0;
        virtual void on_work_started() const noexcept = 0;
        virtual void on_work_finished() const noexcept = 0;
        virtual std::coroutine_handle<> dispatch(std::coroutine_handle<>) const = 0;
        virtual void post(std::coroutine_handle<>) const = 0;
        virtual bool equals(impl_base const*) const noexcept = 0;
        virtual std::type_info const& target_type() const noexcept = 0;
    };

    template<class Ex>
    struct impl final : impl_base
    {
        Ex ex_;

        template<class Ex1>
        explicit impl(Ex1&& ex)
            : ex_(std::forward<Ex1>(ex))
        {
        }

        execution_context& context() const noexcept override
        {
            return const_cast<Ex&>(ex_).context();
        }

        void on_work_started() const noexcept override
        {
            ex_.on_work_started();
        }

        void on_work_finished() const noexcept override
        {
            ex_.on_work_finished();
        }

        std::coroutine_handle<> dispatch(std::coroutine_handle<> h) const override
        {
            return ex_.dispatch(h);
        }

        void post(std::coroutine_handle<> h) const override
        {
            ex_.post(h);
        }

        bool equals(impl_base const* other) const noexcept override
        {
            if(target_type() != other->target_type())
                return false;
            return ex_ == static_cast<impl const*>(other)->ex_;
        }

        std::type_info const& target_type() const noexcept override
        {
            return typeid(Ex);
        }
    };

public:
    /** Default constructor.

        Constructs an empty `any_executor`. Calling any executor
        operations on a default-constructed instance results in
        undefined behavior.

        @par Postconditions
        @li `!*this`
    */
    any_executor() = default;

    /** Copy constructor.

        Creates a new `any_executor` sharing ownership of the
        underlying executor with `other`.

        @par Postconditions
        @li `*this == other`
    */
    any_executor(any_executor const&) = default;

    /** Copy assignment operator.

        Shares ownership of the underlying executor with `other`.

        @par Postconditions
        @li `*this == other`
    */
    any_executor& operator=(any_executor const&) = default;

    /** Constructs from any executor type.

        Allocates storage for a copy of the given executor and
        stores it internally. The executor must satisfy the
        `Executor` concept.

        @param ex The executor to wrap. A copy is stored internally.

        @par Postconditions
        @li `*this` is valid
    */
    template<class Ex>
        requires (
            !std::same_as<std::decay_t<Ex>, any_executor> &&
            std::copy_constructible<std::decay_t<Ex>>)
    any_executor(Ex&& ex)
        : p_(std::make_shared<impl<std::decay_t<Ex>>>(std::forward<Ex>(ex)))
    {
    }

    /** Returns true if this instance holds a valid executor.

        @return `true` if constructed with an executor, `false` if
                default-constructed.
    */
    explicit operator bool() const noexcept
    {
        return p_ != nullptr;
    }

    /** Returns a reference to the associated execution context.

        @return A reference to the execution context.

        @pre This instance holds a valid executor.
    */
    execution_context& context() const noexcept
    {
        return p_->context();
    }

    /** Informs the executor that work is beginning.

        Must be paired with a subsequent call to `on_work_finished()`.

        @pre This instance holds a valid executor.
    */
    void on_work_started() const noexcept
    {
        p_->on_work_started();
    }

    /** Informs the executor that work has completed.

        @pre A preceding call to `on_work_started()` was made.
        @pre This instance holds a valid executor.
    */
    void on_work_finished() const noexcept
    {
        p_->on_work_finished();
    }

    /** Dispatches a coroutine handle through the wrapped executor.

        Invokes the executor's `dispatch()` operation with the given
        coroutine handle, returning a handle suitable for symmetric
        transfer.

        @param h The coroutine handle to dispatch for resumption.

        @return A coroutine handle that the caller may use for symmetric
                transfer, or `std::noop_coroutine()` if the executor
                posted the work for later execution.

        @pre This instance holds a valid executor.
    */
    any_coro dispatch(any_coro h) const
    {
        return p_->dispatch(h);
    }

    /** Posts a coroutine handle to the wrapped executor.

        Posts the coroutine handle to the executor for later execution
        and returns. The caller should transfer to `std::noop_coroutine()`
        after calling this.

        @param h The coroutine handle to post for resumption.

        @pre This instance holds a valid executor.
    */
    void post(any_coro h) const
    {
        p_->post(h);
    }

    /** Compares two executor wrappers for equality.

        Two `any_executor` instances are equal if they both hold
        executors of the same type that compare equal, or if both
        are empty.

        @param other The executor to compare against.

        @return `true` if both wrap equal executors of the same type,
                or both are empty.
    */
    bool operator==(any_executor const& other) const noexcept
    {
        if(!p_ && !other.p_)
            return true;
        if(!p_ || !other.p_)
            return false;
        return p_->equals(other.p_.get());
    }

    /** Returns the type_info of the wrapped executor.

        @return The `std::type_info` of the stored executor type,
                or `typeid(void)` if empty.
    */
    std::type_info const& target_type() const noexcept
    {
        if(!p_)
            return typeid(void);
        return p_->target_type();
    }
};

} // capy
} // boost

#endif
