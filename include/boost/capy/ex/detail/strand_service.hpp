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
#include <boost/capy/ex/any_coro.hpp>
#include <boost/capy/ex/executor_ref.hpp>
#include <boost/capy/ex/execution_context.hpp>

#include <cstddef>

namespace boost {
namespace capy {

// Forward declaration (strand lives in capy, not detail)
template<typename Executor> class strand;

namespace detail {

// Forward declaration - full definition in src/
struct strand_impl;

/** Type trait to detect strand types. */
template<typename T>
struct is_strand : std::false_type {};

template<typename E>
struct is_strand<strand<E>> : std::true_type {};  // Uses forward decl from enclosing namespace

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
public:
    /** Destructor.
    */
    virtual ~strand_service();

    /** Return a pointer to a pooled implementation.

        Uses a hash to select an implementation from the pool.
        The salt is incremented after each call to distribute
        strands across the pool.

        @return Pointer to a strand_impl from the pool.
    */
    virtual strand_impl*
    get_implementation() = 0;

    /** Check if THIS thread is currently executing in the strand. */
    static bool
    running_in_this_thread(strand_impl& impl) noexcept;

    /** Dispatch through strand, returns handle for symmetric transfer. */
    static any_coro
    dispatch(strand_impl& impl, executor_ref ex, any_coro h);

    /** Post to strand queue. */
    static void
    post(strand_impl& impl, executor_ref ex, any_coro h);

protected:
    strand_service();
};

/** Return a reference to the strand service, creating it if needed.

    @param ctx The execution context.
    @return Reference to the strand service.
*/
BOOST_CAPY_DECL
strand_service&
get_strand_service(execution_context& ctx);

} // namespace detail
} // namespace capy
} // namespace boost

#endif
