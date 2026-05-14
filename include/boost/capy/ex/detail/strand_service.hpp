//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_EX_DETAIL_STRAND_SERVICE_HPP
#define BOOST_CAPY_EX_DETAIL_STRAND_SERVICE_HPP

#include <boost/capy/continuation.hpp>
#include <boost/capy/detail/config.hpp>
#include <coroutine>
#include <boost/capy/ex/executor_ref.hpp>
#include <boost/capy/ex/execution_context.hpp>

#include <memory>

namespace boost {
namespace capy {

template<typename Executor> class strand;

namespace detail {

struct strand_impl;

template<typename T>
struct is_strand : std::false_type {};

template<typename E>
struct is_strand<strand<E>> : std::true_type {};

/** Service that manages strand implementations.

    Allocates one `strand_impl` per strand. Maintains a shared pool of
    mutexes that strand_impls borrow, sized to keep memory bounded as
    strand count grows.

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

    /** Allocate a new strand implementation.

        Each call returns a fresh `strand_impl` owned by the returned
        `shared_ptr`. The implementation borrows a mutex from the
        service's shared pool.

        @return shared_ptr to the new strand_impl.
    */
    virtual std::shared_ptr<strand_impl>
    create_implementation() = 0;

    /** Check if THIS thread is currently executing in the strand. */
    static bool
    running_in_this_thread(strand_impl& impl) noexcept;

    /** Dispatch through strand; returns handle for symmetric transfer. */
    static std::coroutine_handle<>
    dispatch(
        std::shared_ptr<strand_impl> const& impl,
        executor_ref ex,
        continuation& c);

    /** Post to strand queue. */
    static void
    post(
        std::shared_ptr<strand_impl> const& impl,
        executor_ref ex,
        continuation& c);

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
