//
// Copyright (c) 2026 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_SYSTEM_CONTEXT_HPP
#define BOOST_CAPY_SYSTEM_CONTEXT_HPP

#include <boost/capy/detail/config.hpp>
#include <boost/capy/ex/execution_context.hpp>

namespace boost {
namespace capy {

/** Return the process-wide system execution context.

    This singleton context serves as a container for services
    but does not provide an executor or handle work. 

    @par Thread Safety
    Safe to call from any thread.

    @return Reference to the system execution context singleton.
*/
BOOST_CAPY_DECL auto
get_system_context() -> execution_context&;

} // capy
} // boost

#endif
