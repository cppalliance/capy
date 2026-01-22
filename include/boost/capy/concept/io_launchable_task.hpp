//
// Copyright (c) 2025 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_CONCEPT_IO_LAUNCHABLE_TASK_HPP
#define BOOST_CAPY_CONCEPT_IO_LAUNCHABLE_TASK_HPP

#include <boost/capy/detail/config.hpp>
#include <boost/capy/concept/io_awaitable_task.hpp>

#include <coroutine>
#include <exception>
#include <type_traits>

namespace boost {
namespace capy {

/** Concept for launchable I/O task types.

    A task satisfies `IoLaunchableTask` if it satisfies @ref IoAwaitableTask
    and provides the additional interface needed by launch utilities like
    `run_async` and `run_on`:

    @li `handle()` — returns the typed coroutine handle
    @li `release()` — releases ownership (task won't destroy frame)
    @li `exception()` — returns stored exception_ptr from the promise
    @li `result()` — returns stored result from the promise (non-void tasks)

    This concept formalizes the contract for launching tasks from
    non-coroutine contexts.

    @tparam T The task type.

    @par Requirements
    @li `T` must satisfy @ref IoAwaitableTask
    @li `T::handle()` returns `std::coroutine_handle<promise_type>`
    @li `T::release()` releases ownership without returning the handle
    @li `T::promise_type::exception()` returns the stored exception
    @li `T::promise_type::result()` returns the result (for non-void tasks)

    @see IoAwaitableTask
    @see run_async
    @see run_on
*/
template<typename T>
concept IoLaunchableTask =
    IoAwaitableTask<T> &&
    requires(T& t, T const& ct, typename T::promise_type const& cp)
    {
        { ct.handle() } noexcept -> std::same_as<std::coroutine_handle<typename T::promise_type>>;
        { t.release() } noexcept;
        { cp.exception() } noexcept -> std::same_as<std::exception_ptr>;
    } &&
    (std::is_void_v<decltype(std::declval<T&>().await_resume())> ||
     requires(typename T::promise_type& p) {
         p.result();
     });

} // namespace capy
} // namespace boost

#endif
