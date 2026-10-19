//
// Copyright (c) 2026 Michael Vandeberg
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_TEST_FAULT_HPP
#define BOOST_CAPY_TEST_FAULT_HPP

/* A cut-down copy of Corosio's fault harness (test/unit/fault there):
   the same fault_scope interface, with only the entry points Capy's
   tests need and one arm per thread.
*/

namespace boost {
namespace capy {
namespace test {
namespace fault {

/** OS entry points the harness can fail.

    Names equal the libc symbol.
*/
enum class sys
{
    pthread_create
};

/** Fail one OS call on the current thread.

    While the scope is alive, the `nth` call to `which` made on this
    thread returns its documented failure with error `err`. Calls
    before and after it forward to the real function, and other
    threads never observe the fault. An `nth` that is never reached
    leaves an arm that only counts calls.

    @par Preconditions
    No other `fault_scope` is alive on this thread; a second one
    aborts. The scope is destroyed on the thread that created it.

    @par Example
    @code
    fault_scope f(sys::pthread_create, EAGAIN);
    std::thread t([]{}); // throws std::system_error
    @endcode
*/
class fault_scope
{
public:
    /// Arm `which` to fail with `err` on its `nth` call.
    fault_scope(sys which, int err, unsigned nth = 1);

    /// Disarm, even if the fault never fired.
    ~fault_scope();

    /// Return true once the armed call has been failed.
    bool fired() const noexcept;

    /** Return how many calls to the armed symbol this arm has seen.

        @return Calls seen since the scope was constructed, including
            the one that fired.
    */
    unsigned count() const noexcept;

    fault_scope(fault_scope const&) = delete;
    fault_scope& operator=(fault_scope const&) = delete;
};

/** Return true if a call to `which` reaches the harness.

    The harness checks at startup that the dynamic loader binds the
    symbol to its replacement. A test asks here rather than asserting
    on an arm that can never fire.

    @param which The entry point to ask about.
*/
bool hook_is_live(sys which) noexcept;

} // fault
} // test
} // capy
} // boost

#endif
