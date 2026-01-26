//
// Copyright (c) 2025 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_EX_IMMEDIATE_HPP
#define BOOST_CAPY_EX_IMMEDIATE_HPP

#include <boost/capy/detail/config.hpp>
#include <boost/capy/ex/executor_ref.hpp>
#include <boost/capy/io_result.hpp>

#include <coroutine>
#include <stop_token>
#include <utility>

namespace boost {
namespace capy {

/** An awaitable that completes immediately with a value.

    This awaitable wraps a synchronous result so it can be used in
    contexts that require an awaitable type. It never suspends - 
    `await_ready()` always returns `true`, so the coroutine machinery
    is optimized away by the compiler.

    Use this to adapt synchronous operations to satisfy async concepts
    like @ref IoAwaitable without the overhead of a full coroutine frame.

    @tparam T The result type to wrap.

    @par Example
    @code
    // Wrap a sync operation as an awaitable
    immediate<int> get_value()
    {
        return {42};
    }

    task<void> example()
    {
        int x = co_await get_value();  // No suspension, returns 42
    }
    @endcode

    @par Satisfying WriteSink with sync operations
    @code
    struct my_sync_sink
    {
        template<ConstBufferSequence CB>
        immediate<io_result<std::size_t>>
        write(CB buffers)
        {
            auto n = process_sync(buffers);
            return {{{}, n}};
        }

        immediate<io_result<>>
        write_eof()
        {
            return {{}};
        }
    };
    @endcode

    @see ready, io_result
*/
template<class T>
struct immediate
{
    /** The wrapped value. */
    T value_;

    /** Always returns true - this awaitable never suspends. */
    constexpr bool
    await_ready() const noexcept
    {
        return true;
    }

    /** Never called since await_ready() returns true. */
    constexpr void
    await_suspend(std::coroutine_handle<>) const noexcept
    {
    }

    /** IoAwaitable protocol overload.

        This overload allows `immediate` to satisfy the @ref IoAwaitable
        concept. Since the result is already available, the executor and
        stop token are unused.

        @param h The coroutine handle (unused).
        @param ex The executor (unused).
        @param token The stop token (unused).

        @return `std::noop_coroutine()` to indicate no suspension.
    */
    std::coroutine_handle<>
    await_suspend(
        std::coroutine_handle<> h,
        executor_ref ex,
        std::stop_token token = {}) const noexcept
    {
        (void)h;
        (void)ex;
        (void)token;
        return std::noop_coroutine();
    }

    /** Returns the wrapped value.

        @return The stored value, moved if non-const.
    */
    constexpr T
    await_resume() noexcept
    {
        return std::move(value_);
    }

    /** Returns the wrapped value (const overload). */
    constexpr T const&
    await_resume() const noexcept
    {
        return value_;
    }
};

//----------------------------------------------------------

/** Create an immediate awaitable for a successful io_result.

    This helper creates an @ref immediate wrapping an @ref io_result
    with no error and the provided values.

    @par Example
    @code
    immediate<io_result<std::size_t>>
    write(const_buffer buf)
    {
        auto n = write_sync(buf);
        return ready(n);  // success with n bytes
    }

    immediate<io_result<>>
    connect()
    {
        connect_sync();
        return ready();  // void success
    }
    @endcode

    @return An immediate awaitable containing a successful io_result.

    @see immediate, io_result
*/
inline
immediate<io_result<>>
ready() noexcept
{
    return {{}};
}

/** Create an immediate awaitable for a successful io_result with one value.

    @param t1 The result value.

    @return An immediate awaitable containing `io_result<T1>{{}, t1}`.
*/
template<class T1>
immediate<io_result<T1>>
ready(T1 t1)
{
    return {{{}, std::move(t1)}};
}

/** Create an immediate awaitable for a successful io_result with two values.

    @param t1 The first result value.
    @param t2 The second result value.

    @return An immediate awaitable containing `io_result<T1,T2>{{}, t1, t2}`.
*/
template<class T1, class T2>
immediate<io_result<T1, T2>>
ready(T1 t1, T2 t2)
{
    return {{{}, std::move(t1), std::move(t2)}};
}

/** Create an immediate awaitable for a successful io_result with three values.

    @param t1 The first result value.
    @param t2 The second result value.
    @param t3 The third result value.

    @return An immediate awaitable containing `io_result<T1,T2,T3>{{}, t1, t2, t3}`.
*/
template<class T1, class T2, class T3>
immediate<io_result<T1, T2, T3>>
ready(T1 t1, T2 t2, T3 t3)
{
    return {{{}, std::move(t1), std::move(t2), std::move(t3)}};
}

//----------------------------------------------------------

/** Create an immediate awaitable for a failed io_result.

    This helper creates an @ref immediate wrapping an @ref io_result
    with an error code.

    @par Example
    @code
    immediate<io_result<std::size_t>>
    write(const_buffer buf)
    {
        auto ec = write_sync(buf);
        if(ec.failed())
            return ready(ec, std::size_t{0});
        return ready(buffer_size(buf));
    }
    @endcode

    @param ec The error code.

    @return An immediate awaitable containing a failed io_result.

    @see immediate, io_result
*/
inline
immediate<io_result<>>
ready(system::error_code ec) noexcept
{
    return {{ec}};
}

/** Create an immediate awaitable for an io_result with error and one value.

    @param ec The error code.
    @param t1 The result value.

    @return An immediate awaitable containing `io_result<T1>{ec, t1}`.
*/
template<class T1>
immediate<io_result<T1>>
ready(system::error_code ec, T1 t1)
{
    return {{ec, std::move(t1)}};
}

/** Create an immediate awaitable for an io_result with error and two values.

    @param ec The error code.
    @param t1 The first result value.
    @param t2 The second result value.

    @return An immediate awaitable containing `io_result<T1,T2>{ec, t1, t2}`.
*/
template<class T1, class T2>
immediate<io_result<T1, T2>>
ready(system::error_code ec, T1 t1, T2 t2)
{
    return {{ec, std::move(t1), std::move(t2)}};
}

/** Create an immediate awaitable for an io_result with error and three values.

    @param ec The error code.
    @param t1 The first result value.
    @param t2 The second result value.
    @param t3 The third result value.

    @return An immediate awaitable containing `io_result<T1,T2,T3>{ec, t1, t2, t3}`.
*/
template<class T1, class T2, class T3>
immediate<io_result<T1, T2, T3>>
ready(system::error_code ec, T1 t1, T2 t2, T3 t3)
{
    return {{ec, std::move(t1), std::move(t2), std::move(t3)}};
}

} // namespace capy
} // namespace boost

#endif
