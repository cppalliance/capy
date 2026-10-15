//
// Copyright (c) 2026 Steve Gerbino
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_TEST_DEATH_TEST_HPP
#define BOOST_CAPY_TEST_DEATH_TEST_HPP

#if !defined(_WIN32)

#include <boost/capy/ex/io_env.hpp>

#include <coroutine>
#include <exception>
#include <type_traits>

#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

namespace boost {
namespace capy {

/// The exit status of a child that reached `std::terminate`.
inline constexpr int terminated_status = 77;

/** Run a function in a child process and return its wait status.

    The child exits 0, or 1 if `fn` returns false. A child that calls
    `std::terminate` exits with @ref terminated_status, so an assertion
    or other abort is not mistaken for it. A child still running after
    a minute is killed, since one forked from a multithreaded process
    can deadlock.
*/
template<class F>
int
run_in_child(F fn)
{
    pid_t pid = ::fork();
    if(pid == 0)
    {
        // Keep diagnostics out of the suite's output.
        int fd = ::open("/dev/null", O_WRONLY);
        if(fd >= 0)
            ::dup2(fd, 2);
        ::alarm(60);
        std::set_terminate([] { ::_exit(terminated_status); });
        bool ok = true;
        if constexpr(std::is_same_v<decltype(fn()), bool>)
            ok = fn();
        else
            fn();
        ::_exit(ok ? 0 : 1);
    }
    int status = 0;
    ::waitpid(pid, &status, 0);
    return status;
}

/// Return true when `fn` calls `std::terminate`.
template<class F>
bool
terminates(F fn)
{
    int status = run_in_child(fn);
    return WIFEXITED(status) && WEXITSTATUS(status) == terminated_status;
}

/// Return true when `fn` returns, and returns true if it returns a bool.
template<class F>
bool
exits_cleanly(F fn)
{
    int status = run_in_child(fn);
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

/// Start a task by hand, as an awaiting parent would, and leave it lent.
template<class Task>
void
lend(Task& t, io_env& env)
{
    t.await_suspend(std::noop_coroutine(), &env).resume();
}

} // capy
} // boost

#endif

#endif
