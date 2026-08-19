//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
// Copyright (c) 2026 Michael Vandeberg
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_IO_ENV_HPP
#define BOOST_CAPY_IO_ENV_HPP

#include <boost/capy/detail/config.hpp>
#include <boost/capy/ex/executor_ref.hpp>

#include <coroutine>
#include <memory_resource>
#include <stop_token>

namespace boost {
namespace capy {

/** Carries the executor, stop token, and frame allocator through a coroutine chain.

    This struct bundles the execution context passed through
    coroutine chains via the IoAwaitable protocol. It contains
    the executor for resumption, a stop token for cancellation,
    and an optional frame allocator for coroutine frame allocation.

    @par Lifetime

    Launcher functions (@ref run_async, @ref run) own the `io_env` and
    guarantee it outlives all tasks and awaitables in the started
    chain. Awaitables receive `io_env const*` in `await_suspend`
    and should store it directly, never copy the pointed-to object.

    @par Thread Safety
    The referenced executor and allocator must remain valid
    for the lifetime of any coroutine using this environment.

    @see IoAwaitable, IoRunnable
*/
struct io_env
{
    /** The executor for coroutine resumption. */
    executor_ref executor;

    /** The stop token for cancellation propagation. */
    std::stop_token stop_token;

    /** The frame allocator for coroutine frame allocation.

        When null, the default allocator is used.
    */
    std::pmr::memory_resource* frame_allocator = nullptr;

};

} // capy
} // boost

#endif
