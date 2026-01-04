//
// Copyright (c) 2025 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_ASYNC_OP_HPP
#define BOOST_CAPY_ASYNC_OP_HPP

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
namespace detail {

template<class T>
struct async_op_impl_base
{
    virtual ~async_op_impl_base() = default;
    virtual void start(std::function<void()> on_done) = 0;
    virtual T get_result() = 0;
};

struct async_op_void_impl_base
{
    virtual ~async_op_void_impl_base() = default;
    virtual void start(std::function<void()> on_done) = 0;
    virtual void get_result() = 0;
};

template<class T, class DeferredOp>
struct async_op_impl : async_op_impl_base<T>
{
    DeferredOp op_;
    std::variant<std::exception_ptr, T> result_{};

    explicit
    async_op_impl(DeferredOp&& op)
        : op_(std::forward<DeferredOp>(op))
    {
    }

    void
    start(std::function<void()> on_done) override
    {
        std::move(op_)(
            [this, on_done = std::move(on_done)](auto&&... args) mutable
            {
                result_.template emplace<1>(T{std::forward<decltype(args)>(args)...});
                on_done();
            });
    }

    T
    get_result() override
    {
        if (result_.index() == 0 && std::get<0>(result_))
            std::rethrow_exception(std::get<0>(result_));
        return std::move(std::get<1>(result_));
    }
};

template<class DeferredOp>
struct async_op_void_impl : async_op_void_impl_base
{
    DeferredOp op_;
    std::exception_ptr exception_{};

    explicit
    async_op_void_impl(DeferredOp&& op)
        : op_(std::forward<DeferredOp>(op))
    {
    }

    void
    start(std::function<void()> on_done) override
    {
        std::move(op_)(std::move(on_done));
    }

    void
    get_result() override
    {
        if (exception_)
            std::rethrow_exception(exception_);
    }
};

} // detail

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
    async_op<void> async_sleep(std::chrono::milliseconds ms)
    {
        return make_async_op<void>(
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

    @see make_async_op, task
*/
template<class T>
class async_op
{
    std::unique_ptr<detail::async_op_impl_base<T>> impl_;

// Workaround: clang fails to match friend function template declarations
#if defined(__clang__) && (__clang_major__ == 16 || \
    (defined(__apple_build_version__) && __apple_build_version__ >= 15000000))
public:
#endif
    explicit
    async_op(std::unique_ptr<detail::async_op_impl_base<T>> p)
        : impl_(std::move(p))
    {
    }
#if defined(__clang__) && (__clang_major__ == 16 || \
    (defined(__apple_build_version__) && __apple_build_version__ >= 15000000))
private:
#endif

    template<class U, class DeferredOp>
        requires (!std::is_void_v<U>)
    friend async_op<U>
    make_async_op(DeferredOp&& op);

public:
    /** Return whether the result is ready.

        @return Always returns false; the operation must be started.
    */
    bool
    await_ready() const noexcept
    {
        return false;
    }

    /** Suspend the caller and start the operation.

        Initiates the asynchronous operation and arranges for
        the caller to be resumed when it completes.

        @param h The coroutine handle of the awaiting coroutine.
    */
    void
    await_suspend(std::coroutine_handle<> h)
    {
        impl_->start([h]{ h.resume(); });
    }

    /** Suspend the caller with scheduler affinity.

        Initiates the asynchronous operation and arranges for
        the caller to be resumed through the dispatcher when
        it completes, maintaining scheduler affinity.

        @param h The coroutine handle of the awaiting coroutine.
        @param dispatcher The dispatcher to resume through.
    */
    template<typename Dispatcher>
    void
    await_suspend(std::coroutine_handle<> h, Dispatcher& dispatcher)
    {
        impl_->start([h, &dispatcher]{ dispatcher(h); });
    }

    /** Return the result after completion.

        @return The value produced by the asynchronous operation.

        @throws Any exception that occurred during the operation.
    */
    [[nodiscard]]
    T
    await_resume()
    {
        return impl_->get_result();
    }
};

//-----------------------------------------------------------------------------

/** An awaitable wrapper for callback-based operations with no result.

    This specialization of async_op is used for asynchronous
    operations that signal completion but do not produce a value,
    such as timers, write operations, or connection establishment.

    @par Thread Safety
    Distinct objects may be accessed concurrently. Shared objects
    require external synchronization.

    @par Example
    @code
    // Wrap a callback-based timer
    async_op<void> async_sleep(std::chrono::milliseconds ms)
    {
        return make_async_op<void>(
            [ms](auto handler) {
                start_timer(ms, [h = std::move(handler)]{ h(); });
            });
    }

    task<void> example()
    {
        co_await async_sleep(std::chrono::milliseconds(100));
    }
    @endcode

    @see async_op, make_async_op
*/
template<>
class async_op<void>
{
    std::unique_ptr<detail::async_op_void_impl_base> impl_;

// Workaround: clang fails to match friend function template declarations
#if defined(__clang__) && (__clang_major__ == 16 || \
    (defined(__apple_build_version__) && __apple_build_version__ >= 15000000))
public:
#endif
    explicit
    async_op(std::unique_ptr<detail::async_op_void_impl_base> p)
        : impl_(std::move(p))
    {
    }
#if defined(__clang__) && (__clang_major__ == 16 || \
    (defined(__apple_build_version__) && __apple_build_version__ >= 15000000))
private:
#endif

    template<class U, class DeferredOp>
        requires std::is_void_v<U>
    friend async_op<void>
    make_async_op(DeferredOp&& op);

public:
    /** Return whether the result is ready.

        @return Always returns false; the operation must be started.
    */
    bool
    await_ready() const noexcept
    {
        return false;
    }

    /** Suspend the caller and start the operation.

        Initiates the asynchronous operation and arranges for
        the caller to be resumed when it completes.

        @param h The coroutine handle of the awaiting coroutine.
    */
    void
    await_suspend(std::coroutine_handle<> h)
    {
        impl_->start([h]{ h.resume(); });
    }

    /** Suspend the caller with scheduler affinity.

        Initiates the asynchronous operation and arranges for
        the caller to be resumed through the dispatcher when
        it completes, maintaining scheduler affinity.

        @param h The coroutine handle of the awaiting coroutine.
        @param dispatcher The dispatcher to resume through.
    */
    template<typename Dispatcher>
    void
    await_suspend(std::coroutine_handle<> h, Dispatcher& dispatcher)
    {
        impl_->start([h, &dispatcher]{ dispatcher(h); });
    }

    /** Complete the await and check for exceptions.

        @throws Any exception that occurred during the operation.
    */
    void
    await_resume()
    {
        impl_->get_result();
    }
};

//-----------------------------------------------------------------------------

/** Return an async_op from a deferred operation.

    This factory function creates an awaitable async_op that
    wraps a callback-based asynchronous operation.

    @par Example
    @code
    async_op<std::string> async_read()
    {
        return make_async_op<std::string>(
            [](auto handler) {
                // Simulate async read
                handler("Hello, World!");
            });
    }
    @endcode

    @tparam T The result type of the asynchronous operation.

    @param op A callable that accepts a completion handler. When invoked,
              it should initiate the asynchronous operation and call the
              handler with the result when complete.

    @return An async_op that can be awaited in a coroutine.

    @see async_op
*/
template<class T, class DeferredOp>
    requires (!std::is_void_v<T>)
[[nodiscard]]
async_op<T>
make_async_op(DeferredOp&& op)
{
    using impl_type = detail::async_op_impl<T, std::decay_t<DeferredOp>>;
    return async_op<T>(
        std::make_unique<impl_type>(std::forward<DeferredOp>(op)));
}

/** Return an async_op<void> from a deferred operation.

    This overload is used for operations that signal completion
    without producing a value.

    @par Example
    @code
    async_op<void> async_wait(int milliseconds)
    {
        return make_async_op<void>(
            [milliseconds](auto on_done) {
                // Start timer, call on_done() when elapsed
                start_timer(milliseconds, std::move(on_done));
            });
    }
    @endcode

    @param op A callable that accepts a completion handler taking no
              arguments. When invoked, it should initiate the operation
              and call the handler when complete.

    @return An async_op<void> that can be awaited in a coroutine.

    @see async_op
*/
template<class T, class DeferredOp>
    requires std::is_void_v<T>
[[nodiscard]]
async_op<void>
make_async_op(DeferredOp&& op)
{
    using impl_type = detail::async_op_void_impl<std::decay_t<DeferredOp>>;
    return async_op<void>(
        std::make_unique<impl_type>(std::forward<DeferredOp>(op)));
}

} // capy
} // boost

#endif

#endif
