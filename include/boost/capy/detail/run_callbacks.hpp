//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
// Copyright (c) 2026 Michael Vandeberg
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_DETAIL_RUN_CALLBACKS_HPP
#define BOOST_CAPY_DETAIL_RUN_CALLBACKS_HPP

#include <boost/capy/detail/config.hpp>
#include <boost/capy/detail/quitter_stopped.hpp>

#include <concepts>
#include <exception>
#include <type_traits>
#include <utility>

namespace boost {
namespace capy {
namespace detail {

struct default_handler
{
    template<class T>
    void operator()(T&&) const noexcept
    {
    }

    void operator()() const noexcept
    {
    }

    void operator()(std::exception_ptr ep) const
    {
        if(!ep)
            return;
        try
        {
            std::rethrow_exception(ep);
        }
        catch(quitter_stopped const&)
        {
            // A quitter's stop is not an error.
        }
        // Any other exception propagates to the trampoline's
        // unhandled_exception, which terminates.
    }
};

template<class H1, class H2>
struct handler_pair
{
    static_assert(
        std::is_nothrow_move_constructible_v<H1> &&
        std::is_nothrow_move_constructible_v<H2>,
        "Handlers must be nothrow move constructible");

    H1 h1_;
    H2 h2_;

    template<class T>
    void operator()(T&& v)
    {
        h1_(std::forward<T>(v));
    }

    void operator()()
    {
        h1_();
    }

    void operator()(std::exception_ptr ep)
    {
        h2_(ep);
    }
};

template<class H1>
struct handler_pair<H1, default_handler>
{
    static_assert(
        std::is_nothrow_move_constructible_v<H1>,
        "Handler must be nothrow move constructible");

    H1 h1_;

    template<class T>
    void operator()(T&& v)
    {
        h1_(std::forward<T>(v));
    }

    void operator()()
    {
        h1_();
    }

    void operator()(std::exception_ptr ep)
    {
        if constexpr(std::invocable<H1, std::exception_ptr>)
            h1_(ep);
        else
            default_handler{}(ep);
    }
};

} // namespace detail
} // namespace capy
} // namespace boost

#endif
