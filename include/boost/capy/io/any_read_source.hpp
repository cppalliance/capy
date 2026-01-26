//
// Copyright (c) 2025 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_IO_ANY_READ_SOURCE_HPP
#define BOOST_CAPY_IO_ANY_READ_SOURCE_HPP

#include <boost/capy/detail/config.hpp>
#include <boost/capy/buffers.hpp>
#include <boost/capy/buffers/buffer_param.hpp>
#include <boost/capy/concept/io_awaitable.hpp>
#include <boost/capy/concept/read_source.hpp>
#include <boost/capy/coro.hpp>
#include <boost/capy/ex/executor_ref.hpp>
#include <boost/capy/io_result.hpp>
#include <boost/capy/task.hpp>

#include <boost/system/error_code.hpp>

#include <concepts>
#include <coroutine>
#include <cstddef>
#include <span>
#include <stop_token>
#include <utility>

namespace boost {
namespace capy {

/** Type-erased wrapper for any ReadSource.

    This class provides type erasure for any type satisfying the
    @ref ReadSource concept, enabling runtime polymorphism for
    source read operations. It uses a cached coroutine frame to achieve
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
    some_source source;
    any_read_source rs(source);

    mutable_buffer buf(data, size);
    auto [ec, n] = co_await rs.read(std::span(&buf, 1));
    @endcode

    @see any_read_stream, ReadSource
*/
class any_read_source
{
    struct vtable;

    template<ReadSource S>
    struct vtable_for_impl;

    struct read_op;

    void* source_ = nullptr;
    vtable const* vt_ = nullptr;
    void* cached_frame_ = nullptr;
    std::size_t cached_size_ = 0;

    template<ReadSource S>
    static coro
    do_read_impl(
        void* source,
        any_read_source* wrapper,
        std::span<mutable_buffer const> buffers,
        coro h,
        executor_ref ex,
        std::stop_token token,
        system::error_code* ec,
        std::size_t* n);

    template<ReadSource S>
    static read_op
    read_coro(
        any_read_source* wrapper,
        S& source,
        std::span<mutable_buffer const> bufs,
        system::error_code* out_ec,
        std::size_t* out_n);

    void* alloc_frame(std::size_t size);
    void free_frame(void* p, std::size_t size);

public:
    /** Destructor.

        Releases the cached coroutine frame if any.
    */
    ~any_read_source()
    {
        if(cached_frame_)
            ::operator delete(cached_frame_);
    }

    /** Default constructor.

        Constructs an empty wrapper. Operations on a default-constructed
        wrapper result in undefined behavior.
    */
    any_read_source() = default;

    /** Non-copyable.

        The frame cache is per-instance and cannot be shared.
    */
    any_read_source(any_read_source const&) = delete;
    any_read_source& operator=(any_read_source const&) = delete;

    /** Move constructor.

        Transfers ownership of the wrapped source reference and
        cached frame from `other`. After the move, `other` is
        in a default-constructed state.

        @param other The wrapper to move from.
    */
    any_read_source(any_read_source&& other) noexcept
        : source_(std::exchange(other.source_, nullptr))
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
    any_read_source&
    operator=(any_read_source&& other) noexcept
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

    /** Construct from a ReadSource.

        Wraps the given source and preallocates the internal
        coroutine frame. The source must remain valid for the
        lifetime of this wrapper.

        @param s The source to wrap.
    */
    template<ReadSource S>
        requires (!std::same_as<std::decay_t<S>, any_read_source>)
    any_read_source(S& s) noexcept
        : source_(&s)
        , vt_(&vtable_for_impl<S>::value)
    {
        // Preallocate the coroutine frame
        read_coro<S>(this, s, {}, nullptr, nullptr);
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

    /** Initiate an asynchronous read operation.

        Reads data into the provided buffer sequence. The operation
        completes when the entire buffer sequence is filled, end-of-file
        is reached, or an error occurs.

        @param buffers The buffer sequence to read into. Passed by
            value to ensure the sequence lives in the coroutine frame
            across suspension points.

        @return An awaitable yielding `(error_code,std::size_t)`.

        @par Postconditions
        Exactly one of the following is true on return:
        @li **Success**: `!ec.failed()` and `n == buffer_size(buffers)`.
            The entire buffer was filled.
        @li **End-of-stream or Error**: `ec.failed()` and `n` indicates
            the number of bytes transferred before the failure.

        @par Preconditions
        The wrapper must contain a valid source (`has_value() == true`).
    */
    template<MutableBufferSequence MB>
    task<io_result<std::size_t>>
    read(MB buffers);

private:
    auto
    read_some_(std::span<mutable_buffer const> buffers);
};

//----------------------------------------------------------

struct any_read_source::vtable
{
    coro (*do_read)(
        void* source,
        any_read_source* wrapper,
        std::span<mutable_buffer const> buffers,
        coro h,
        executor_ref ex,
        std::stop_token token,
        system::error_code* ec,
        std::size_t* n);
};

template<ReadSource S>
struct any_read_source::vtable_for_impl
{
    static constexpr vtable value = {
        &any_read_source::do_read_impl<S>
    };
};

//----------------------------------------------------------

struct any_read_source::read_op
{
    struct promise_type
    {
        executor_ref executor_;
        std::stop_token stop_token_;
        coro caller_h_{};

        promise_type() = default;

        read_op
        get_return_object() noexcept
        {
            return read_op{
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
            any_read_source* wrapper,
            Args&&...)
        {
            return wrapper->alloc_frame(size);
        }

        template<class... Args>
        static void
        operator delete(void*, any_read_source*, Args&&...) noexcept
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

    ~read_op()
    {
        if(h_)
            h_.destroy();
    }

    read_op(read_op const&) = delete;
    read_op& operator=(read_op const&) = delete;

    read_op(read_op&& other) noexcept
        : h_(std::exchange(other.h_, nullptr))
    {
    }

    read_op& operator=(read_op&& other) noexcept
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
    read_op(std::coroutine_handle<promise_type> h) noexcept
        : h_(h)
    {
    }
};

//----------------------------------------------------------

inline void*
any_read_source::alloc_frame(std::size_t size)
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
any_read_source::free_frame(void*, std::size_t)
{
    // Keep the frame cached for reuse
}

template<ReadSource S>
any_read_source::read_op
any_read_source::read_coro(
    any_read_source*,
    S& source,
    std::span<mutable_buffer const> bufs,
    system::error_code* out_ec,
    std::size_t* out_n)
{
    auto [err, bytes] = co_await source.read(bufs);

    *out_ec = err;
    *out_n = bytes;
}

template<ReadSource S>
coro
any_read_source::do_read_impl(
    void* source,
    any_read_source* wrapper,
    std::span<mutable_buffer const> buffers,
    coro h,
    executor_ref ex,
    std::stop_token token,
    system::error_code* ec,
    std::size_t* n)
{
    auto& s = *static_cast<S*>(source);

    // Create coroutine - frame is cached in wrapper
    auto op = read_coro<S>(wrapper, s, buffers, ec, n);

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
any_read_source::read_some_(std::span<mutable_buffer const> buffers)
{
    struct awaitable
    {
        any_read_source* self_;
        std::span<mutable_buffer const> buffers_;
        system::error_code ec_;
        std::size_t n_ = 0;

        bool
        await_ready() const noexcept
        {
            return false;
        }

        coro
        await_suspend(coro h, executor_ref ex, std::stop_token token)
        {
            return self_->vt_->do_read(
                self_->source_,
                self_,
                buffers_,
                h,
                ex,
                token,
                &ec_,
                &n_);
        }

        io_result<std::size_t>
        await_resume() const noexcept
        {
            return {ec_, n_};
        }
    };
    return awaitable{this, buffers, {}, 0};
}

template<MutableBufferSequence MB>
task<io_result<std::size_t>>
any_read_source::read(MB buffers)
{
    buffer_param<MB> bp(std::move(buffers));
    std::size_t total = 0;

    for(;;)
    {
        auto bufs = bp.data();
        if(bufs.empty())
            break;

        auto [ec, n] = co_await read_some_(bufs);
        total += n;
        if(ec.failed())
            co_return {ec, total};
        bp.consume(n);
    }

    co_return {{}, total};
}

} // namespace capy
} // namespace boost

#endif
