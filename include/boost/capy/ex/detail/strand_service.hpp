//
// Copyright (c) 2025 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_EX_DETAIL_STRAND_SERVICE_HPP
#define BOOST_CAPY_EX_DETAIL_STRAND_SERVICE_HPP

#include <boost/capy/detail/config.hpp>
#include <boost/capy/ex/execution_context.hpp>

#include <cstddef>

namespace boost {
namespace capy {
namespace detail {

// Forward declaration - full definition in src/
struct strand_impl;

//----------------------------------------------------------

/** Service that manages pooled strand implementations.

    This service maintains a fixed pool of strand_impl objects.
    When a strand is constructed, it obtains a pointer to one
    of these pooled implementations based on a hash.

    @par Thread Safety
    The service operations are thread-safe.
*/
class BOOST_CAPY_DECL strand_service
    : public execution_context::service
{
    class impl;
    impl* impl_;

public:
    /** Construct the strand service.

        @param ctx The owning execution context.
    */
    explicit
    strand_service(execution_context& ctx);

    /** Destructor.
    */
    ~strand_service();

    /** Return a pointer to a pooled implementation.

        Uses a hash to select an implementation from the pool.
        The salt is incremented after each call to distribute
        strands across the pool.

        @return Pointer to a strand_impl from the pool.
    */
    strand_impl*
    get_implementation();

protected:
    /** Shut down the service.

        Called when the owning execution context shuts down.
    */
    void
    shutdown() override;
};

} // namespace detail
} // namespace capy
} // namespace boost

#endif
