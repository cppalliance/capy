//
// Copyright (c) 2026 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#include <boost/capy/ex/system_context.hpp>

#include <mutex>

namespace boost {
namespace capy {

auto
get_system_context() -> thread_pool&
{
    static thread_pool ctx;
    return ctx;
}

} // namespace capy
} // namespace boost
