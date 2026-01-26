//
// Copyright (c) 2025 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_IO_ANY_BUFFER_SOURCE_HPP
#define BOOST_CAPY_IO_ANY_BUFFER_SOURCE_HPP

#include <boost/capy/detail/config.hpp>
#include <boost/capy/buffers.hpp>
#include <boost/capy/concept/buffer_source.hpp>
#include <boost/capy/concept/io_awaitable.hpp>
#include <boost/capy/coro.hpp>
#include <boost/capy/ex/executor_ref.hpp>
#include <boost/capy/io_result.hpp>

#include <boost/system/error_code.hpp>

#include <concepts>
#include <coroutine>
#include <cstddef>
#include <exception>
#include <stop_token>
#include <utility>

namespace boost {
namespace capy {

/** Type-erased wrapper for any BufferSource.

    This class provides type erasure for any type satisfying the
    @ref BufferSource concept, enabling runtime polymorphism for
    buffer pull operations. It uses a cached coroutine frame to achieve
    zero steady-state allocation after construction.

    The wrapper has reference semantics - it wraps an existing
    source without taking ownership. The wrapped source must
    outlive this wrapper.

    @par Frame Preallocation
    The constructor preallocates the internal coroutine frame.
    This reserves all virtual address space at server startup
    so memory usage can be measured up front, rather than
    allocating piecemeal as traffic arrives.

    @par Thread Safety
    Not thread-safe. Concurrent operations on the same wrapper
    are undefined behavior.

    @par Example
    @code
    some_buffer_source src;
    any_buffer_source abs(src);

    const_buffer arr[16];
    auto [ec, count] = co_await abs.pull(arr, 16);
    @endcode

    @see any_write_sink, BufferSource
*/
class any_buffer_source
{
    struct vtable;

    template<BufferSource S>
    struct vtable_for_impl;

    struct pull_op;

    void* source_ = nullptr;
    vtable const* vt_ = nullptr;
    void* cached_frame_ = nullptr;
    std::size_t cached_size_ = 0;

    template<BufferSource S>
    static coro
    do_pull_impl(
        void* source,
        any_buffer_source* wrapper,
        const_buffer* arr,
        std::size_t max_count,
        coro h,
        executor_ref ex,
        std::stop_token token,
        system::error_code* ec,
        std::size_t* count);

    template<BufferSource S>
    static pull_op
    pull_coro(
        any_buffer_source* wrapper,
        S& source,
        const_buffer* arr,
        std::size_t max_count,
        system::error_code* out_ec,
        std::size_t* out_count);

    void* alloc_frame(std::size_t size);
    void free_frame(void* p, std::size_t size);

public:
    /** Destructor.

        Releases the cached coroutine frame if any.
    */
    ~any_buffer_source()
    {
        if(cached_frame_)
            ::operator delete(cached_frame_);
    }

    /** Default constructor.

        Constructs an empty wrapper. Operations on a default-constructed
        wrapper result in undefined behavior.
    */
    any_buffer_source() = default;

    /** Non-copyable.

        The frame cache is per-instance and cannot be shared.
    */
    any_buffer_source(any_buffer_source const&) = delete;
    any_buffer_source& operator=(any_buffer_source const&) = delete;

    /** Move constructor.

        Transfers ownership of the wrapped source reference and
        cached frame from `other`. After the move, `other` is
        in a default-constructed state.

        @param other The wrapper to move from.
    */
    any_buffer_source(any_buffer_source&& other) noexcept
        : source_(std::exchange(other.source_, nullptr))
        , vt_(std::exchange(other.vt_, nullptr))
        , cached_frame_(std::exchange(other.cached_frame_, nullptr))
        , cached_size_(std::exchange(other.cached_size_, 0))
    {
    }

    /** Rebinding move constructor.

        Transfers the cached frame and vtable from `other`, but binds
        to a new source object. Used by owning wrappers when the owned
        object moves to a new location.

        @param other The wrapper to move state from.
        @param new_source The new source to bind to. Must be the same
            type as the original source.
    */
    template<BufferSource S>
    any_buffer_source(any_buffer_source&& other, S& new_source) noexcept
        : source_(&new_source)
        , vt_(std::exchange(other.vt_, nullptr))
        , cached_frame_(std::exchange(other.cached_frame_, nullptr))
        , cached_size_(std::exchange(other.cached_size_, 0))
    {
    }

    /** Move assignment operator.

        Releases any existing cached frame, then transfers ownership
        from `other`.

        @param other The wrapper to move from.
        @return Reference to this wrapper.
    */
    any_buffer_source&
    operator=(any_buffer_source&& other) noexcept
    {
        if(this != &other)
        {
            if(cached_frame_)
                ::operator delete(cached_frame_);
            source_ = std::exchange(other.source_, nullptr);
            vt_ = std::exchange(other.vt_, nullptr);
            cached_frame_ = std::exchange(other.cached_frame_, nullptr);
            cached_size_ = std::exchange(other.cached_size_, 0);
        }
        return *this;
    }

    /** Construct from a BufferSource.

        Wraps the given source and preallocates the internal
        coroutine frame. The source must remain valid for the
        lifetime of this wrapper.

        @param s The source to wrap.
    */
    template<BufferSource S>
        requires (!std::same_as<std::decay_t<S>, any_buffer_source>)
    any_buffer_source(S& s) noexcept
        : source_(&s)
        , vt_(&vtable_for_impl<S>::value)
    {
        // Preallocate coroutine frame to find max size
        pull_coro<S>(this, s, nullptr, 0, nullptr, nullptr);
    }

    /** Check if the wrapper contains a valid source.

        @return `true` if wrapping a source, `false` if default-constructed
            or moved-from.
    */
    bool
    has_value() const noexcept
    {
        return source_ != nullptr;
    }

    /** Check if the wrapper contains a valid source.

        @return `true` if wrapping a source, `false` if default-constructed
            or moved-from.
    */
    explicit
    operator bool() const noexcept
    {
        return has_value();
    }

    /** Pull buffer data from the source.

        Fills the provided array with buffer descriptors from the
        underlying source. The operation completes when data is
        available, the source is exhausted, or an error occurs.

        @param arr Pointer to array of const_buffer to fill.
        @param max_count Maximum number of buffers to fill.

        @return An awaitable yielding `(error_code,std::size_t)`.
            On success with data, `count > 0` indicates buffers filled.
            On success with `count == 0`, source is exhausted.

        @par Preconditions
        The wrapper must contain a valid source (`has_value() == true`).
    */
    auto
    pull(const_buffer* arr, std::size_t max_count);

protected:
    /** Rebind to a new source after move.

        Updates the internal pointer to reference a new source object.
        Used by owning wrappers after move assignment when the owned
        object has moved to a new location.

        @param new_source The new source to bind to. Must be the same
            type as the original source.

        @note Terminates if called with a source of different type
            than the original.
    */
    template<BufferSource S>
    void
    rebind(S& new_source) noexcept
    {
        if(vt_ != &vtable_for_impl<S>::value)
            std::terminate();
        source_ = &new_source;
    }
};

//----------------------------------------------------------

struct any_buffer_source::vtable
{
    coro (*do_pull)(
        void* source,
        any_buffer_source* wrapper,
        const_buffer* arr,
        std::size_t max_count,
        coro h,
        executor_ref ex,
        std::stop_token token,
        system::error_code* ec,
        std::size_t* count);
};

template<BufferSource S>
struct any_buffer_source::vtable_for_impl
{
    static constexpr vtable value = {
        &any_buffer_source::do_pull_impl<S>
    };
};

//----------------------------------------------------------

struct any_buffer_source::pull_op
{
    struct promise_type
    {
        executor_ref executor_;
        std::stop_token stop_token_;
        coro caller_h_{};

        promise_type() = default;

        pull_op
        get_return_object() noexcept
        {
            return pull_op{
                std::coroutine_handle<promise_type>::from_promise(*this)};
        }

        std::suspend_always
        initial_suspend() noexcept
        {
            return {};
        }

        auto
        final_suspend() noexcept
        {
            struct awaiter
            {
                promise_type* p_;

                bool await_ready() const noexcept { return false; }

                coro await_suspend(coro) const noexcept
                {
                    if(p_->caller_h_)
                        return p_->caller_h_;
                    return std::noop_coroutine();
                }

                void await_resume() const noexcept {}
            };
            return awaiter{this};
        }

        void
        return_void() noexcept
        {
        }

        void
        unhandled_exception()
        {
            throw;
        }

        template<class... Args>
        static void*
        operator new(
            std::size_t size,
            any_buffer_source* wrapper,
            Args&&...)
        {
            return wrapper->alloc_frame(size);
        }

        template<class... Args>
        static void
        operator delete(void*, any_buffer_source*, Args&&...) noexcept
        {
        }

        static void
        operator delete(void*, std::size_t) noexcept
        {
        }

        void
        set_executor(executor_ref ex) noexcept
        {
            executor_ = ex;
        }

        void
        set_stop_token(std::stop_token token) noexcept
        {
            stop_token_ = token;
        }

        void
        set_caller(coro h) noexcept
        {
            caller_h_ = h;
        }

        template<class Awaitable>
        struct transform_awaiter
        {
            std::decay_t<Awaitable> a_;
            promise_type* p_;

            bool await_ready()
            {
                return a_.await_ready();
            }

            auto await_resume()
            {
                return a_.await_resume();
            }

            auto await_suspend(coro h)
            {
                return a_.await_suspend(h, p_->executor_, p_->stop_token_);
            }
        };

        template<class Awaitable>
        auto await_transform(Awaitable&& a)
        {
            using A = std::decay_t<Awaitable>;
            if constexpr (IoAwaitable<A>)
            {
                return transform_awaiter<Awaitable>{
                    std::forward<Awaitable>(a), this};
            }
            else
            {
                static_assert(sizeof(A) == 0, "requires IoAwaitable");
            }
        }
    };

    std::coroutine_handle<promise_type> h_;

    ~pull_op()
    {
        if(h_)
            h_.destroy();
    }

    pull_op(pull_op const&) = delete;
    pull_op& operator=(pull_op const&) = delete;

    pull_op(pull_op&& other) noexcept
        : h_(std::exchange(other.h_, nullptr))
    {
    }

    pull_op& operator=(pull_op&& other) noexcept
    {
        if(this != &other)
        {
            if(h_)
                h_.destroy();
            h_ = std::exchange(other.h_, nullptr);
        }
        return *this;
    }

private:
    explicit
    pull_op(std::coroutine_handle<promise_type> h) noexcept
        : h_(h)
    {
    }
};

//----------------------------------------------------------

inline void*
any_buffer_source::alloc_frame(std::size_t size)
{
    if(cached_frame_ && cached_size_ >= size)
        return cached_frame_;

    if(cached_frame_)
        ::operator delete(cached_frame_);

    cached_frame_ = ::operator new(size);
    cached_size_ = size;
    return cached_frame_;
}

inline void
any_buffer_source::free_frame(void*, std::size_t)
{
    // Keep the frame cached for reuse
}

template<BufferSource S>
any_buffer_source::pull_op
any_buffer_source::pull_coro(
    any_buffer_source*,
    S& source,
    const_buffer* arr,
    std::size_t max_count,
    system::error_code* out_ec,
    std::size_t* out_count)
{
    auto [err, count] = co_await source.pull(arr, max_count);

    *out_ec = err;
    *out_count = count;
}

template<BufferSource S>
coro
any_buffer_source::do_pull_impl(
    void* source,
    any_buffer_source* wrapper,
    const_buffer* arr,
    std::size_t max_count,
    coro h,
    executor_ref ex,
    std::stop_token token,
    system::error_code* ec,
    std::size_t* count)
{
    auto& s = *static_cast<S*>(source);

    // Create coroutine - frame is cached in wrapper
    auto op = pull_coro<S>(wrapper, s, arr, max_count, ec, count);

    // Set executor and stop token on promise before resuming
    op.h_.promise().set_executor(ex);
    op.h_.promise().set_stop_token(token);

    // Resume the coroutine to start the operation
    op.h_.resume();

    // Check if operation completed synchronously
    if(op.h_.done())
    {
        op.h_.destroy();
        op.h_ = nullptr;
        return ex.dispatch(h);
    }

    // Operation is pending - caller will be resumed via symmetric transfer
    op.h_.promise().set_caller(h);
    op.h_ = nullptr;
    return std::noop_coroutine();
}

//----------------------------------------------------------

inline auto
any_buffer_source::pull(
    const_buffer* arr,
    std::size_t max_count)
{
    struct awaitable
    {
        any_buffer_source* self_;
        const_buffer* arr_;
        std::size_t max_count_;
        system::error_code ec_;
        std::size_t count_ = 0;

        bool
        await_ready() const noexcept
        {
            return false;
        }

        coro
        await_suspend(coro h, executor_ref ex, std::stop_token token)
        {
            return self_->vt_->do_pull(
                self_->source_,
                self_,
                arr_,
                max_count_,
                h,
                ex,
                token,
                &ec_,
                &count_);
        }

        io_result<std::size_t>
        await_resume() const noexcept
        {
            return {ec_, count_};
        }
    };
    return awaitable{this, arr, max_count, {}, 0};
}

} // namespace capy
} // namespace boost

#endif
