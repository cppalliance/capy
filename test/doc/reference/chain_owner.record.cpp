//
// Copyright (c) 2026 Steve Gerbino
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Reference examples for boost::capy::chain_owner, injected into its documentation by
// doc/addons/extensions/reference-snippets.lua. Declared in:
//   include/boost/capy/ex/abandon.hpp
//
// The tagged regions are what the reference renders; the includes,
// suppressions and namespaces around them are scaffolding. Each region gets
// its own namespace so that examples which reuse a name still compile.

#include "../doc_warnings.hpp"

#include <boost/capy.hpp>

#include <coroutine>
#include <exception>

namespace capy = boost::capy;

namespace {

namespace ex_1 {

struct owner_root
{
    // tag::example[]
    // An owner's promise binds its chain_owner and names it in the
    // environment it hands to the segment it awaits.
    struct promise_type
        : capy::context_awaitable_promise_base<promise_type>
    {
        capy::chain_owner owner_;
        capy::await_context env_;

        template<class... Args>
        promise_type(capy::executor_ref ex, Args&&...)
            : env_{ex, {}, capy::get_current_frame_allocator(), &owner_}
        {
            this->set_environment(&env_);
        }

        owner_root get_return_object()
        {
            auto self =
                std::coroutine_handle<promise_type>::from_promise(*this);
            owner_.bind(self, env_.executor);
            return {self};
        }
        std::suspend_always initial_suspend() noexcept { return {}; }
        std::suspend_never final_suspend() noexcept { return {}; }
        void return_void() noexcept {}
        void unhandled_exception() noexcept { std::terminate(); }
    };
    // end::example[]

    std::coroutine_handle<promise_type> h;

    owner_root(std::coroutine_handle<promise_type> h_) noexcept : h(h_) {}
    owner_root(owner_root const&) = delete;

    ~owner_root()
    {
        if(h)
            h.destroy();
    }
};

owner_root serve(capy::executor_ref)
{
    co_return;
}

} // namespace ex_1

} // namespace
