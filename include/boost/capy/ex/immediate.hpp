//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
// Copyright (c) 2026 Michael Vandeberg
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_EX_IMMEDIATE_HPP
#define BOOST_CAPY_EX_IMMEDIATE_HPP

#include <boost/capy/detail/config.hpp>
#include <boost/capy/ex/await_context.hpp>
#include <boost/capy/results.hpp>

#include <coroutine>
#include <stop_token>
#include <utility>

namespace boost {
namespace capy {

/** Complete a `co_await` at once with a stored value.

    This awaitable wraps a synchronous result so it can be used in
    contexts that require an awaitable type. `await_ready()` always
    returns `true`, so the awaiting coroutine never suspends.

    Use this to adapt synchronous operations to satisfy async concepts
    like @ref ContextAwaitable without the overhead of a full coroutine frame.

    @par Example
    @par !example example_1


    @par Building synchronous I/O operations
    @par !example example_2


    @tparam T The result type to wrap.

    @see @ref ready, @ref results
*/
template<class T>
struct immediate
{
    /** The wrapped value. */
    T value_;

    /** Check readiness; always `true`.

        The awaiting coroutine does not suspend, and `await_suspend` is
        never called.

        @return Always `true`.
    */
    constexpr bool
    await_ready() const noexcept
    {
        return true;
    }

    /** Satisfy @ref ContextAwaitable; never called, since `await_ready` is `true`.

        This overload allows `immediate` to satisfy the @ref ContextAwaitable
        concept. Since the result is already available, the environment
        is unused. If it were called, the returned `std::noop_coroutine()`
        would leave the awaiting coroutine suspended with nothing to
        resume it.

        @param h The coroutine handle (unused).
        @param env The execution environment (unused).

        @return `std::noop_coroutine()`.
    */
    std::coroutine_handle<>
    await_suspend(
        std::coroutine_handle<> h,
        await_context const* env) const noexcept
    {
        (void)h;
        (void)env;
        return std::noop_coroutine();
    }

    /** Return the wrapped value.

        @note A move constructor of `T` that throws calls `std::terminate`.

        @return The stored value, moved out.
    */
    [[nodiscard]] constexpr T
    await_resume() noexcept
    {
        return std::move(value_);
    }

    /** Return the wrapped value (const overload).

        @return A reference to the stored value. Nothing is moved, so the
        reference is valid only while the `immediate` is alive.
    */
    [[nodiscard]] constexpr T const&
    await_resume() const noexcept
    {
        return value_;
    }
};

/** Create an immediate awaitable for a successful `results`.

    This helper creates an @ref immediate wrapping a @ref results
    with no error and the provided values.

    @par Example
    @par !example example_1


    @return An immediate awaitable containing a successful `results`.

    @see @ref immediate, @ref results
*/
inline
immediate<results<>>
ready() noexcept
{
    return {{}};
}

/** Create an immediate awaitable for a successful `results` with one value.

    @par Exception Safety
    Propagates any exception from copying or moving the arguments.

    @param t1 The result value.

    @return An immediate awaitable containing `results<T1>{std::error_code(), t1}`.
*/
template<class T1>
immediate<results<T1>>
ready(T1 t1)
{
    return {{std::error_code(), std::move(t1)}};
}

/** Create an immediate awaitable for a successful `results` with two values.

    @par Exception Safety
    Propagates any exception from copying or moving the arguments.

    @param t1 The first result value.
    @param t2 The second result value.

    @return An immediate awaitable containing `results<T1,T2>{std::error_code(), t1, t2}`.
*/
template<class T1, class T2>
immediate<results<T1, T2>>
ready(T1 t1, T2 t2)
{
    return {{std::error_code(), std::move(t1), std::move(t2)}};
}

/** Create an immediate awaitable for a successful `results` with three values.

    @par Exception Safety
    Propagates any exception from copying or moving the arguments.

    @param t1 The first result value.
    @param t2 The second result value.
    @param t3 The third result value.

    @return An immediate awaitable containing `results<T1,T2,T3>{std::error_code(), t1, t2, t3}`.
*/
template<class T1, class T2, class T3>
immediate<results<T1, T2, T3>>
ready(T1 t1, T2 t2, T3 t3)
{
    return {{std::error_code(), std::move(t1), std::move(t2), std::move(t3)}};
}

/** Create an immediate awaitable for a failed `results`.

    This helper creates an @ref immediate wrapping a @ref results
    with an error code.

    @par Example
    @par !example example_2


    @param ec The error code.

    @return An immediate awaitable containing a failed `results`.

    @see @ref immediate, @ref results
*/
inline
immediate<results<>>
ready(std::error_code ec) noexcept
{
    return {{ec}};
}

/** Create an immediate awaitable for a `results` with error and one value.

    @par Exception Safety
    Propagates any exception from copying or moving the arguments.

    @param ec The error code.
    @param t1 The result value.

    @return An immediate awaitable containing `results<T1>{ec, t1}`.
*/
template<class T1>
immediate<results<T1>>
ready(std::error_code ec, T1 t1)
{
    return {{ec, std::move(t1)}};
}

/** Create an immediate awaitable for a `results` with error and two values.

    @par Exception Safety
    Propagates any exception from copying or moving the arguments.

    @param ec The error code.
    @param t1 The first result value.
    @param t2 The second result value.

    @return An immediate awaitable containing `results<T1,T2>{ec, t1, t2}`.
*/
template<class T1, class T2>
immediate<results<T1, T2>>
ready(std::error_code ec, T1 t1, T2 t2)
{
    return {{ec, std::move(t1), std::move(t2)}};
}

/** Create an immediate awaitable for a `results` with error and three values.

    @par Exception Safety
    Propagates any exception from copying or moving the arguments.

    @param ec The error code.
    @param t1 The first result value.
    @param t2 The second result value.
    @param t3 The third result value.

    @return An immediate awaitable containing `results<T1,T2,T3>{ec, t1, t2, t3}`.
*/
template<class T1, class T2, class T3>
immediate<results<T1, T2, T3>>
ready(std::error_code ec, T1 t1, T2 t2, T3 t3)
{
    return {{ec, std::move(t1), std::move(t2), std::move(t3)}};
}

} // namespace capy
} // namespace boost

#endif
