//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
// Copyright (c) 2026 Michael Vandeberg
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_IO_TASK_HPP
#define BOOST_CAPY_IO_TASK_HPP

#include <boost/capy/error.hpp>
#include <boost/capy/io_result.hpp>
#include <boost/capy/task.hpp>

namespace boost {
namespace capy {

/** Names `task<io_result<Ts...>>`, whose `co_return` can convert an error code directly.

    This is a convenience alias for `task<io_result<Ts...>>`.
    The tuple converting constructor allows direct `co_return`
    of error codes:

    @par !example example


    @tparam Ts Additional value types beyond error_code.
*/
template<class... Ts>
using io_task = task<io_result<Ts...>>;

} // namespace capy
} // namespace boost

#endif
