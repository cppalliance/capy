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

/** RAII guard that keeps an executor's context from completing.

    This class holds "work" on an executor, preventing the associated
    execution context's `run()` function from returning due to lack of
    work. It calls `on_work_started()` on construction and
    `on_work_finished()` on destruction, ensuring proper work tracking.

    The guard is useful when you need to keep an execution context
    running while waiting for external events or when work is
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
    @par !example work_guard


    @note The executor is returned by reference, allowing callers to
    manage the executor's lifetime directly. This is essential in
    coroutine-first designs where the executor often outlives individual
    coroutine frames.

    @tparam Ex A type satisfying the Executor concept.

    @see make_work_guard, Executor
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

        Creates a new work guard holding work on the same executor.
        Calls `on_work_started()` on the executor.

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

    /** Destructor.

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

        The reference remains valid for the lifetime of this guard,
        enabling callers to manage executor lifetime explicitly.

        @par Exception Safety
        No-throw guarantee.

        @return A reference to the stored executor.
    */
    executor_type const&
    executor() const noexcept
    {
        return ex_;
    }

    /** Return whether the guard owns work.

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

    @see work_guard
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
