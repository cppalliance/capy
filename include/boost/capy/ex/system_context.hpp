//
// Copyright (c) 2026 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_SYSTEM_CONTEXT_HPP
#define BOOST_CAPY_SYSTEM_CONTEXT_HPP

#include <boost/capy/detail/config.hpp>
#include <boost/capy/ex/thread_pool.hpp>

namespace boost {
namespace capy {

/** Return a process-wide thread pool, creating it on first use.

    Use it when code needs an execution context but has none of its
    own, and the program does not want to own a pool. Nothing in the
    library uses it implicitly.

    The pool has one worker thread per hardware thread and is destroyed
    at program exit. Work must not be posted to it during or after its
    destruction. Like any @ref thread_pool, it allocates coroutine
    frames with `std::allocator<void>` unless
    @ref execution_context::set_frame_allocator changes that.

    @par Thread Safety
    Safe to call from any thread.

    @return Reference to the system thread pool singleton.
*/
BOOST_CAPY_DECL auto
get_system_context() -> thread_pool&;

} // capy
} // boost

#endif
