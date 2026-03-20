//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_EXECUTOR_REF_HPP
#define BOOST_CAPY_EXECUTOR_REF_HPP

#include <boost/capy/detail/config.hpp>
#include <boost/capy/detail/type_id.hpp>
#include <boost/capy/continuation.hpp>
#include <concepts>
#include <coroutine>
#include <type_traits>
#include <utility>

namespace boost {
namespace capy {

class execution_context;

namespace detail {

/** Virtual function table for type-erased executor operations. */
struct executor_vtable
{
    execution_context& (*context)(void const*) noexcept;
    void (*on_work_started)(void const*) noexcept;
    void (*on_work_finished)(void const*) noexcept;
    void (*post)(void const*, continuation&);
    std::coroutine_handle<> (*dispatch)(void const*, continuation&);
    bool (*equals)(void const*, void const*) noexcept;
    detail::type_info const* type_id;
};

/** Vtable instance for a specific executor type. */
template<class Ex>
inline constexpr executor_vtable vtable_for = {
    // context
    [](void const* p) noexcept -> execution_context& {
        return const_cast<Ex*>(static_cast<Ex const*>(p))->context();
    },
    // on_work_started
    [](void const* p) noexcept {
        const_cast<Ex*>(static_cast<Ex const*>(p))->on_work_started();
    },
    // on_work_finished
    [](void const* p) noexcept {
        const_cast<Ex*>(static_cast<Ex const*>(p))->on_work_finished();
    },
    // post
    [](void const* p, continuation& c) {
        static_cast<Ex const*>(p)->post(c);
    },
    // dispatch
    [](void const* p, continuation& c) -> std::coroutine_handle<> {
        return static_cast<Ex const*>(p)->dispatch(c);
    },
    // equals
    [](void const* a, void const* b) noexcept -> bool {
        return *static_cast<Ex const*>(a) == *static_cast<Ex const*>(b);
    },
    // type_id
    &detail::type_id<Ex>()
};

} // detail

/** A type-erased reference wrapper for executor objects.

    This class provides type erasure for any executor type, enabling
    runtime polymorphism without virtual functions or allocation.
    It stores a pointer to the original executor and a pointer to a
    static vtable, allowing executors of different types to be stored
    uniformly while satisfying the full `Executor` concept.

    @par Reference Semantics
    This class has reference semantics: it does not allocate or own
    the wrapped executor. Copy operations simply copy the internal
    pointers. The caller must ensure the referenced executor outlives
    all `executor_ref` instances that wrap it.

    @par Thread Safety
    The `executor_ref` itself is not thread-safe for concurrent
    modification, but its executor operations are safe to call
    concurrently if the underlying executor supports it.

    @par Executor Concept
    This class satisfies the `Executor` concept, making it usable
    anywhere a concrete executor is expected.

    @par Example
    @code
    void store_executor(executor_ref ex)
    {
        if(ex)
            ex.post(my_continuation);
    }

    io_context ctx;
    store_executor(ctx.get_executor());
    @endcode

    @see any_executor, Executor
*/
class executor_ref
{
    void const* ex_ = nullptr;
    detail::executor_vtable const* vt_ = nullptr;

public:
    /** Construct a default instance.

        Constructs an empty `executor_ref`. Calling any executor
        operations on a default-constructed instance results in
        undefined behavior.
    */
    executor_ref() = default;

    /** Construct a copy.

        Copies the internal pointers, preserving identity.
        This enables the same-executor optimization when passing
        executor_ref through coroutine chains.
    */
    executor_ref(executor_ref const&) = default;

    /** Copy assignment operator. */
    executor_ref& operator=(executor_ref const&) = default;

    /** Constructs from any executor type.

        Captures a reference to the given executor and stores a pointer
        to the type-specific vtable. The executor must remain valid for
        the lifetime of this `executor_ref` instance.

        @param ex The executor to wrap. Must satisfy the `Executor`
                  concept. A pointer to this object is stored
                  internally; the executor must outlive this wrapper.
    */
#if defined(__GNUC__) && !defined(__clang__)
    // GCC constraint satisfaction caching bug workaround
    template<class Ex,
        std::enable_if_t<!std::is_same_v<
            std::decay_t<Ex>, executor_ref>, int> = 0>
#else
    template<class Ex>
        requires (!std::same_as<std::decay_t<Ex>, executor_ref>)
#endif
    executor_ref(Ex const& ex) noexcept
        : ex_(&ex)
        , vt_(&detail::vtable_for<Ex>)
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

    /** Returns a reference to the associated execution context.

        @return A reference to the execution context.

        @pre This instance was constructed with a valid executor.
    */
    execution_context& context() const noexcept
    {
        return vt_->context(ex_);
    }

    /** Informs the executor that work is beginning.

        Must be paired with a subsequent call to `on_work_finished()`.

        @pre This instance was constructed with a valid executor.
    */
    void on_work_started() const noexcept
    {
        vt_->on_work_started(ex_);
    }

    /** Informs the executor that work has completed.

        @pre A preceding call to `on_work_started()` was made.
        @pre This instance was constructed with a valid executor.
    */
    void on_work_finished() const noexcept
    {
        vt_->on_work_finished(ex_);
    }

    /** Dispatches a continuation through the wrapped executor.

        Returns a handle for symmetric transfer. If running in the
        executor's thread, returns `c.h`. Otherwise, posts the
        continuation for later execution and returns
        `std::noop_coroutine()`.

        @param c The continuation to dispatch for resumption.
                 Must remain at a stable address until dequeued.

        @return A handle for symmetric transfer or `std::noop_coroutine()`.

        @pre This instance was constructed with a valid executor.
    */
    std::coroutine_handle<> dispatch(continuation& c) const
    {
        return vt_->dispatch(ex_, c);
    }

    /** Posts a continuation to the wrapped executor.

        Posts the continuation to the executor for later execution
        and returns. The caller should transfer to `std::noop_coroutine()`
        after calling this.

        @param c The continuation to post for resumption.
                 Must remain at a stable address until dequeued.

        @pre This instance was constructed with a valid executor.
    */
    void post(continuation& c) const
    {
        vt_->post(ex_, c);
    }

    /** Compares two executor references for equality.

        Two `executor_ref` instances are equal if they wrap
        executors of the same type that compare equal.

        @param other The executor reference to compare against.

        @return `true` if both wrap equal executors of the same type.
    */
    bool operator==(executor_ref const& other) const noexcept
    {
        if (ex_ == other.ex_)
            return true;
        if (vt_ != other.vt_)
            return false;
        return vt_->equals(ex_, other.ex_);
    }

    /** Return a pointer to the wrapped executor if it matches
        the requested type.

        Performs a type check against the stored executor and
        returns a typed pointer when the types match, or
        `nullptr` otherwise. Analogous to
        `std::any_cast< Executor >( &a )`.

        @tparam Executor The executor type to retrieve.

        @return A pointer to the underlying executor, or
            `nullptr` if the type does not match.
    */
    template< typename Executor >
    const Executor* target() const
    {
        if ( *vt_->type_id == detail::type_id< Executor >() )
           return static_cast< Executor const* >( ex_ );
        return nullptr;
    }

    /// @copydoc target() const
    template< typename Executor>
    Executor* target()
    {
        if ( *vt_->type_id == detail::type_id< Executor >() )
           return const_cast< Executor* >(
               static_cast< Executor const* >( ex_ ));
        return nullptr;
    }
};

} // capy
} // boost

#endif
