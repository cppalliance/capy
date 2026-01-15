//
// Copyright (c) 2025 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_SRC_EX_DETAIL_STRAND_SERVICE_HPP
#define BOOST_CAPY_SRC_EX_DETAIL_STRAND_SERVICE_HPP

// Private header - implementation details

#include "strand_queue.hpp"
#include <boost/capy/ex/detail/strand_service.hpp>

#include <mutex>

namespace boost {
namespace capy {
namespace detail {

//----------------------------------------------------------

/** Implementation state for a strand.

    Each strand_impl provides serialization for coroutines
    dispatched through strands that share it.
*/
struct strand_impl
{
    std::mutex mutex_;
    strand_queue pending_;
    bool locked_ = false;
};

//----------------------------------------------------------

/** Internal implementation of strand_service.

    Holds the fixed pool of strand_impl objects.
*/
class strand_service::impl
{
public:
    static constexpr std::size_t num_impls = 211;

    strand_impl impls_[num_impls];
    std::size_t salt_ = 0;
    std::mutex mutex_;
};

} // namespace detail
} // namespace capy
} // namespace boost

#endif
