//
// Copyright (c) 2025 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_ASYNC_RESULT_HPP
#define BOOST_CAPY_ASYNC_RESULT_HPP

#include <boost/capy/detail/config.hpp>

#ifdef BOOST_CAPY_HAS_CORO

#include <concepts>
#include <coroutine>
#include <exception>
#include <functional>
#include <memory>
#include <variant>

namespace boost {
namespace capy {

//-----------------------------------------------------------------------------
//
// Concepts
//
//-----------------------------------------------------------------------------

/** Concept for a deferred operation that produces a value.

    A deferred operation is a callable that accepts a completion
    handler. When invoked, it initiates an asynchronous operation
    and calls the handler with the result when complete.

    @tparam Op The operation type.
    @tparam T The result type.
*/
template<class Op, class T>
concept deferred_operation = std::invocable<Op, std::function<void(T)>>;

/** Concept for a deferred operation that produces no value.

    A void deferred operation accepts a completion handler that
    takes no arguments.

    @tparam Op The operation type.
*/
template<class Op>
concept void_deferred_operation = std::invocable<Op, std::function<void()>>;

//-----------------------------------------------------------------------------

/** An awaitable wrapper for callback-based asynchronous operations.

    This class template provides a bridge between traditional
    callback-based asynchronous APIs and C++20 coroutines. It
    wraps a deferred operation and makes it awaitable, allowing
    seamless integration with coroutine-based code.

    @par Thread Safety
    Distinct objects may be accessed concurrently. Shared objects
    require external synchronization.

    @par Example
    @code
    // Wrap a callback-based timer
    async_result<void> async_sleep(std::chrono::milliseconds ms)
    {
        return make_async_result<void>(
            [ms](auto&& handler) {
                // Start timer, call handler when done
                start_timer(ms, std::move(handler));
            });
    }

    task<void> example()
    {
        co_await async_sleep(std::chrono::milliseconds(100));
    }
    @endcode

    @tparam T The type of value produced by the asynchronous operation.

    @see make_async_result, task
*/
template<class T>
class async_result
{
public:
    /** Abstract base class for operation implementations.

        Derived classes implement the actual asynchronous operation
        and result retrieval logic.
    */
    struct impl_base
    {
        /// Virtual destructor.
        virtual ~impl_base() = default;

        /** Start the asynchronous operation.

            @param on_done Callback to invoke when the operation completes.
        */
        virtual void start(std::function<void()> on_done) = 0;

        /** Retrieve the operation result.

            @return The result value.

            @throws Any exception stored during the operation.
        */
        virtual T get_result() = 0;
    };

private:
    std::unique_ptr<impl_base> impl_;

public:
    /** Construct from an implementation.

        @param p Unique pointer to the operation implementation.
    */
    explicit async_result(std::unique_ptr<impl_base> p) : impl_(std::move(p)) {}

    /// Default move constructor.
    async_result(async_result&&) = default;

    /// Default move assignment operator.
    async_result& operator=(async_result&&) = default;

    /** Check if the result is ready.

        @return Always returns false; the operation must be started.
    */
    bool await_ready() const noexcept { return false; }

    /** Suspend the caller and start the operation.

        Initiates the asynchronous operation and arranges for
        the caller to be resumed when it completes.

        @param h The coroutine handle of the awaiting coroutine.
    */
    void await_suspend(std::coroutine_handle<> h)
    {
        impl_->start([h]{ h.resume(); });
    }

    /** Retrieve the result after completion.

        @return The value produced by the asynchronous operation.

        @throws Any exception that occurred during the operation.
    */
    [[nodiscard]]
    T await_resume()
    {
        return impl_->get_result();
    }
};

//-----------------------------------------------------------------------------

/** An awaitable wrapper for callback-based operations with no result.

    This specialization of async_result is used for asynchronous
    operations that signal completion but do not produce a value,
    such as timers, write operations, or connection establishment.

    @par Thread Safety
    Distinct objects may be accessed concurrently. Shared objects
    require external synchronization.

    @par Example
    @code
    // Wrap a callback-based timer
    async_result<void> async_sleep(std::chrono::milliseconds ms)
    {
        return make_async_result<void>(
            [ms](auto handler) {
                start_timer(ms, [h = std::move(handler)]{ h(); });
            });
    }

    task<void> example()
    {
        co_await async_sleep(std::chrono::milliseconds(100));
    }
    @endcode

    @see async_result, make_async_result
*/
template<>
class async_result<void>
{
public:
    /** Abstract base class for void operation implementations.

        Derived classes implement the actual asynchronous operation
        and exception handling.
    */
    struct impl_base
    {
        /// Virtual destructor.
        virtual ~impl_base() = default;

        /** Start the asynchronous operation.

            @param on_done Callback to invoke when the operation completes.
        */
        virtual void start(std::function<void()> on_done) = 0;

        /** Check for and rethrow any stored exception.

            @throws Any exception stored during the operation.
        */
        virtual void get_result() = 0;
    };

private:
    std::unique_ptr<impl_base> impl_;

public:
    /** Construct from an implementation.

        @param p Unique pointer to the operation implementation.
    */
    explicit async_result(std::unique_ptr<impl_base> p) : impl_(std::move(p)) {}

    /// Default move constructor.
    async_result(async_result&&) = default;

    /// Default move assignment operator.
    async_result& operator=(async_result&&) = default;

    /** Check if the result is ready.

        @return Always returns false; the operation must be started.
    */
    bool await_ready() const noexcept { return false; }

    /** Suspend the caller and start the operation.

        Initiates the asynchronous operation and arranges for
        the caller to be resumed when it completes.

        @param h The coroutine handle of the awaiting coroutine.
    */
    void await_suspend(std::coroutine_handle<> h)
    {
        impl_->start([h]{ h.resume(); });
    }

    /** Complete the await and check for exceptions.

        @throws Any exception that occurred during the operation.
    */
    void await_resume()
    {
        impl_->get_result();
    }
};

//-----------------------------------------------------------------------------

/** Default implementation of async_result::impl_base.

    This class template wraps a deferred operation callable and
    manages the result storage.

    @tparam T The result type.
    @tparam DeferredOp The callable type that initiates the operation.
*/
template<class T, class DeferredOp>
struct async_result_impl : capy::async_result<T>::impl_base
{
    /// The deferred operation callable.
    DeferredOp op_;

    /// Storage for exception or result value.
    std::variant<std::exception_ptr, T> result_{};

    /** Construct from a deferred operation.

        @param op The callable that initiates the asynchronous operation.
                  It will be invoked with a completion handler that
                  accepts the result arguments.
    */
    explicit async_result_impl(DeferredOp&& op)
        : op_(std::forward<DeferredOp>(op))
    {
    }

    /** Start the operation.

        Invokes the deferred operation with a handler that stores
        the result and calls the completion callback.

        @param on_done Callback to invoke when complete.
    */
    void start(std::function<void()> on_done) override
    {
        std::move(op_)(
            [this, on_done = std::move(on_done)](auto&&... args) mutable
            {
                result_.template emplace<1>(T{std::forward<decltype(args)>(args)...});
                on_done();
            });
    }

    /** Retrieve the result.

        @return The result value.

        @throws Any stored exception.
    */
    T get_result() override
    {
        if (result_.index() == 0 && std::get<0>(result_))
            std::rethrow_exception(std::get<0>(result_));
        return std::move(std::get<1>(result_));
    }
};

//-----------------------------------------------------------------------------

/** Implementation of async_result<void>::impl_base.

    This specialization wraps a deferred operation that produces
    no result value.

    @tparam DeferredOp The callable type that initiates the operation.
*/
template<class DeferredOp>
struct async_result_void_impl : capy::async_result<void>::impl_base
{
    /// The deferred operation callable.
    DeferredOp op_;

    /// Storage for an exception, if one occurred.
    std::exception_ptr exception_{};

    /** Construct from a deferred operation.

        @param op The callable that initiates the asynchronous operation.
                  It will be invoked with a completion handler that
                  takes no arguments.
    */
    explicit async_result_void_impl(DeferredOp&& op)
        : op_(std::forward<DeferredOp>(op))
    {
    }

    /** Start the operation.

        Invokes the deferred operation with a handler that signals
        completion and calls the done callback.

        @param on_done Callback to invoke when complete.
    */
    void start(std::function<void()> on_done) override
    {
        std::move(op_)(std::move(on_done));
    }

    /** Check for exceptions.

        @throws Any stored exception.
    */
    void get_result() override
    {
        if (exception_)
            std::rethrow_exception(exception_);
    }
};

//-----------------------------------------------------------------------------

/** Create an async_result from a deferred operation.

    This factory function creates an awaitable async_result that
    wraps a callback-based asynchronous operation.

    @par Example
    @code
    async_result<std::string> async_read()
    {
        return make_async_result<std::string>(
            [](auto handler) {
                // Simulate async read
                handler("Hello, World!");
            });
    }
    @endcode

    @tparam T The result type of the asynchronous operation.
    @tparam DeferredOp The type of the deferred operation callable.

    @param op A callable that accepts a completion handler. When invoked,
              it should initiate the asynchronous operation and call the
              handler with the result when complete.

    @return An async_result that can be awaited in a coroutine.

    @see async_result
*/
template<class T, class DeferredOp>
    requires (!std::is_void_v<T>)
[[nodiscard]]
capy::async_result<T>
make_async_result(DeferredOp&& op)
{
    using impl_type = async_result_impl<T, std::decay_t<DeferredOp>>;
    return capy::async_result<T>(
        std::make_unique<impl_type>(std::forward<DeferredOp>(op)));
}

/** Create an async_result<void> from a deferred operation.

    This overload is used for operations that signal completion
    without producing a value.

    @par Example
    @code
    async_result<void> async_wait(int milliseconds)
    {
        return make_async_result<void>(
            [milliseconds](auto on_done) {
                // Start timer, call on_done() when elapsed
                start_timer(milliseconds, std::move(on_done));
            });
    }
    @endcode

    @tparam DeferredOp The type of the deferred operation callable.

    @param op A callable that accepts a completion handler taking no
              arguments. When invoked, it should initiate the operation
              and call the handler when complete.

    @return An async_result<void> that can be awaited in a coroutine.

    @see async_result
*/
template<class T, class DeferredOp>
    requires std::is_void_v<T>
[[nodiscard]]
capy::async_result<void>
make_async_result(DeferredOp&& op)
{
    using impl_type = async_result_void_impl<std::decay_t<DeferredOp>>;
    return capy::async_result<void>(
        std::make_unique<impl_type>(std::forward<DeferredOp>(op)));
}

} // capy
} // boost

#endif

#endif
