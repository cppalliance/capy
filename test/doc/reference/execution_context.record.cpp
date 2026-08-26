//
// Copyright (c) 2026 Michael Vandeberg
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Reference examples for boost::capy::execution_context, injected into its documentation by
// doc/addons/extensions/reference-snippets.lua. Declared in:
//   include/boost/capy/ex/execution_context.hpp
//
// The tagged regions are what the reference renders; the includes,
// suppressions and namespaces around them are scaffolding. Each region gets
// its own namespace so that examples which reuse a name still compile.

#include "../doc_warnings.hpp"

#include <boost/capy.hpp>

#include <cassert>
#include <chrono>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace capy = boost::capy;

namespace {

namespace ex_1 {
// tag::example[]
struct file_service : capy::execution_context::service
{
protected:
    void shutdown() override {}
};

struct posix_file_service : file_service
{
    using key_type = file_service;

    explicit posix_file_service(capy::execution_context&) {}
};

class io_context : public capy::execution_context
{
public:
    ~io_context()
    {
        shutdown();
        destroy();
    }
};

void configure_services(io_context& ctx)
{
    ctx.make_service<posix_file_service>();
    ctx.find_service<file_service>();       // returns posix_file_service*
    ctx.find_service<posix_file_service>(); // also works
}
// end::example[]
} // namespace ex_1

} // namespace
