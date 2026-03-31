//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#include <boost/capy/ex/execution_context.hpp>
#include <boost/capy/ex/recycling_memory_resource.hpp>
#include <boost/capy/detail/except.hpp>

namespace boost {
namespace capy {

execution_context::
execution_context()
    : frame_alloc_(get_recycling_memory_resource())
{
}

execution_context::
~execution_context()
{
    shutdown();
    destroy();
}

void
execution_context::
shutdown() noexcept
{
    if(shutdown_)
        return;
    shutdown_ = true;

    service* p = head_;
    while(p)
    {
        p->shutdown();
        p = p->next_;
    }
}

void
execution_context::
destroy() noexcept
{
    service* p = head_;
    head_ = nullptr;
    while(p)
    {
        service* next = p->next_;
        delete p;
        p = next;
    }
    for(auto& s : slots_)
        s.store(nullptr, std::memory_order_relaxed);
}

execution_context::service*
execution_context::
find_impl(detail::type_index ti) const noexcept
{
    auto p = head_;
    while(p)
    {
        if(p->t0_ == ti || p->t1_ == ti)
            break;
        p = p->next_;
    }
    return p;
}

execution_context::service&
execution_context::
use_service_impl(factory& f)
{
    std::unique_lock<std::mutex> lock(mutex_);

    if(auto* p = find_impl(f.t0))
    {
        if(f.slot0 < max_service_slots)
            slots_[f.slot0].store(p, std::memory_order_release);
        if(f.slot0 != f.slot1 && f.slot1 < max_service_slots)
            slots_[f.slot1].store(p, std::memory_order_release);
        return *p;
    }

    lock.unlock();

    // Create the service outside lock, enabling nested calls
    service* sp = f.create(*this);
    sp->t0_ = f.t0;
    sp->t1_ = f.t1;

    lock.lock();

    if(auto* p = find_impl(f.t0))
    {
        if(f.slot0 < max_service_slots)
            slots_[f.slot0].store(p, std::memory_order_release);
        if(f.slot0 != f.slot1 && f.slot1 < max_service_slots)
            slots_[f.slot1].store(p, std::memory_order_release);
        delete sp;
        return *p;
    }

    sp->next_ = head_;
    head_ = sp;

    if(f.slot0 < max_service_slots)
        slots_[f.slot0].store(sp, std::memory_order_release);
    if(f.slot0 != f.slot1 && f.slot1 < max_service_slots)
        slots_[f.slot1].store(sp, std::memory_order_release);

    return *sp;
}

execution_context::service&
execution_context::
make_service_impl(factory& f)
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if(find_impl(f.t0))
            detail::throw_invalid_argument();
        if(f.t0 != f.t1 && find_impl(f.t1))
            detail::throw_invalid_argument();
    }

    // Unlocked to allow nested service creation from constructor
    service* p = f.create(*this);

    std::lock_guard<std::mutex> lock(mutex_);
    if(find_impl(f.t0))
    {
        delete p;
        detail::throw_invalid_argument();
    }

    p->t0_ = f.t0;
    if(f.t0 != f.t1)
    {
        if(find_impl(f.t1))
        {
            delete p;
            detail::throw_invalid_argument();
        }
        p->t1_ = f.t1;
    }
    else
    {
        p->t1_ = f.t0;
    }

    p->next_ = head_;
    head_ = p;

    if(f.slot0 < max_service_slots)
        slots_[f.slot0].store(p, std::memory_order_release);
    if(f.slot0 != f.slot1 && f.slot1 < max_service_slots)
        slots_[f.slot1].store(p, std::memory_order_release);

    return *p;
}

} // namespace capy
} // namespace boost
