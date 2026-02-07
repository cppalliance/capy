//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_DETAIL_AWAIT_SUSPEND_HELPER_HPP
#define BOOST_CAPY_DETAIL_AWAIT_SUSPEND_HELPER_HPP

#include <boost/capy/coro.hpp>
#include <boost/capy/ex/executor_ref.hpp>

#include <stop_token>
#include <type_traits>

namespace boost {
namespace capy {
namespace detail {

// Helper to normalize await_suspend return types to coro
template<typename Awaitable>
coro call_await_suspend(
    Awaitable* a,
    coro h,
    executor_ref ex,
    std::stop_token token)
{
    using R = decltype(a->await_suspend(h, ex, token));
    if constexpr (std::is_void_v<R>)
    {
        a->await_suspend(h, ex, token);
        return std::noop_coroutine();
    }
    else if constexpr (std::is_same_v<R, bool>)
    {
        if(a->await_suspend(h, ex, token))
            return std::noop_coroutine();
        return h;
    }
    else
    {
        return a->await_suspend(h, ex, token);
    }
}

} // namespace detail
} // namespace capy
} // namespace boost

#endif
