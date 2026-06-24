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

/** Return the process-wide system thread pool.

    This singleton is the default execution context used when no
    explicit context is supplied (for example, by timers and
    services). It provides an executor via `get_executor()` and
    runs scheduled work on its worker threads.

    @par Thread Safety
    Safe to call from any thread.

    @return Reference to the system thread pool singleton.
*/
BOOST_CAPY_DECL auto
get_system_context() -> thread_pool&;

} // capy
} // boost

#endif
