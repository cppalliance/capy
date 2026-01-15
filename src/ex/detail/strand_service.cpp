//
// Copyright (c) 2025 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#include "strand_service.hpp"
#include <boost/capy/ex/any_coro.hpp>

namespace boost {
namespace capy {
namespace detail {

strand_service::
strand_service(execution_context& ctx)
    : service()
    , impl_(new impl)
{
    (void)ctx;
}

strand_service::
~strand_service()
{
    delete impl_;
}

strand_impl*
strand_service::
get_implementation()
{
    std::lock_guard<std::mutex> lock(impl_->mutex_);

    // Hash the salt to select an impl from the pool
    std::size_t index = impl_->salt_++;
    index = index % impl::num_impls;

    return &impl_->impls_[index];
}

void
strand_service::
shutdown()
{
    // Clear pending operations from all impls
    for(std::size_t i = 0; i < impl::num_impls; ++i)
    {
        std::lock_guard<std::mutex> lock(impl_->impls_[i].mutex_);
        // Mark as locked to prevent new work
        impl_->impls_[i].locked_ = true;
    }
}

//----------------------------------------------------------

BOOST_CAPY_DECL
void
strand_enqueue(
    strand_impl& impl,
    any_coro h,
    bool& should_run)
{
    std::lock_guard<std::mutex> lock(impl.mutex_);

    impl.pending_.push(h);

    if(!impl.locked_)
    {
        impl.locked_ = true;
        should_run = true;
    }
    else
    {
        should_run = false;
    }
}

BOOST_CAPY_DECL
void
strand_dispatch_pending(strand_impl& impl)
{
    impl.pending_.dispatch();
}

BOOST_CAPY_DECL
void
strand_unlock(strand_impl& impl)
{
    std::lock_guard<std::mutex> lock(impl.mutex_);
    impl.locked_ = false;
}

BOOST_CAPY_DECL
bool
strand_running_in_this_thread(strand_impl& impl) noexcept
{
    std::lock_guard<std::mutex> lock(impl.mutex_);
    return impl.locked_;
}

} // namespace detail
} // namespace capy
} // namespace boost
