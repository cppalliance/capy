//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_TEST_FUSE_HPP
#define BOOST_CAPY_TEST_FUSE_HPP

#include <boost/capy/detail/config.hpp>
#include <boost/capy/concept/io_launchable_task.hpp>
#include <boost/capy/error.hpp>
#include <boost/capy/test/run_blocking.hpp>
#include <system_error>
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

/** A test utility for systematic error injection.

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

    @code
    fuse()([](fuse& f) {
        auto ec = f.maybe_fail();
        if(ec)
            return;

        ec = f.maybe_fail();
        if(ec)
            return;
    });
    @endcode

    @par Named Fuse with armed()

    @code
    fuse f;
    MyObject obj(f);
    auto r = f.armed([&](fuse&) {
        obj.do_something();
    });
    @endcode

    @par Using inert() for Single-Run Tests

    @code
    fuse f;
    auto r = f.inert([](fuse& f) {
        auto ec = f.maybe_fail();  // Always succeeds
        if(some_condition)
            f.fail();  // Only way to signal failure
    });
    @endcode

    @par Dependency Injection (Standalone Usage)

    A default-constructed fuse is a no-op when used outside
    of @ref armed or @ref inert. This enables passing a fuse
    to classes for dependency injection without affecting
    normal operation.

    @code
    class MyService
    {
        fuse& f_;
    public:
        explicit MyService(fuse& f) : f_(f) {}

        std::error_code do_work()
        {
            auto ec = f_.maybe_fail();  // No-op outside armed/inert
            if(ec)
                return ec;
            // ... actual work ...
            return {};
        }
    };

    // Production usage - fuse is no-op
    fuse f;
    MyService svc(f);
    svc.do_work();  // maybe_fail() returns {} always

    // Test usage - failures are injected
    auto r = f.armed([&](fuse&) {
        svc.do_work();  // maybe_fail() triggers failures
    });
    @endcode

    @par Custom Error Code

    @code
    auto custom_ec = make_error_code(
        std::errc::operation_canceled);
    fuse f(custom_ec);
    auto r = f.armed([](fuse& f) {
        auto ec = f.maybe_fail();
        if(ec)
            return;
    });
    @endcode

    @par Checking the Result

    @code
    fuse f;
    auto r = f([](fuse& f) {
        auto ec = f.maybe_fail();
        if(ec)
            return;
    });

    if(!r)
    {
        std::cerr << "Failure at "
            << r.loc.file_name() << ":"
            << r.loc.line() << "\n";
    }
    @endcode

    @par Test Framework Integration

    @code
    fuse f;
    auto r = f([](fuse& f) {
        auto ec = f.maybe_fail();
        if(ec)
            return;
    });

    // Boost.Test
    BOOST_TEST(r.success);
    if(!r)
        BOOST_TEST_MESSAGE("Failed at " << r.loc.file_name()
            << ":" << r.loc.line());

    // Catch2
    REQUIRE(r.success);
    if(!r)
        INFO("Failed at " << r.loc.file_name()
            << ":" << r.loc.line());
    @endcode
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
    /** Result of a fuse operation.

        Contains the outcome of @ref armed or @ref inert
        and, on failure, the source location of the failing
        point. Converts to `bool` for convenient success
        checking.

        @par Example

        @code
        fuse f;
        auto r = f([](fuse& f) {
            auto ec = f.maybe_fail();
            if(ec)
                return;
        });

        if(!r)
        {
            std::cerr << "Failure at "
                << r.loc.file_name() << ":"
                << r.loc.line() << "\n";
        }
        @endcode
    */
    struct result
    {
        std::source_location loc = {};
        std::exception_ptr ep = nullptr;
        bool success = true;

        constexpr explicit operator bool() const noexcept
        {
            return success;
        }
    };

    /** Construct a fuse with a custom error code.

        @par Example

        @code
        auto custom_ec = make_error_code(
            std::errc::operation_canceled);
        fuse f(custom_ec);

        std::error_code captured_ec;
        auto r = f([&](fuse& f) {
            auto ec = f.maybe_fail();
            if(ec)
            {
                captured_ec = ec;
                return;
            }
        });

        assert(captured_ec == custom_ec);
        @endcode

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

        @code
        fuse f;
        std::error_code captured_ec;

        auto r = f([&](fuse& f) {
            auto ec = f.maybe_fail();
            if(ec)
            {
                captured_ec = ec;
                return;
            }
        });

        assert(captured_ec == error::test_failure);
        @endcode
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

        @code
        fuse f;
        auto r = f([](fuse& f) {
            // Error code mode: returns the error
            auto ec = f.maybe_fail();
            if(ec)
                return;

            // Exception mode: throws system_error
            ec = f.maybe_fail();
            if(ec)
                return;
        });
        @endcode

        @par Standalone Usage

        @code
        fuse f;
        auto ec = f.maybe_fail();  // Always returns {} (no-op)
        @endcode

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
        condition. Both @ref armed and @ref inert will return
        a failed @ref result immediately.

        @par Example

        @code
        fuse f;
        auto r = f([](fuse& f) {
            auto ec = f.maybe_fail();
            if(ec)
                return;

            // Explicit failure when a condition is not met
            if(some_value != expected)
            {
                f.fail();
                return;
            }
        });

        if(!r)
        {
            std::cerr << "Test failed at "
                << r.loc.file_name() << ":"
                << r.loc.line() << "\n";
        }
        @endcode

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
        and @ref inert will return a failed @ref result with
        the captured exception pointer.

        @par Example

        @code
        fuse f;
        auto r = f([](fuse& f) {
            try
            {
                do_something();
            }
            catch(...)
            {
                f.fail(std::current_exception());
                return;
            }
        });

        if(!r)
        {
            try
            {
                if(r.ep)
                    std::rethrow_exception(r.ep);
            }
            catch(std::exception const& e)
            {
                std::cerr << "Exception: " << e.what() << "\n";
            }
        }
        @endcode

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

    /** Run a test function with systematic failure injection.

        Repeatedly invokes the provided function, failing at
        successive points until the function completes without
        encountering a failure. First runs the complete loop
        using error codes, then runs using exceptions.

        @par Example

        @code
        fuse f;
        auto r = f.armed([](fuse& f) {
            auto ec = f.maybe_fail();
            if(ec)
                return;

            ec = f.maybe_fail();
            if(ec)
                return;
        });

        if(!r)
        {
            std::cerr << "Failure at "
                << r.loc.file_name() << ":"
                << r.loc.line() << "\n";
        }
        @endcode

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

        This overload handles lambdas that return an @ref IoLaunchableTask
        (such as `task<void>`), executing them synchronously via
        @ref run_blocking.

        @par Example

        @code
        fuse f;
        auto r = f.armed([&](fuse&) -> task<void> {
            auto ec = f.maybe_fail();
            if(ec)
                co_return;

            ec = f.maybe_fail();
            if(ec)
                co_return;
        });

        if(!r)
        {
            std::cerr << "Failure at "
                << r.loc.file_name() << ":"
                << r.loc.line() << "\n";
        }
        @endcode

        @param fn The coroutine test function to invoke. It receives
        a reference to the fuse and should call @ref maybe_fail
        at each potential failure point.

        @return A @ref result indicating success or failure.
        On failure, `result::loc` contains the source location
        of the last @ref maybe_fail or @ref fail call.
    */
    template<class F>
        requires IoLaunchableTask<std::invoke_result_t<F, fuse&>>
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
                run_blocking()(fn(*this));
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
                run_blocking()(fn(*this));
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

    /** Alias for @ref armed.

        Allows the fuse to be invoked directly as a function
        object for more concise syntax.

        @par Example

        @code
        // These are equivalent:
        fuse f;
        auto r1 = f.armed([](fuse& f) { ... });
        auto r2 = f([](fuse& f) { ... });

        // Inline usage:
        auto r3 = fuse()([](fuse& f) {
            auto ec = f.maybe_fail();
            if(ec)
                return;
        });
        @endcode

        @see armed
    */
    template<class F>
    result
    operator()(F&& fn)
    {
        return armed(std::forward<F>(fn));
    }

    /** Alias for @ref armed (coroutine overload).

        @see armed
    */
    template<class F>
        requires IoLaunchableTask<std::invoke_result_t<F, fuse&>>
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

        @code
        fuse f;
        auto r = f.inert([](fuse& f) {
            auto ec = f.maybe_fail();  // Always succeeds
            assert(!ec);

            // Only way to signal failure:
            if(some_condition)
            {
                f.fail();
                return;
            }
        });

        if(!r)
        {
            std::cerr << "Test failed at "
                << r.loc.file_name() << ":"
                << r.loc.line() << "\n";
        }
        @endcode

        @param fn The test function to invoke. It receives
        a reference to the fuse. Calls to @ref maybe_fail
        will always succeed.

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

        @code
        fuse f;
        auto r = f.inert([](fuse& f) -> task<void> {
            auto ec = f.maybe_fail();  // Always succeeds
            assert(!ec);

            // Only way to signal failure:
            if(some_condition)
            {
                f.fail();
                co_return;
            }
        });

        if(!r)
        {
            std::cerr << "Test failed at "
                << r.loc.file_name() << ":"
                << r.loc.line() << "\n";
        }
        @endcode

        @param fn The coroutine test function to invoke. It receives
        a reference to the fuse. Calls to @ref maybe_fail
        will always succeed.

        @return A @ref result indicating success or failure.
        On failure, `result::loc` contains the source location
        of the @ref fail call.
    */
    template<class F>
        requires IoLaunchableTask<std::invoke_result_t<F, fuse&>>
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
