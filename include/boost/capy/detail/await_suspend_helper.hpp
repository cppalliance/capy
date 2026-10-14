//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
// Copyright (c) 2026 Steve Gerbino
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_DETAIL_AWAIT_SUSPEND_HELPER_HPP
#define BOOST_CAPY_DETAIL_AWAIT_SUSPEND_HELPER_HPP

#include <coroutine>
#include <boost/capy/detail/config.hpp>
#include <boost/capy/ex/io_env.hpp>

#include <type_traits>

namespace boost {
namespace capy {
namespace detail {

/** Perform symmetric transfer, working around an MSVC codegen bug.

    MSVC stores the `std::coroutine_handle<>` returned from
    `await_suspend` in a hidden `__$ReturnUdt$` variable located
    on the coroutine frame. When another thread resumes or destroys
    the frame between the store and the read-back for the
    symmetric-transfer tail-call, the read hits freed memory.

    This occurs in two scenarios:

    @li `await_suspend` calls `h.destroy()` then returns a handle
        (e.g. `when_all_runner` and `when_any_runner` final_suspend).
        The return value is written to the now-destroyed frame.

    @li `await_suspend` hands the continuation to another thread
        via an executor handoff (e.g. `post()` or `dispatch()`),
        which may resume the parent. The parent can destroy this
        frame before the runtime reads `__$ReturnUdt$` (e.g.
        `boundary_trampoline` final_suspend).

    On affected compilers this function calls `h.resume()` on the
    current stack and returns `void`, causing unconditional
    suspension. The trade-off is O(n) stack growth instead of
    O(1) tail-calls.

    On x64 the workaround applies to MSVC 19.34 through 19.44 and
    self-retires on MSVC 19.50 (VS 2026 / 18.0). Measured on
    19.44 the caller builds the hidden return slot at
    `__coro_frame_ptr$ + 0xC0`, on the coroutine frame; on 19.51
    it is an `rsp`-relative stack temporary, so destroying the
    frame no longer invalidates it.

    Do not widen this gate on the basis of Developer Community
    ticket 10251975, tagged "Fixed in VS 2022 17.9 Preview 2";
    19.39 reproduces the fault identically to 19.34.

    On ARM64 the workaround does not retire at 19.50, because
    that target has a second, unrelated defect. With a real
    symmetric transfer, MSVC 19.51 release loses the handler for
    a `try` region that spans the suspend point: after the
    coroutine is resumed from another call stack, a `throw`
    inside that region is not caught by the `catch` beside it and
    escapes to the promise's `unhandled_exception`. A `catch(...)`
    misses it too, so the region is not found at all rather than
    the handler failing to match. Debug builds are unaffected, as
    is x64 at the same toolset. `testCatchAfterDeferredResume` in
    test/unit/task.cpp covers this.

    `_M_ARM64EC` is included conservatively. It generates ARM64
    code and has not been tested here; keeping the workaround on
    is the safe direction, since it costs stack depth rather than
    correctness.

    The gate deliberately excludes Clang. Both `clang-cl` and
    `clang++` targeting Windows define `_MSC_VER` for ABI
    compatibility, but generate a correct tail-call.

    Note that a probe which merely poisons the destroyed frame
    cannot validate this gate. Routing the return through this
    function moves the frame write to after `destroy()`, which
    repairs the poison pattern and hides the defect. The
    regression test in
    test/unit/detail/await_suspend_helper.cpp unmaps the frame
    instead, so any post-destroy access faults.

    On unaffected compilers the handle is returned directly for
    proper symmetric transfer.

    Callers must use `auto` return type on their `await_suspend`
    so the return type adapts per platform.

    @param h The coroutine handle to transfer to.
*/
#if (BOOST_CAPY_WORKAROUND(_MSC_VER, < 1950) || \
     defined(_M_ARM64) || defined(_M_ARM64EC)) && \
    !defined(__clang__)
inline void symmetric_transfer(std::coroutine_handle<> h) noexcept
{
    // safe_resume is not needed here: the calling coroutine is
    // about to suspend unconditionally. When it later resumes,
    // await_resume restores TLS from the promise's environment.
    h.resume();
}
#else
inline std::coroutine_handle<>
symmetric_transfer(std::coroutine_handle<> h) noexcept
{
    return h;
}
#endif

// Helper to normalize await_suspend return types to std::coroutine_handle<>
template<typename Awaitable>
std::coroutine_handle<> call_await_suspend(
    Awaitable* a,
    std::coroutine_handle<> h,
    io_env const* env)
{
    using R = decltype(a->await_suspend(h, env));
    if constexpr (std::is_void_v<R>)
    {
        a->await_suspend(h, env);
        return std::noop_coroutine();
    }
    else if constexpr (std::is_same_v<R, bool>)
    {
        if(a->await_suspend(h, env))
            return std::noop_coroutine();
        return h;
    }
    else
    {
        return a->await_suspend(h, env);
    }
}

} // namespace detail
} // namespace capy
} // namespace boost

#endif
