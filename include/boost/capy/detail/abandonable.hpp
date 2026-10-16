//
// Copyright (c) 2026 Steve Gerbino
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_DETAIL_ABANDONABLE_HPP
#define BOOST_CAPY_DETAIL_ABANDONABLE_HPP

#include <boost/capy/detail/config.hpp>
#include <boost/capy/ex/abandon.hpp>
#include <boost/capy/ex/await_context.hpp>

#include <optional>
#include <type_traits>
#include <utility>

namespace boost {
namespace capy {
namespace detail {

/** Await an awaitable on behalf of a segment owner.

    An owner suspended here is resumed either when the awaitable
    completes or to unwind its abandoned segment. In the second case the
    awaitable never completed, so the result is empty (or `false` for an
    awaitable with no result) and the owner must destroy the awaitable
    without reading it.
*/
template<class A>
struct abandonable
{
    using result_type = decltype(std::declval<A&>().await_resume());

    A& a_;
    chain_owner* owner_ = nullptr;

    bool await_ready()
    {
        return a_.await_ready();
    }

    template<class H>
    auto await_suspend(H h, await_context const* env)
    {
        owner_ = env->owner;
        return a_.await_suspend(h, env);
    }

    auto await_resume()
    {
        bool const unwinding = owner_ && owner_->abandoned();
        if constexpr(std::is_void_v<result_type>)
        {
            if(unwinding)
                return false;
            a_.await_resume();
            return true;
        }
        else
        {
            if(unwinding)
                return std::optional<result_type>();
            return std::optional<result_type>(a_.await_resume());
        }
    }
};

} // namespace detail
} // namespace capy
} // namespace boost

#endif
