//
// Copyright (c) 2025 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Test that header file is self-contained.
#include <boost/capy/ex/async_mutex.hpp>

#include <boost/capy/concept/io_awaitable.hpp>
#include <boost/capy/ex/executor_ref.hpp>

namespace boost {
namespace capy {

static_assert(IoAwaitable<async_mutex::lock_awaiter, executor_ref>);
static_assert(IoAwaitable<async_mutex::lock_guard_awaiter, executor_ref>);

} // capy
} // boost
