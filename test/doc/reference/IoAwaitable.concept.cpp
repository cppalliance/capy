//
// Copyright (c) 2026 Michael Vandeberg
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Reference examples for boost::capy::IoAwaitable, injected into its documentation by
// doc/addons/extensions/reference-snippets.lua. Declared in:
//   include/boost/capy/concept/io_awaitable.hpp
//
// The tagged regions are what the reference renders; the includes,
// suppressions and namespaces around them are scaffolding. Each region gets
// its own namespace so that examples which reuse a name still compile.

#include "../doc_warnings.hpp"

#include <boost/capy.hpp>

#include <cassert>
#include <system_error>
#include <chrono>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace capy = boost::capy;

namespace {

namespace ex_1 {
// Stands in for a real asynchronous entry point -- a socket read, a timer --
// that takes a stop token and invokes the completion handler when the
// operation finishes. Scaffolding: the reference renders only the tagged
// region below, which is what the docstring's prose describes.
template<class Completion>
void start_my_io_op(std::stop_token, Completion&&) {}

// tag::example[]
class my_awaitable
{
    capy::io_env const* env_ = nullptr;
    capy::continuation  cont_;
    std::error_code     ec_ {};

public:
    bool await_ready() const noexcept { return false; }

    std::coroutine_handle<>
    await_suspend(std::coroutine_handle<> h, capy::io_env const* env) noexcept
    {
        env_  = env;                      // store the pointer, never a copy
        cont_ = capy::continuation{h};

        auto completion = [this](std::error_code ec) noexcept
        {
            ec_ = ec;                     // publish result; touch *this
            env_->executor.post(cont_);   // only before post, never after
        };

        start_my_io_op(env_->stop_token, completion);

        return std::noop_coroutine(); // go back to scheduler
    }

    capy::io_result<> await_resume() const noexcept { return {ec_}; }
};
// end::example[]

static_assert( capy::IoAwaitable<my_awaitable> );
} // namespace ex_1

} // namespace
