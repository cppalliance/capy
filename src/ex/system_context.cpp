//
// Copyright (c) 2026 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#include <boost/capy/ex/system_context.hpp>

namespace boost {
namespace capy {

namespace {

// Singleton context with no-op frame allocator
class system_context_impl : public execution_context
{
public:
    system_context_impl()
    {
        set_frame_allocator(std::pmr::null_memory_resource());
    }

    ~system_context_impl()
    {
        shutdown();
        destroy();
    }
};

} // namespace

auto
get_system_context() -> execution_context&
{
    static system_context_impl ctx;
    return ctx;
}

} // namespace capy
} // namespace boost
