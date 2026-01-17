//
// Copyright (c) 2025 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_RUN_SYNC_HPP
#define BOOST_CAPY_RUN_SYNC_HPP

#include <boost/capy/detail/config.hpp>
#include <boost/capy/ex/any_coro.hpp>
#include <boost/capy/task.hpp>

#include <coroutine>
#include <exception>
#include <type_traits>
#include <utility>

namespace boost {
namespace capy {

namespace detail {

/** Trivial dispatcher for synchronous execution.

    Returns the coroutine handle directly for symmetric transfer,
    enabling inline execution without scheduling.
*/
struct sync_dispatcher
{
    any_coro operator()(any_coro h) const
    {
        return h;
    }
};

/** Synchronous task runner.

    Runs a coroutine task to completion on the caller's thread,
    returning the result directly or rethrowing any exception.

    This class is not intended for direct use. Use the `run_sync()`
    factory function instead.

    @par Thread Safety
    Not thread-safe. The task runs entirely on the calling thread.

    @see run_sync
*/
class sync_runner
{
public:
    sync_runner() = default;

    sync_runner(sync_runner const&) = delete;
    sync_runner& operator=(sync_runner const&) = delete;
    sync_runner(sync_runner&&) = default;
    sync_runner& operator=(sync_runner&&) = default;

    /** Run a task to completion and return the result.

        Executes the task synchronously on the calling thread. The task
        runs to completion before this function returns.

        @par Exception Safety
        If the task throws an exception, it is rethrown to the caller.

        @param t The task to execute.

        @return The value returned by the task.

        @throws Any exception thrown by the task.
    */
    template<typename T>
    T operator()(task<T> t) &&
    {
        auto h = t.release();
        sync_dispatcher d;

        h.promise().continuation_ = std::noop_coroutine();
        h.promise().ex_ = d;
        h.promise().caller_ex_ = d;
        h.promise().needs_dispatch_ = false;

        d(any_coro{h}).resume();

        std::exception_ptr ep = h.promise().ep_;

        if constexpr (std::is_void_v<T>)
        {
            h.destroy();
            if (ep)
                std::rethrow_exception(ep);
        }
        else
        {
            if (ep)
            {
                h.destroy();
                std::rethrow_exception(ep);
            }
            auto& result_base = static_cast<detail::task_return_base<T>&>(
                h.promise());
            auto result = std::move(*result_base.result_);
            h.destroy();
            return result;
        }
    }
};

} // namespace detail

/** Create a synchronous task runner.

    Returns a runner that executes a coroutine task to completion
    on the caller's thread. The task completes before this function
    returns, and the result is returned directly.

    @par Usage
    @code
    // Run a task and get the result
    int value = run_sync()(compute_value());

    // Run a void task
    run_sync()(do_work());

    // Exceptions propagate normally
    try {
        run_sync()(failing_task());
    } catch (std::exception const& e) {
        // handle error
    }
    @endcode

    @par Thread Safety
    The task runs entirely on the calling thread. No dispatcher or
    execution context is required.

    @return A runner object with `operator()(task<T>)` that returns `T`.

    @see task
    @see run_async
    @see run_on
*/
inline
detail::sync_runner
run_sync()
{
    return detail::sync_runner{};
}

} // namespace capy
} // namespace boost

#endif
