//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
// Copyright (c) 2026 Michael Vandeberg
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_TEST_FUSE_HPP
#define BOOST_CAPY_TEST_FUSE_HPP

#include <boost/capy/detail/config.hpp>
#include <boost/capy/concept/io_runnable.hpp>
#include <boost/capy/error.hpp>
#include <boost/capy/test/run_blocking.hpp>
#include <system_error>
#include <concepts>
#include <cstddef>
#include <exception>
#include <limits>
#include <memory>
#include <source_location>
#include <type_traits>

/*
    LLM/AI Instructions for fuse-based test patterns:

    When f.armed() runs a test, it injects errors at successive points
    via maybe_fail(). Operations like read_stream::read_some() and
    write_stream::write_some() call maybe_fail() internally.

    CORRECT pattern - early return on injected error:

        auto [ec, n] = co_await rs.read_some(buf);
        if(ec)
            co_return;  // fuse injected error, exit gracefully
        // ... continue with success path

    WRONG pattern - asserting success unconditionally:

        auto [ec, n] = co_await rs.read_some(buf);
        BOOST_TEST(! ec);  // FAILS when fuse injects error!

    The fuse mechanism tests error handling by failing at each point
    in sequence. Tests must handle injected errors by returning early,
    not by asserting that operations always succeed.
*/

namespace boost {
namespace capy {
namespace test {

/** Reruns a code path, injecting a failure at one later point on each pass.

    This class enables exhaustive testing of error handling
    paths by injecting failures at successive points in code.
    Each iteration fails at a later point until the code path
    completes without encountering a failure. The @ref armed
    method runs in two phases: first with error codes, then
    with exceptions. The @ref inert method runs once without
    automatic failure injection.

    @par Thread Safety

    @b Not @b thread @b safe. Instances must not be accessed
    from different logical threads of operation concurrently.
    This includes coroutines - accessing the same fuse from
    multiple concurrent coroutines causes non-deterministic
    test behavior.

    @par Basic Inline Usage

    @par !example example_1


    @par Named Fuse with armed()

    @par !example example_2


    @par Using inert() for Single-Run Tests

    @par !example example_3


    @par Dependency Injection (Standalone Usage)

    A default-constructed fuse is a no-op when used outside
    of @ref armed or @ref inert. This enables passing a fuse
    to classes for dependency injection without affecting
    normal operation.

    @par !example example_4


    @par Custom Error Code

    @par !example example_5


    @par Checking the Result

    @par !example example_6


    @par Test Framework Integration

    @par !example example_7

*/
class fuse
{
    struct state
    {
        std::size_t n = (std::numeric_limits<std::size_t>::max)();
        std::size_t i = 0;
        bool triggered = false;
        bool throws = false;
        bool stopped = false;
        bool inert = true;
        std::error_code ec;
        std::source_location loc;
        std::exception_ptr ep;
    };

    std::shared_ptr<state> p_;

    /** Return true if testing should continue.

        On the first call, initializes the failure point to 0.
        After a triggered failure, increments the failure point
        and resets for the next iteration. Returns false when
        the test completes without triggering a failure.
    */
    explicit operator bool() const noexcept
    {
        auto& s = *p_;
        if(s.n == (std::numeric_limits<std::size_t>::max)())
        {
            // First call: start round 0
            s.n = 0;
            return true;
        }
        if(s.triggered)
        {
            // Previous round triggered, try next failure point
            s.n++;
            s.i = 0;
            s.triggered = false;
            return true;
        }
        // Test completed without trigger: success
        return false;
    }

public:
    /** Converts to `bool`, reporting success, and carries the failure point on failure.

        Contains the outcome of @ref armed or @ref inert
        and, on failure, the source location of the failing
        point. Converts to `bool` for convenient success
        checking.

        @par Example

        @par !example example

    */
    struct result
    {
        /// Source location of the failing point, set only on failure.
        std::source_location loc = {};

        /// Exception captured by @ref fail, or null if none.
        std::exception_ptr ep = nullptr;

        /// True if the test completed without a failure.
        bool success = true;

        /** Return whether the test completed without a failure.

            @return @ref success.
        */
        constexpr explicit operator bool() const noexcept
        {
            return success;
        }
    };

    /** Construct a fuse with a custom error code.

        @par Example

        @par !example example_1


        @param ec The error code to deliver at failure points.
    */
    explicit fuse(std::error_code ec)
        : p_(std::make_shared<state>())
    {
        p_->ec = ec;
    }

    /** Construct a fuse with the default error code.

        The default error code is `error::test_failure`.

        @par Example

        @par !example example_2

    */
    fuse()
        : fuse(error::test_failure)
    {
    }

    /** Return an error or throw at the current failure point.

        When running under @ref armed, increments the internal
        counter. When the counter reaches the current failure
        point, returns the stored error code (or throws
        `std::system_error` in exception mode) and records
        the source location.

        When called outside of @ref armed or @ref inert (standalone
        usage), or when running under @ref inert, always returns
        an empty error code. This enables dependency injection
        where the fuse is a no-op in production code.

        @par Example

        @par !example example_1


        @par Standalone Usage

        @par !example example_2


        @param loc The source location of the call site,
        captured automatically.

        @return The stored error code if at the failure point,
        otherwise an empty error code. In exception mode,
        throws instead of returning an error. When called
        outside @ref armed, or when running under @ref inert,
        always returns an empty error code.

        @throws std::system_error When in exception mode
        and at the failure point (not thrown outside @ref armed).
    */
    std::error_code
    maybe_fail(
        std::source_location loc = std::source_location::current())
    {
        auto& s = *p_;
        if(s.inert)
            return {};
        if(s.i < s.n)
            ++s.i;
        if(s.i == s.n)
        {
            s.triggered = true;
            s.loc = loc;
            if(s.throws)
                throw std::system_error(s.ec);
            return s.ec;
        }
        return {};
    }

    /** Signal a test failure and stop execution.

        Call this from the test function to indicate a failure
        condition. Both @ref armed and @ref inert return
        a failed @ref result immediately.

        @par Example

        @par !example example_1


        @param loc The source location of the call site,
        captured automatically.
    */
    void
    fail(
        std::source_location loc =
            std::source_location::current()) noexcept
    {
        p_->loc = loc;
        p_->stopped = true;
    }

    /** Signal a test failure with an exception and stop execution.

        Call this from the test function to indicate a failure
        condition with an associated exception. Both @ref armed
        and @ref inert return a failed @ref result with
        the captured exception pointer.

        @par Example

        @par !example example_2


        @param ep The exception pointer to capture.

        @param loc The source location of the call site,
        captured automatically.
    */
    void
    fail(
        std::exception_ptr ep,
        std::source_location loc =
            std::source_location::current()) noexcept
    {
        p_->ep = ep;
        p_->loc = loc;
        p_->stopped = true;
    }

private:
    /*  Drive the two-phase armed loop, invoking `do_iter` once per round.

        Phase 1 delivers injected failures as error codes; phase 2 as
        exceptions. Shared by the two coroutine `armed` overloads: each
        supplies a nullary `do_iter` that runs one iteration — via
        @ref run_blocking, or via a caller-supplied runner — so the round
        sequence and failure handling stay identical across them.
    */
    template<class DoIter>
    result
    run_phases(DoIter&& do_iter)
    {
        result r;

        // Phase 1: error code mode
        p_->throws = false;
        p_->inert = false;
        p_->n = (std::numeric_limits<std::size_t>::max)();
        while(*this)
        {
            try
            {
                do_iter();
            }
            catch(...)
            {
                r.success = false;
                r.loc = p_->loc;
                r.ep = p_->ep;
                p_->inert = true;
                return r;
            }
            if(p_->stopped)
            {
                r.success = false;
                r.loc = p_->loc;
                r.ep = p_->ep;
                p_->inert = true;
                return r;
            }
        }

        // Phase 2: exception mode
        p_->throws = true;
        p_->n = (std::numeric_limits<std::size_t>::max)();
        p_->i = 0;
        p_->triggered = false;
        while(*this)
        {
            try
            {
                do_iter();
            }
            catch(std::system_error const& ex)
            {
                if(ex.code() != p_->ec)
                {
                    r.success = false;
                    r.loc = p_->loc;
                    r.ep = p_->ep;
                    p_->inert = true;
                    return r;
                }
            }
            catch(...)
            {
                r.success = false;
                r.loc = p_->loc;
                r.ep = p_->ep;
                p_->inert = true;
                return r;
            }
            if(p_->stopped)
            {
                r.success = false;
                r.loc = p_->loc;
                r.ep = p_->ep;
                p_->inert = true;
                return r;
            }
        }
        p_->inert = true;
        return r;
    }

public:
    /** Run a test function with systematic failure injection.

        Repeatedly invokes the provided function, failing at
        successive points until the function completes without
        encountering a failure. First runs the complete loop
        using error codes, then runs using exceptions.

        @par Example

        @par !example example_2


        @param fn The test function to invoke. It receives
        a reference to the fuse and should call @ref maybe_fail
        at each potential failure point.

        @return A @ref result indicating success or failure.
        On failure, `result::loc` contains the source location
        of the last @ref maybe_fail or @ref fail call.
    */
    template<class F>
    result
    armed(F&& fn)
    {
        result r;

        // Phase 1: error code mode
        p_->throws = false;
        p_->inert = false;
        p_->n = (std::numeric_limits<std::size_t>::max)();
        while(*this)
        {
            try
            {
                fn(*this);
            }
            catch(...)
            {
                r.success = false;
                r.loc = p_->loc;
                r.ep = p_->ep;
                p_->inert = true;
                return r;
            }
            if(p_->stopped)
            {
                r.success = false;
                r.loc = p_->loc;
                r.ep = p_->ep;
                p_->inert = true;
                return r;
            }
        }

        // Phase 2: exception mode
        p_->throws = true;
        p_->n = (std::numeric_limits<std::size_t>::max)();
        p_->i = 0;
        p_->triggered = false;
        while(*this)
        {
            try
            {
                fn(*this);
            }
            catch(std::system_error const& ex)
            {
                if(ex.code() != p_->ec)
                {
                    r.success = false;
                    r.loc = p_->loc;
                    r.ep = p_->ep;
                    p_->inert = true;
                    return r;
                }
            }
            catch(...)
            {
                r.success = false;
                r.loc = p_->loc;
                r.ep = p_->ep;
                p_->inert = true;
                return r;
            }
            if(p_->stopped)
            {
                r.success = false;
                r.loc = p_->loc;
                r.ep = p_->ep;
                p_->inert = true;
                return r;
            }
        }
        p_->inert = true;
        return r;
    }

    /** Run a coroutine test function with systematic failure injection.

        Repeatedly invokes the provided coroutine function, failing at
        successive points until the function completes without
        encountering a failure. First runs the complete loop
        using error codes, then runs using exceptions.

        This overload handles lambdas that return an @ref IoRunnable
        (such as `task<void>`), executing them synchronously via
        @ref run_blocking.

        @par Example

        @par !example example_3


        @param fn The coroutine test function to invoke. It receives
        a reference to the fuse and should call @ref maybe_fail
        at each potential failure point.

        @return A @ref result indicating success or failure.
        On failure, `result::loc` contains the source location
        of the last @ref maybe_fail or @ref fail call.
    */
    template<class F>
        requires IoRunnable<std::invoke_result_t<F, fuse&>>
    result
    armed(F&& fn)
    {
        return run_phases([&]{ run_blocking()(fn(*this)); });
    }

    /** Run a coroutine test function on a caller-supplied runner.

        Behaves like the @ref IoRunnable overload of @ref armed, but
        instead of driving each iteration through @ref run_blocking, it
        hands the coroutine to `run_one`. This lets a caller run each
        iteration on any execution context it chooses. Operations built
        on `corosio::timeout` or `corosio::delay` in particular require
        an `io_context`, because they abort on a non-`io_context`
        executor. `fuse` never learns about the context;
        the caller owns the drive loop.

        @par Runner contract
        `run_one` is invoked once per round with the @ref IoRunnable
        produced by `fn`. It must run that task to completion
        synchronously and *return* any exception the task raised as a
        `std::exception_ptr` (null on success). It must not rethrow.
        `armed` rethrows the returned pointer from its own synchronous
        code, so the exception phase observes injected failures. An
        exception escaping a `run_async` completion handler would
        instead call `std::terminate`. Capture the exception in the error
        handler and return it once the run loop is done.

        @par Example
        @par !example example_1


        @param run_one A callable invoked with each iteration's task; it
        runs the task to completion and returns any escaped exception
        (null on success) without rethrowing.

        @param fn The coroutine test function to invoke.

        @return A @ref result indicating success or failure.
    */
    template<class Runner, class F>
        requires IoRunnable<std::invoke_result_t<F, fuse&>>
            && std::same_as<
                std::invoke_result_t<Runner&, std::invoke_result_t<F, fuse&>>,
                std::exception_ptr>
    result
    armed(Runner&& run_one, F&& fn)
    {
        return run_phases([&]{
            if(auto ep = run_one(fn(*this)))
                std::rethrow_exception(ep);
        });
    }

    /** Alias for @ref armed.

        Allows the fuse to be invoked directly as a function
        object for more concise syntax.

        @par Example

        @par !example example


        @param fn The test function to run under failure injection.

        @return The @ref result of the armed run.

        @see armed
    */
    template<class F>
    result
    operator()(F&& fn)
    {
        return armed(std::forward<F>(fn));
    }

    /** Alias for @ref armed (coroutine overload).

        @param fn The test coroutine factory to run under failure injection.

        @return The @ref result of the armed run.

        @see armed
    */
    template<class F>
        requires IoRunnable<std::invoke_result_t<F, fuse&>>
    result
    operator()(F&& fn)
    {
        return armed(std::forward<F>(fn));
    }

    /** Run a test function once without failure injection.

        Invokes the provided function exactly once. Calls to
        @ref maybe_fail always return an empty error code and
        never throw. Only explicit calls to @ref fail can
        signal a test failure.

        This is useful for running tests where you want to
        manually control failures, or for quick single-run
        tests without systematic error injection.

        @par Example

        @par !example example_1


        @param fn The test function to invoke. It receives
        a reference to the fuse. Calls to @ref maybe_fail
        always succeed.

        @return A @ref result indicating success or failure.
        On failure, `result::loc` contains the source location
        of the @ref fail call.
    */
    template<class F>
    result
    inert(F&& fn)
    {
        result r;
        p_->inert = true;
        try
        {
            fn(*this);
        }
        catch(...)
        {
            r.success = false;
            r.loc = p_->loc;
            r.ep = std::current_exception();
            return r;
        }
        if(p_->stopped)
        {
            r.success = false;
            r.loc = p_->loc;
            r.ep = p_->ep;
        }
        return r;
    }

    /** Run a coroutine test function once without failure injection.

        Invokes the provided coroutine function exactly once using
        @ref run_blocking. Calls to @ref maybe_fail always return
        an empty error code and never throw. Only explicit calls
        to @ref fail can signal a test failure.

        @par Example

        @par !example example_2


        @param fn The coroutine test function to invoke. It receives
        a reference to the fuse. Calls to @ref maybe_fail
        always succeed.

        @return A @ref result indicating success or failure.
        On failure, `result::loc` contains the source location
        of the @ref fail call.
    */
    template<class F>
        requires IoRunnable<std::invoke_result_t<F, fuse&>>
    result
    inert(F&& fn)
    {
        result r;
        p_->inert = true;
        try
        {
            run_blocking()(fn(*this));
        }
        catch(...)
        {
            r.success = false;
            r.loc = p_->loc;
            r.ep = std::current_exception();
            return r;
        }
        if(p_->stopped)
        {
            r.success = false;
            r.loc = p_->loc;
            r.ep = p_->ep;
        }
        return r;
    }
};

} // test
} // capy
} // boost

#endif
