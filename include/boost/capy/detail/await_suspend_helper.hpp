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
        via `executor::dispatch()`, which may resume the parent.
        The parent can destroy this frame before the runtime reads
        `__$ReturnUdt$` (e.g. `dispatch_trampoline` final_suspend).

    On MSVC this function calls `h.resume()` on the current stack
    and returns `void`, causing unconditional suspension. The
    trade-off is O(n) stack growth instead of O(1) tail-calls.

    On other compilers the handle is returned directly for proper
    symmetric transfer.

    Callers must use `auto` return type on their `await_suspend`
    so the return type adapts per platform.

    @param h The coroutine handle to transfer to.
*/
#ifdef _MSC_VER
inline void symmetric_transfer(std::coroutine_handle<> h) noexcept
{
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
