//
// Copyright (c) 2026 Steve Gerbino
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef CAPY_EXAMPLE_ABANDON_GUARD_HPP
#define CAPY_EXAMPLE_ABANDON_GUARD_HPP

#include <boost/capy/continuation.hpp>
#include <boost/capy/ex/abandon.hpp>
#include <boost/capy/ex/frame_allocator.hpp>

#include <utility>

// Rides in an Asio handler holding a parked coroutine. A handler
// destroyed without running, as when its io_context is destroyed,
// gives the coroutine up instead of leaking its chain.
class abandon_guard
{
    boost::capy::continuation* c_;

public:
    ~abandon_guard()
    {
        if(c_)
            boost::capy::abandon(*c_);
    }

    explicit abandon_guard(boost::capy::continuation& c) noexcept
        : c_(&c)
    {
    }

    abandon_guard(abandon_guard&& other) noexcept
        : c_(std::exchange(other.c_, nullptr))
    {
    }

    abandon_guard(abandon_guard const&) = delete;
    abandon_guard& operator=(abandon_guard const&) = delete;
    abandon_guard& operator=(abandon_guard&&) = delete;

    // Return the held continuation, still given up on destruction.
    boost::capy::continuation* get() const noexcept
    {
        return c_;
    }

    // Return the continuation, which is no longer given up, or null
    // if this guard holds none.
    boost::capy::continuation* release() noexcept
    {
        return std::exchange(c_, nullptr);
    }
};

// Resumes its coroutine when Asio runs it; gives it up when Asio
// destroys it unrun.
class resume_or_abandon
{
    abandon_guard g_;

public:
    explicit resume_or_abandon(boost::capy::continuation& c) noexcept
        : g_(c)
    {
    }

    // Leave the coroutine to the caller, as when the post that was to
    // carry it throws. Returns false if this handler was moved from.
    bool release() noexcept
    {
        return g_.release() != nullptr;
    }

    void operator()()
    {
        boost::capy::safe_resume(g_.release()->h);
    }
};

#endif
