//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
// Copyright (c) 2026 Michael Vandeberg
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_CONCEPT_IO_AWAITABLE_HPP
#define BOOST_CAPY_CONCEPT_IO_AWAITABLE_HPP

#include <boost/capy/detail/config.hpp>
#include <coroutine>
#include <boost/capy/ex/io_env.hpp>
#include <ranges>

namespace boost {
namespace capy {

namespace detail {

    template <typename T>
    constexpr bool is_coroutine_handle = false;

    template <typename T>
    constexpr bool is_coroutine_handle<std::coroutine_handle<T>> = true; 

    template <typename T>
    concept await_suspend_valid_result = std::same_as<T, void> || std::same_as<T, bool> || is_coroutine_handle<T>;
}

/** Describes types that can be `co_await`-ed in Capy-coroutines and 
    that can be passed the information about the execution environment.

    See the tutorial section _The IoAwaitable Protocol_ for the description
    of the execution environment propagation mechanism in Capy.



    @tparam A The awaitable type.

    @par `await_ready`

    In the context of processing a `co_await` expression, says
    if the expression can be computed synchronously, without engaging
    the further awaiting machinery.

    _Returns_: 

    @li @c true when the operation can be computed synchronously via immediately calling
    @c await_resume ,
    @li @c false when @c await_suspend needs to be called. 

    @par `await_suspend`

    In the context of processing a `co_await` expression, instructs the coroutine machinery,
    which coroutine needs to be launched or resumed.

    _preconditions_: `a.await_ready() == false`.

    _Parameters_:

    @li @c h — the handle to the just suspended coroutine,
    @li @c env — the execution environment of the just suspended coroutine. 

    _Effects_: If this operations instructs a coroutine to be resumed, it shall make sure that the coroutine's 
    promise type is passed the @c env parameter, in a manner specific to `A`.

    _Returns_: The signature has one of the these return types: `void`, `bool` and `std::coroutine_handle<P>` for any type `P`.

    If the return type is `void`, instructs the coroutine machinery that the control shall be 
    returned to the resumer of the coroutine that invoked the `co_await` expression. 
    Takes the ownership for scheduling the
    resumption of the coroutine represented by `h` via either  `env->executor.post` or `env->executor.dispatch`.

    If the return type is `bool`:

    @li value @c true indicates the behavior equivalent to that of the @c void return type;

    @li value @c false instructs the coroutine machinery to resume the coroutine represented by @c h and to immediately invoke @c await_resume .

    If the return type is `std::coroutine_handle<P>`, instructs the compiler to resume 
    the coroutine represented by the returned handle, in a tail call manner (not consuming the stack). 

    _Note_: 
    
    @li Returning @c h is equivalent to returning @c false in the @c bool return type signature.
    @li Returning @c std::noop_coroutine() is equivalent to using the @c void return type, returning @c true in the @c bool return type signature.


    _Lifetime_: The object of type @ref io_env pointed to by `env` remains valid 
    for the duration of the async operation represented by `A`. This is guarantee is maintained
    by @ref run, @ref run_async and the other functions that "start a task".
    

    @par `await_resume`

    In the context of processing a `co_await` expression, when the suspended coroutine is being resumed or
    upon immediate resumption, returns a value — if any — that shall be returned from the `co_await` expression.

    If it throws an exception, the exception is propagated out of the `co_await` expression into the awaiting coroutine's
    scope.

    _Returns_: value intended to be returned from the `co_await` expression. The return type determines the type of
    the enclosing `co_await` expression and can be `void`.
    
    


    @par Example

    The example demonstrates a "leaf" awaitable: one that is associated directly with 
    a system's I/O operation but no coroutine.

    @par !example example

    @par Models

    General-purpose class templates that model `IoAwaitable`: @ref task, @ref quitter, @ref immediate.


    @see @ref IoRunnable, @ref io_env, @ref executor_ref
*/
template<typename A>
concept IoAwaitable = std::move_constructible<A> &&
    requires(
        A a,
        std::coroutine_handle<> h,
        io_env const* env)
    {
        { a.await_ready() } -> std::same_as<bool>;
        { a.await_suspend(h, env) } -> detail::await_suspend_valid_result;   
        a.await_resume();
    };

/** Names what `co_await a` yields for awaitable type A.

    Given an awaitable A, yields the type returned by A::await_resume().

    @tparam A The awaitable type.
*/
template<typename A>
using awaitable_result_t = decltype(std::declval<std::decay_t<A>&>().await_resume());

/** Requires a sized input range whose value type satisfies `IoAwaitable`.

    A range satisfies `IoAwaitableRange` if it is a sized input range
    whose value type satisfies @ref IoAwaitable.

    @tparam R The range type.
*/
template<typename R>
concept IoAwaitableRange =
    std::ranges::input_range<R> &&
    std::ranges::sized_range<R> &&
    IoAwaitable<std::ranges::range_value_t<R>>;

} // namespace capy
} // namespace boost

#endif
