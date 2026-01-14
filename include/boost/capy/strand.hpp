//
// Copyright (c) 2025 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_STRAND_HPP
#define BOOST_CAPY_STRAND_HPP

#include <boost/capy/detail/config.hpp>
#include <boost/capy/ex/coro.hpp>
#include <boost/capy/core/intrusive_queue.hpp>

#include <memory>
#include <mutex>

namespace boost {
namespace capy {

namespace detail {

/** Node type for strand's intrusive queue.
*/
struct strand_node : intrusive_queue<strand_node>::node
{
    coro h_;

    explicit strand_node(coro h) noexcept
        : h_(h)
    {
    }
};

/** Shared state for strand serialization.
*/
struct strand_impl
{
    std::mutex mutex_;
    intrusive_queue<strand_node> pending_;
    bool locked_ = false;
};

} // namespace detail

/** Provides serialized coroutine dispatch for any executor type.

    A strand wraps an inner executor and ensures that coroutines
    dispatched through it are serialized - at most one runs at a time.
    This is a simplified implementation for benchmarking purposes.

    The strand is copyable; all copies share the same serialization
    state via a shared pointer.

    @tparam Executor The type of the underlying executor.
*/
template<typename Executor>
class strand
{
    Executor ex_;
    std::shared_ptr<detail::strand_impl> impl_;

public:
    /** The type of the underlying executor.
    */
    using inner_executor_type = Executor;

    /** Construct a strand for the specified executor.

        @param ex The inner executor to wrap.
    */
    explicit strand(Executor ex)
        : ex_(std::move(ex))
        , impl_(std::make_shared<detail::strand_impl>())
    {
    }

    /** Obtain the underlying executor.
    */
    Executor const&
    get_inner_executor() const noexcept
    {
        return ex_;
    }

    /** Obtain the underlying execution context.
    */
    decltype(auto)
    context() const noexcept
    {
        return ex_.context();
    }

    /** Determine whether the strand is running in the current thread.

        @return @c true if the strand is currently locked.
    */
    bool
    running_in_this_thread() const noexcept
    {
        std::lock_guard<std::mutex> lock(impl_->mutex_);
        return impl_->locked_;
    }

    /** Compare two strands for equality.

        Two strands are equal if they share the same internal state.
    */
    bool
    operator==(strand const& other) const noexcept
    {
        return impl_ == other.impl_;
    }

    /** Dispatch a coroutine through the strand.

        This function serializes coroutine execution. If no other
        coroutine is currently running on the strand, the coroutine
        is dispatched immediately through the inner executor.
        Otherwise, it is queued for later execution.

        @param h The coroutine handle to dispatch.

        @return A coroutine handle for symmetric transfer.
    */
    coro
    operator()(coro h) const
    {
        auto* node = new detail::strand_node(h);

        std::lock_guard<std::mutex> lock(impl_->mutex_);
        impl_->pending_.push(node);

        if(impl_->locked_)
            return std::noop_coroutine();

        impl_->locked_ = true;
        return run_pending();
    }

private:
    /** Run pending coroutines.

        Must be called with mutex held and locked_ == true.
    */
    coro
    run_pending() const
    {
        auto* node = impl_->pending_.pop();
        if(!node)
        {
            impl_->locked_ = false;
            return std::noop_coroutine();
        }

        coro h = node->h_;
        delete node;

        // Dispatch through inner executor
        return ex_(h);
    }
};

/** Create a strand for an executor.

    @param ex An executor.

    @returns A strand constructed with the specified executor.
*/
template<typename Executor>
strand<Executor>
make_strand(Executor const& ex)
{
    return strand<Executor>(ex);
}

} // namespace capy
} // namespace boost

#endif
