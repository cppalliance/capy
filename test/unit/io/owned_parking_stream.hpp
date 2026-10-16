//
// Copyright (c) 2026 Steve Gerbino
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_TEST_OWNED_PARKING_STREAM_HPP
#define BOOST_CAPY_TEST_OWNED_PARKING_STREAM_HPP

#include <boost/capy/buffers.hpp>
#include <boost/capy/results.hpp>

#include "test/unit/test_helpers.hpp"

#include <coroutine>
#include <cstddef>
#include <system_error>
#include <utility>

namespace boost {
namespace capy {

/** A stream for a type-erased wrapper to own.

    Its reads and writes park the awaiting coroutine for the test to
    resume or abandon. Each operation records, when it is destroyed,
    whether the stream it came from was already gone.
*/
struct owned_parking_stream
{
    /// What the stream and its operations report to the test.
    struct state
    {
        parked slot;
        int ops_down = 0;
        int late = 0;
        bool alive = false;
    };

    struct op
    {
        state* st_;

        ~op()
        {
            if(st_)
            {
                ++st_->ops_down;
                if(!st_->alive)
                    ++st_->late;
            }
        }

        explicit op(state* st) noexcept
            : st_(st)
        {
        }

        op(op&& o) noexcept
            : st_(std::exchange(o.st_, nullptr))
        {
        }

        bool await_ready() const noexcept { return false; }

        std::coroutine_handle<>
        await_suspend(std::coroutine_handle<> h, await_context const* env) noexcept
        {
            return park_awaitable{&st_->slot}.await_suspend(h, env);
        }

        results<std::size_t> await_resume() noexcept
        {
            return {std::error_code(), 0};
        }
    };

    state* st_;

    ~owned_parking_stream()
    {
        if(st_)
            st_->alive = false;
    }

    explicit owned_parking_stream(state& st) noexcept
        : st_(&st)
    {
        st.alive = true;
    }

    owned_parking_stream(owned_parking_stream&& o) noexcept
        : st_(std::exchange(o.st_, nullptr))
    {
    }

    op read_some(MutableBufferSequence auto)
    {
        return op{st_};
    }

    op write_some(ConstBufferSequence auto)
    {
        return op{st_};
    }
};

} // capy
} // boost

#endif
