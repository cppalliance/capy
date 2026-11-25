//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
// Copyright (c) 2026 Michael Vandeberg
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_WORK_GUARD_HPP
#define BOOST_CAPY_WORK_GUARD_HPP

#include <boost/capy/detail/config.hpp>
#include <boost/capy/ex/execution_context.hpp>
#include <boost/capy/concept/executor.hpp>

#include <utility>

namespace boost {
namespace capy {

/** Hold work on an executor so its execution context does not finish.

    This class holds "work" on an executor. That keeps the execution
    context from finishing for lack of work: for example, `run()` on an
    event loop, or @ref thread_pool::join, keeps waiting. It calls
    `on_work_started()` on construction and `on_work_finished()` on
    destruction, ensuring proper work tracking.

    The guard is useful when you need to keep an execution context
    running while waiting for external events or when work is
    posted later.

    @par RAII Semantics

    @li Construction calls `ex.on_work_started()`.
    @li Destruction calls `ex.on_work_finished()` if `owns_work()`.
    @li Copy construction creates a new work reference (calls
        `on_work_started()` again) if `other` owns work.
    @li Move construction transfers ownership without additional calls.

    @par Thread Safety

    Distinct objects may be accessed concurrently. Access to a single
    object requires external synchronization.

    @par Example
    @par !example work_guard


    @tparam Ex A type satisfying the Executor concept.

    @see @ref make_work_guard, @ref Executor
*/
template<Executor Ex>
class work_guard
{
    Ex ex_;
    bool owns_;

public:
    /** Names the executor type this `work_guard<Ex>` guards. */
    using executor_type = Ex;

    /** Construct a work guard.

        Calls `ex.on_work_started()` to inform the executor that
        work is outstanding.

        @par Exception Safety
        No-throw guarantee.

        @par Postconditions
        @li `owns_work() == true`
        @li `executor() == ex`

        @param ex The executor to hold work on. Moved into the guard.
    */
    explicit
    work_guard(Ex ex) noexcept
        : ex_(std::move(ex))
        , owns_(true)
    {
        ex_.on_work_started();
    }

    /** Construct a copy.

        Creates a new work guard on the same executor. If `other`
        owns work, calls `on_work_started()` on the executor.

        @par Exception Safety
        No-throw guarantee.

        @par Postconditions
        @li `owns_work() == other.owns_work()`
        @li `executor() == other.executor()`

        @param other The work guard to copy from.
    */
    work_guard(work_guard const& other) noexcept
        : ex_(other.ex_)
        , owns_(other.owns_)
    {
        if(owns_)
            ex_.on_work_started();
    }

    /** Construct by moving.

        Transfers work ownership from `other` to `*this`. Does not
        call `on_work_started()` or `on_work_finished()`.

        @par Exception Safety
        No-throw guarantee.

        @par Postconditions
        @li `owns_work()` equals the prior value of `other.owns_work()`
        @li `other.owns_work() == false`

        @param other The work guard to move from.
    */
    work_guard(work_guard&& other) noexcept
        : ex_(std::move(other.ex_))
        , owns_(other.owns_)
    {
        other.owns_ = false;
    }

    /** Destroy the guard, releasing its work if it owns any.

        If `owns_work()` is `true`, calls `on_work_finished()` on
        the executor.

        @par Exception Safety
        No-throw guarantee.
    */
    ~work_guard()
    {
        if(owns_)
            ex_.on_work_finished();
    }

    /** Copy assignment is disabled.

        A guard takes its work reference at construction and releases it at
        destruction or through @ref reset. No operation rebinds an existing
        guard to a different executor.

        @param other The work guard that would be assigned from.

        @return A reference to `*this`.
    */
    work_guard& operator=(work_guard const& other) = delete;

    /** Return the underlying executor by reference.

        The guard holds its own copy of the executor, so the reference
        remains valid only for the lifetime of this guard.

        @par Exception Safety
        No-throw guarantee.

        @return A reference to the stored executor.
    */
    executor_type const&
    executor() const noexcept
    {
        return ex_;
    }

    /** Check whether the guard owns work.

        @par Exception Safety
        No-throw guarantee.

        @return `true` if this guard calls `on_work_finished()`
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

/** Create a work guard from an executor.

    @par Exception Safety
    No-throw guarantee.

    @param ex The executor to create the guard for.

    @return A `work_guard` holding work on `ex`.

    @see @ref work_guard
*/
template<Executor Ex>
work_guard<Ex>
make_work_guard(Ex ex)
{
    return work_guard<Ex>(std::move(ex));
}

} // capy
} // boost

#endif
