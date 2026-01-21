//
// Copyright (c) 2026 Cinar Gursoy
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_EX_STOP_TOKEN_HPP
#define BOOST_CAPY_EX_STOP_TOKEN_HPP

#include <boost/capy/detail/config.hpp>

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

namespace boost {
namespace capy {

namespace detail {

struct stop_state
    {
        std::atomic<bool> stop_requested_{false};
        std::mutex mutex_;
        std::vector<std::function<void()>> callbacks_;

        bool request_stop() noexcept
        {
            bool expected = false;
            if (!stop_requested_.compare_exchange_strong(expected, true))
                return false;  // Already requested

            // Invoke all callbacks
            std::vector<std::function<void()>> cbs;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                cbs.swap(callbacks_);
            }
            for (auto& cb : cbs)
            {
                try
                {
                    cb();
                }
                catch (...)
                {
                    // Callbacks should not throw, but ignore if they do
                }
            }
            return true;
        }

        void register_callback(std::function<void()> cb)
        {
            // If already stopped, invoke immediately
            if (stop_requested_.load(std::memory_order_acquire))
            {
                try
                {
                    cb();
                }
                catch (...)
                {
                    // Ignore exceptions from callbacks
                }
                return;
            }

            // Register for later invocation
            std::lock_guard<std::mutex> lock(mutex_);
            if (stop_requested_.load(std::memory_order_acquire))
            {
                // Stop was requested while we were acquiring lock
                try
                {
                    cb();
                }
                catch (...)
                {
                    // Ignore exceptions
                }
            }
            else
            {
                callbacks_.push_back(std::move(cb));
            }
        }

    };

} // namespace detail

/** A token that can be checked for cancellation requests.

    A `stop_token` can be checked to see if a stop request has been made.
    Multiple tokens can share the same stop state, allowing coordinated
    cancellation across multiple operations.

    @see stop_source, stop_callback
*/
class stop_token
{
    std::shared_ptr<detail::stop_state> state_;

    explicit stop_token(std::shared_ptr<detail::stop_state> state) noexcept
        : state_(std::move(state))
    {
    }

    friend class stop_source;
    template<class Callback>
    friend class stop_callback;

public:
    /** Default constructor.

        Creates a stop token that cannot be stopped.
        `stop_possible()` returns `false` for default-constructed tokens.
    */
    stop_token() noexcept = default;

    /** Copy constructor.

        Creates a copy that shares the same stop state.
    */
    stop_token(stop_token const&) noexcept = default;

    /** Copy assignment.

        Assigns the stop state from another token.
    */
    stop_token& operator=(stop_token const&) noexcept = default;

    /** Move constructor.

        Moves the stop state from another token.
    */
    stop_token(stop_token&&) noexcept = default;

    /** Move assignment.

        Moves the stop state from another token.
    */
    stop_token& operator=(stop_token&&) noexcept = default;

    /** Check if a stop request has been made.

        @return `true` if a stop request has been made, `false` otherwise.
    */
    bool stop_requested() const noexcept
    {
        if (!state_)
            return false;
        return state_->stop_requested_.load(std::memory_order_acquire);
    }

    /** Check if this token can receive stop requests.

        @return `true` if this token is associated with a stop source,
                `false` for default-constructed tokens.
    */
    bool stop_possible() const noexcept
    {
        return state_ != nullptr;
    }

    /** Swap with another stop token.

        @param other The token to swap with.
    */
    void swap(stop_token& other) noexcept
    {
        state_.swap(other.state_);
    }
};

/** Swap two stop tokens.

    @param lhs First token.
    @param rhs Second token.
*/
inline void swap(stop_token& lhs, stop_token& rhs) noexcept
{
    lhs.swap(rhs);
}

//----------------------------------------------------------
//
// stop_source
//
//----------------------------------------------------------

/** A source for stop tokens that can request cancellation.

    A `stop_source` creates `stop_token` objects and can request
    cancellation on all tokens it has created. Multiple tokens from
    the same source share the same stop state.

    @par Thread Safety
    Distinct objects: Safe.
    Shared objects: Safe.

    @par Example
    @code
    capy::stop_source source;
    auto token = source.get_token();

    // Start operation with token
    start_operation(token);

    // Later, cancel the operation
    source.request_stop();
    @endcode

    @see stop_token, stop_callback
*/
class stop_source
{
    std::shared_ptr<detail::stop_state> state_;

public:
    /** Default constructor.

        Creates a stop source that can create stop tokens.
    */
    stop_source()
        : state_(std::make_shared<detail::stop_state>())
    {
    }

    /** Copy constructor.

        Creates a new stop source that shares the same stop state
        as the source being copied. Both sources can request stop
        and affect the same tokens.
    */
    stop_source(stop_source const&) = default;

    /** Copy assignment.

        Assigns the stop state from another source.
    */
    stop_source& operator=(stop_source const&) = default;

    /** Move constructor.

        Moves the stop state from another source.
    */
    stop_source(stop_source&&) noexcept = default;

    /** Move assignment.

        Moves the stop state from another source.
    */
    stop_source& operator=(stop_source&&) noexcept = default;

    /** Get a stop token associated with this source.

        All tokens returned from the same source share the same
        stop state. Requesting stop on the source affects all
        tokens created from it.

        @return A stop token that can be checked for cancellation.
    */
    stop_token get_token() const noexcept
    {
        return stop_token(state_);
    }

    /** Request stop on all tokens created from this source.

        This function is idempotent—calling it multiple times
        has the same effect as calling it once. After the first
        call, all subsequent calls return `false`.

        @return `true` if this was the first stop request,
                `false` if stop was already requested.
    */
    bool request_stop() noexcept
    {
        if (!state_)
            return false;
        return state_->request_stop();
    }

    /** Check if stop has been requested.

        @return `true` if `request_stop()` has been called,
                `false` otherwise.
    */
    bool stop_requested() const noexcept
    {
        if (!state_)
            return false;
        return state_->stop_requested_.load(std::memory_order_acquire);
    }
};

//----------------------------------------------------------
//
// stop_callback
//
//----------------------------------------------------------

/** Register a callback to be invoked when stop is requested.

    The callback is invoked when the associated stop token receives
    a stop request. If stop has already been requested when the
    callback is constructed, it is invoked immediately.

    The callback is automatically unregistered when the `stop_callback`
    object is destroyed.

    @tparam Callback The type of the callback function object.
                     Must be callable with no arguments.

    @par Thread Safety
    Distinct objects: Safe.
    Shared objects: Unsafe.

    @par Example
    @code
    capy::stop_source source;
    auto token = source.get_token();

    // Register callback
    capy::stop_callback cb(token, [] {
        std::cout << "Stop requested!\n";
    });

    // Requesting stop will invoke the callback
    source.request_stop();
    @endcode

    @see stop_token, stop_source
*/
template<class Callback>
class stop_callback
{
    std::shared_ptr<detail::stop_state> state_;
    std::shared_ptr<Callback> callback_;

public:
    /** Construct a stop callback.

        Registers the callback to be invoked when the stop token
        receives a stop request. If stop has already been requested,
        the callback is invoked immediately during construction.

        @param st The stop token to monitor.
        @param cb The callback to invoke when stop is requested.
    */
    explicit stop_callback(stop_token const& st, Callback&& cb)
        : state_(st.state_)
        , callback_(std::make_shared<Callback>(std::forward<Callback>(cb)))
    {
        if (!state_)
            return;  // No-op token

        // Register callback with shared_ptr to keep it alive
        auto weak_cb = std::weak_ptr<Callback>(callback_);
        state_->register_callback([weak_cb] {
            if (auto cb = weak_cb.lock())
            {
                (*cb)();
            }
        });
    }

    /** Destructor.

        The callback may still be invoked after destruction if
        stop is requested, as long as the callback object itself
        remains valid (via shared_ptr).
    */
    ~stop_callback() = default;

    // Non-copyable, non-movable
    stop_callback(stop_callback const&) = delete;
    stop_callback& operator=(stop_callback const&) = delete;
    stop_callback(stop_callback&&) = delete;
    stop_callback& operator=(stop_callback&&) = delete;
};

} // namespace capy
} // namespace boost

#endif
