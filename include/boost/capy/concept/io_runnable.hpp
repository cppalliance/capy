//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_CONCEPT_IO_RUNNABLE_HPP
#define BOOST_CAPY_CONCEPT_IO_RUNNABLE_HPP

#include <boost/capy/detail/config.hpp>
#include <boost/capy/concept/io_awaitable.hpp>
#include <boost/capy/ex/io_env.hpp>

#include <coroutine>
#include <exception>
#include <type_traits>

namespace boost {
namespace capy {

/** Concept for task types that can be launched from non-coroutine contexts.

    Extends @ref IoAwaitable with operations needed by launch utilities
    (@ref run, @ref run_async) to start a task, transfer ownership of the
    coroutine frame, and retrieve results or exceptions after completion.

    @tparam T The task type.

    @par Syntactic Requirements

    @li `T` must satisfy @ref IoAwaitable
    @li `T::promise_type` must be a valid type
    @li `t.handle()` returns `std::coroutine_handle<T::promise_type>`,
        must be `noexcept`
    @li `t.release()` releases ownership, must be `noexcept`
    @li `p.exception()` returns `std::exception_ptr`, must be `noexcept`
    @li `p.result()` returns the task result (required for non-void tasks)
    @li `p.set_continuation(h)` sets the continuation handle, must be `noexcept`
    @li `p.set_environment(env)` sets the execution environment, must be `noexcept`

    @par Semantic Requirements

    The `handle` operation provides access to the coroutine:

    @li Returns the typed coroutine handle for the task's frame
    @li The task retains ownership; destroying the task destroys the frame

    The `release` operation transfers ownership:

    @li After `release()`, destroying the task does not destroy the frame
    @li The caller becomes responsible for resuming and destroying the frame

    The `exception` operation retrieves failure state:

    @li Returns the exception stored by the promise if the coroutine
        completed with an unhandled exception
    @li Returns `nullptr` if no exception was thrown

    The `result` operation retrieves success state (non-void tasks):

    @li Returns the value passed to `co_return`
    @li Behavior is undefined if called when `exception()` is non-null

    The `set_continuation` operation establishes the continuation:

    @li Sets the coroutine handle to resume when this task reaches
        `final_suspend`
    @li Used by launch functions to wire the task back to the trampoline

    The `set_environment` operation establishes the execution environment:

    @li Sets the `io_env` pointer that propagates executor, stop token,
        and allocator through the coroutine chain
    @li The pointed-to `io_env` must outlive the coroutine

    @par Conforming Signatures

    @code
    class T
    {
    public:
        struct promise_type
        {
            std::exception_ptr exception() noexcept;
            R result();  // non-void tasks only
            void set_continuation(std::coroutine_handle<>) noexcept;
            void set_environment(io_env const*) noexcept;
        };

        std::coroutine_handle<promise_type> handle() const noexcept;
        void release() noexcept;
    };
    @endcode

    @see IoAwaitable, run, run_async
*/
template<typename T>
concept IoRunnable =
    IoAwaitable<T> &&
    requires { typename T::promise_type; } &&
    requires(T& t, T const& ct, typename T::promise_type const& cp, typename T::promise_type& p)
    {
        { ct.handle() } noexcept -> std::same_as<std::coroutine_handle<typename T::promise_type>>;
        { cp.exception() } noexcept -> std::same_as<std::exception_ptr>;
        { t.release() } noexcept;
        { p.set_continuation(std::coroutine_handle<>{}) } noexcept;
        { p.set_environment(static_cast<io_env const*>(nullptr)) } noexcept;
    } &&
    (std::is_void_v<decltype(std::declval<T&>().await_resume())> ||
     requires(typename T::promise_type& p) {
         p.result();
     });

} // namespace capy
} // namespace boost

#endif
