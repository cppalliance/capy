//
// Copyright (c) 2025 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_EXECUTOR_WORK_GUARD_HPP
#define BOOST_CAPY_EXECUTOR_WORK_GUARD_HPP

#include <boost/capy/detail/config.hpp>
#include <boost/capy/execution_context.hpp>
#include <boost/capy/executor.hpp>

#include <utility>

namespace boost {
namespace capy {

/** RAII guard that keeps an executor's context from completing.

    This class holds "work" on an executor, preventing the associated
    execution context's `run()` function from returning due to lack of
    work. It calls `on_work_started()` on construction and
    `on_work_finished()` on destruction, ensuring proper work tracking.

    The guard is useful when you need to keep an execution context
    running while waiting for external events or when work will be
    posted later.

    @par RAII Semantics

    @li Construction calls `ex.on_work_started()`.
    @li Destruction calls `ex.on_work_finished()` if `owns_work()`.
    @li Copy construction creates a new work reference (calls
        `on_work_started()` again).
    @li Move construction transfers ownership without additional calls.

    @par Thread Safety

    Distinct objects may be accessed concurrently. Access to a single
    object requires external synchronization.

    @par Example
    @code
    io_context ctx;

    // Keep context running while we set things up
    auto guard = make_work_guard(ctx);

    std::thread t([&ctx]{ ctx.run(); });

    // ... post work to ctx ...

    // Allow context to complete when work is done
    guard.reset();

    t.join();
    @endcode

    @tparam Executor A type satisfying the executor concept.

    @see make_work_guard, executor
*/
template<executor Executor>
class executor_work_guard
{
    Executor ex_;
    bool owns_;

public:
    /** The underlying executor type. */
    using executor_type = Executor;

    /** Construct a work guard.

        Calls `ex.on_work_started()` to inform the executor that
        work is outstanding.

        @par Exception Safety
        No-throw guarantee.

        @par Postconditions
        @li `owns_work() == true`
        @li `get_executor() == ex`

        @param ex The executor to hold work on. Moved into the guard.
    */
    explicit
    executor_work_guard(Executor ex) noexcept
        : ex_(std::move(ex))
        , owns_(true)
    {
        ex_.on_work_started();
    }

    /** Copy constructor.

        Creates a new work guard holding work on the same executor.
        Calls `on_work_started()` on the executor.

        @par Exception Safety
        No-throw guarantee.

        @par Postconditions
        @li `owns_work() == other.owns_work()`
        @li `get_executor() == other.get_executor()`

        @param other The work guard to copy from.
    */
    executor_work_guard(executor_work_guard const& other) noexcept
        : ex_(other.ex_)
        , owns_(other.owns_)
    {
        if(owns_)
            ex_.on_work_started();
    }

    /** Move constructor.

        Transfers work ownership from `other` to `*this`. Does not
        call `on_work_started()` or `on_work_finished()`.

        @par Exception Safety
        No-throw guarantee.

        @par Postconditions
        @li `owns_work()` equals the prior value of `other.owns_work()`
        @li `other.owns_work() == false`

        @param other The work guard to move from.
    */
    executor_work_guard(executor_work_guard&& other) noexcept
        : ex_(std::move(other.ex_))
        , owns_(other.owns_)
    {
        other.owns_ = false;
    }

    /** Destructor.

        If `owns_work()` is `true`, calls `on_work_finished()` on
        the executor.

        @par Exception Safety
        No-throw guarantee.
    */
    ~executor_work_guard()
    {
        if(owns_)
            ex_.on_work_finished();
    }

    executor_work_guard& operator=(executor_work_guard const&) = delete;

    /** Return the underlying executor.

        @par Exception Safety
        No-throw guarantee.

        @return A copy of the stored executor.
    */
    executor_type
    get_executor() const noexcept
    {
        return ex_;
    }

    /** Return whether the guard owns work.

        @par Exception Safety
        No-throw guarantee.

        @return `true` if this guard will call `on_work_finished()`
            on destruction, `false` otherwise.
    */
    bool
    owns_work() const noexcept
    {
        return owns_;
    }

    /** Release ownership of the work.

        If `owns_work()` is `true`, calls `on_work_finished()` on
        the executor and sets ownership to `false`. Otherwise, has
        no effect.

        @par Exception Safety
        No-throw guarantee.

        @par Postconditions
        @li `owns_work() == false`
    */
    void
    reset() noexcept
    {
        if(owns_)
        {
            ex_.on_work_finished();
            owns_ = false;
        }
    }
};

//------------------------------------------------

/** Create a work guard from an executor.

    @par Exception Safety
    No-throw guarantee.

    @param ex The executor to create the guard for.

    @return An `executor_work_guard` holding work on `ex`.

    @see executor_work_guard
*/
template<executor Executor>
executor_work_guard<Executor>
make_work_guard(Executor ex)
{
    return executor_work_guard<Executor>(std::move(ex));
}

/** Create a work guard from an execution context.

    Equivalent to `make_work_guard(ctx.get_executor())`.

    @par Exception Safety
    No-throw guarantee.

    @param ctx The execution context to create the guard for.

    @return An `executor_work_guard` holding work on the context's
        executor.

    @see executor_work_guard
*/
template<is_execution_context ExecutionContext>
executor_work_guard<typename ExecutionContext::executor_type>
make_work_guard(ExecutionContext& ctx)
{
    return executor_work_guard<typename ExecutionContext::executor_type>(
        ctx.get_executor());
}

} // capy
} // boost

#endif
