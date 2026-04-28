//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_SRC_EX_DETAIL_STRAND_IMPL_HPP
#define BOOST_CAPY_SRC_EX_DETAIL_STRAND_IMPL_HPP

#include "src/ex/detail/strand_queue.hpp"
#include <boost/capy/detail/intrusive.hpp>
#include <atomic>
#include <mutex>
#include <thread>

namespace boost {
namespace capy {
namespace detail {

class strand_service_impl;

/** Implementation state for a single strand.

    Each strand owns one of these via shared_ptr. The mutex is borrowed
    from the service's shared pool. The intrusive_list base links this
    impl into the service's list of live impls for shutdown traversal.
*/
struct strand_impl
    : intrusive_list<strand_impl>::node
{
    std::mutex* mutex_ = nullptr;
    strand_queue pending_;
    bool locked_ = false;
    std::atomic<std::thread::id> dispatch_thread_{};

    std::atomic<strand_service_impl*> service_{nullptr};

    ~strand_impl();
};

} // namespace detail
} // namespace capy
} // namespace boost

#endif
