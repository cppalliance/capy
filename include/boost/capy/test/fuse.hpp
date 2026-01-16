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
#include <boost/capy/error.hpp>
#include <boost/system/system_error.hpp>
#include <cstddef>
#include <limits>
#include <memory>

namespace boost {
namespace capy {
namespace test {

/** A test utility for systematic error injection.

    This class enables exhaustive testing of error handling
    paths by injecting failures at successive points in code.
    Each iteration fails at a later point until the code path
    completes without encountering a failure. The check runs
    in two phases: first with error codes, then with exceptions.

    @par Thread Safety

    @b Not @b thread @b safe. Instances must not be accessed
    from different logical threads of operation concurrently.
    This includes coroutines - accessing the same fuse from
    multiple concurrent coroutines causes non-deterministic
    test behavior.

    @par Usage

    @code
    // Simple inline usage
    fuse().check([](fuse& f) {
        auto ec = f.maybe_fail();
        if(ec.failed())
            return;
        // ... more test code with maybe_fail() calls ...
    });

    // Named fuse for passing to objects under test
    fuse f;
    MyObject obj(f);
    bool ok = f.check([&](fuse&) {
        obj.do_something();
    });
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
        system::error_code ec;
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
    /** Construct a fuse with a custom error code.

        @param ec The error code to deliver at failure points.
    */
    explicit fuse(system::error_code ec)
        : p_(std::make_shared<state>())
    {
        p_->ec = ec;
    }

    /** Construct a fuse with the default error code.
    */
    fuse()
        : fuse(error::test_failure)
    {
    }

    /** Return an error or throw at the current failure point.

        Increments the internal counter. When the counter
        reaches the current failure point, returns the stored
        error code (or throws `system::system_error` in
        exception mode) and sets the triggered flag.

        @return The stored error code if at the failure point,
        otherwise an empty error code. In exception mode,
        throws instead of returning an error.

        @throws system::system_error When in exception mode
        and at the failure point.
    */
    system::error_code
    maybe_fail()
    {
        auto& s = *p_;
        if(s.i < s.n)
            ++s.i;
        if(s.i == s.n)
        {
            s.triggered = true;
            if(s.throws)
                throw system::system_error(s.ec);
            return s.ec;
        }
        return {};
    }

    /** Signal a test failure and stop the check loop.

        Call this from the test function to indicate a failure
        condition. The check loop will end immediately and
        `check()` will return `false`.
    */
    void
    fail_stop() noexcept
    {
        p_->stopped = true;
    }

    /** Run a test function with failure injection.

        Repeatedly invokes the provided function, failing at
        successive points until the function completes without
        encountering a failure. First runs the complete loop
        using error codes, then runs using exceptions.

        @param f The test function to invoke. It receives
        a reference to the fuse and should call `maybe_fail()`
        at each potential failure point.

        @return `true` if all failure points were tested
        successfully, `false` if a stray exception was caught
        or `fail_stop()` was called.
    */
    template<class F>
    bool
    check(F&& f)
    {
        // Phase 1: error code mode
        p_->throws = false;
        p_->n = (std::numeric_limits<std::size_t>::max)();
        while(*this)
        {
            try
            {
                f(*this);
            }
            catch(...)
            {
                return false;
            }
            if(p_->stopped)
                return false;
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
                f(*this);
            }
            catch(system::system_error const& ex)
            {
                if(ex.code() != p_->ec)
                    return false;
            }
            catch(...)
            {
                return false;
            }
            if(p_->stopped)
                return false;
        }
        return true;
    }
};

} // test
} // capy
} // boost

#endif
