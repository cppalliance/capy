//
// Copyright (c) 2025 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_IO_ANY_READ_STREAM_HPP
#define BOOST_CAPY_IO_ANY_READ_STREAM_HPP

#include <boost/capy/detail/config.hpp>
#include <boost/capy/buffers.hpp>
#include <boost/capy/buffers/buffer_param.hpp>
#include <boost/capy/concept/io_awaitable.hpp>
#include <boost/capy/concept/read_stream.hpp>
#include <boost/capy/coro.hpp>
#include <boost/capy/ex/executor_ref.hpp>
#include <boost/capy/io_result.hpp>

#include <system_error>

#include <concepts>
#include <coroutine>
#include <cstddef>
#include <exception>
#include <span>
#include <stop_token>
#include <utility>

namespace boost {
namespace capy {

/** Type-erased wrapper for any ReadStream.

    This class provides type erasure for any type satisfying the
    @ref ReadStream concept, enabling runtime polymorphism for
    read operations. It uses a cached coroutine frame to achieve
    zero steady-state allocation after construction.

    The wrapper supports two construction modes:
    - **Owning**: Pass by value to transfer ownership. The wrapper
      allocates storage and owns the stream.
    - **Reference**: Pass a pointer to wrap without ownership. The
      pointed-to stream must outlive this wrapper.

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
    // Owning - takes ownership of the stream
    any_read_stream stream(socket{ioc});

    // Reference - wraps without ownership
    socket sock(ioc);
    any_read_stream stream(&sock);

    mutable_buffer buf(data, size);
    auto [ec, n] = co_await stream.read_some(std::span(&buf, 1));
    @endcode

    @see any_write_stream, any_stream, ReadStream
*/
class any_read_stream
{
    struct vtable;

    template<ReadStream S>
    struct vtable_for_impl;

    struct read_op;

    void* stream_ = nullptr;
    vtable const* vt_ = nullptr;
    void* cached_frame_ = nullptr;
    std::size_t cached_size_ = 0;
    void* storage_ = nullptr;

    template<ReadStream S>
    static coro
    do_read_impl(
        void* stream,
        any_read_stream* wrapper,
        std::span<mutable_buffer const> buffers,
        coro h,
        executor_ref ex,
        std::stop_token token,
        std::error_code* ec,
        std::size_t* n);

    template<ReadStream S>
    static read_op
    read_coro(
        any_read_stream* wrapper,
        S& stream,
        std::span<mutable_buffer const> bufs,
        std::error_code* out_ec,
        std::size_t* out_n);

    void* alloc_frame(std::size_t size);
    void free_frame(void* p, std::size_t size);

public:
    /** Destructor.

        Destroys the owned stream (if any) and releases the cached
        coroutine frame.
    */
    ~any_read_stream();

    /** Default constructor.

        Constructs an empty wrapper. Operations on a default-constructed
        wrapper result in undefined behavior.
    */
    any_read_stream() = default;

    /** Non-copyable.

        The frame cache is per-instance and cannot be shared.
    */
    any_read_stream(any_read_stream const&) = delete;
    any_read_stream& operator=(any_read_stream const&) = delete;

    /** Move constructor.

        Transfers ownership of the wrapped stream (if owned) and
        cached frame from `other`. After the move, `other` is
        in a default-constructed state.

        @param other The wrapper to move from.
    */
    any_read_stream(any_read_stream&& other) noexcept
        : stream_(std::exchange(other.stream_, nullptr))
        , vt_(std::exchange(other.vt_, nullptr))
        , cached_frame_(std::exchange(other.cached_frame_, nullptr))
        , cached_size_(std::exchange(other.cached_size_, 0))
        , storage_(std::exchange(other.storage_, nullptr))
    {
    }

    /** Move assignment operator.

        Destroys any owned stream and releases existing resources,
        then transfers ownership from `other`.

        @param other The wrapper to move from.
        @return Reference to this wrapper.
    */
    any_read_stream&
    operator=(any_read_stream&& other) noexcept;

    /** Construct by taking ownership of a ReadStream.

        Allocates storage and moves the stream into this wrapper.
        The wrapper owns the stream and will destroy it.

        @param s The stream to take ownership of.
    */
    template<ReadStream S>
        requires (!std::same_as<std::decay_t<S>, any_read_stream>)
    any_read_stream(S s);

    /** Construct by wrapping a ReadStream without ownership.

        Wraps the given stream by pointer. The stream must remain
        valid for the lifetime of this wrapper.

        @param s Pointer to the stream to wrap.
    */
    template<ReadStream S>
    any_read_stream(S* s) noexcept
        : stream_(s)
        , vt_(&vtable_for_impl<S>::value)
    {
        // Preallocate the coroutine frame
        read_coro<S>(this, *s, {}, nullptr, nullptr);
    }
    /** Check if the wrapper contains a valid stream.

        @return `true` if wrapping a stream, `false` if default-constructed
            or moved-from.
    */
    bool
    has_value() const noexcept
    {
        return stream_ != nullptr;
    }

    /** Check if the wrapper contains a valid stream.

        @return `true` if wrapping a stream, `false` if default-constructed
            or moved-from.
    */
    explicit
    operator bool() const noexcept
    {
        return has_value();
    }

    /** Initiate an asynchronous read operation.

        Reads data into the provided buffer sequence. The operation
        completes when at least one byte has been read, or an error
        occurs.

        @param buffers The buffer sequence to read into. Passed by
            value to ensure the sequence lives in the coroutine frame
            across suspension points.

        @return An awaitable yielding `(error_code,std::size_t)`.

        @par Preconditions
        The wrapper must contain a valid stream (`has_value() == true`).
    */
    template<MutableBufferSequence MB>
    auto
    read_some(MB buffers);

protected:
    /** Rebind to a new stream after move.

        Updates the internal pointer to reference a new stream object.
        Used by owning wrappers after move assignment when the owned
        object has moved to a new location.

        @param new_stream The new stream to bind to. Must be the same
            type as the original stream.

        @note Terminates if called with a stream of different type
            than the original.
    */
    template<ReadStream S>
    void
    rebind(S& new_stream) noexcept
    {
        if(vt_ != &vtable_for_impl<S>::value)
            std::terminate();
        stream_ = &new_stream;
    }
};

//----------------------------------------------------------

struct any_read_stream::vtable
{
    void (*destroy)(void*) noexcept;

    coro (*do_read)(
        void* stream,
        any_read_stream* wrapper,
        std::span<mutable_buffer const> buffers,
        coro h,
        executor_ref ex,
        std::stop_token token,
        std::error_code* ec,
        std::size_t* n);
};

template<ReadStream S>
struct any_read_stream::vtable_for_impl
{
    static void
    do_destroy_impl(void* stream) noexcept
    {
        static_cast<S*>(stream)->~S();
    }

    static constexpr vtable value = {
        &do_destroy_impl,
        &any_read_stream::do_read_impl<S>
    };
};

//----------------------------------------------------------

inline
any_read_stream::~any_read_stream()
{
    if(storage_)
    {
        vt_->destroy(stream_);
        ::operator delete(storage_);
    }
    if(cached_frame_)
        ::operator delete(cached_frame_);
}

inline any_read_stream&
any_read_stream::operator=(any_read_stream&& other) noexcept
{
    if(this != &other)
    {
        if(storage_)
        {
            vt_->destroy(stream_);
            ::operator delete(storage_);
        }
        if(cached_frame_)
            ::operator delete(cached_frame_);
        stream_ = std::exchange(other.stream_, nullptr);
        vt_ = std::exchange(other.vt_, nullptr);
        cached_frame_ = std::exchange(other.cached_frame_, nullptr);
        cached_size_ = std::exchange(other.cached_size_, 0);
        storage_ = std::exchange(other.storage_, nullptr);
    }
    return *this;
}

template<ReadStream S>
    requires (!std::same_as<std::decay_t<S>, any_read_stream>)
any_read_stream::any_read_stream(S s)
    : vt_(&vtable_for_impl<S>::value)
{
    struct guard {
        any_read_stream* self;
        bool committed = false;
        ~guard() {
            if(!committed && self->storage_) {
                self->vt_->destroy(self->stream_);
                ::operator delete(self->storage_);
                self->storage_ = nullptr;
                self->stream_ = nullptr;
            }
        }
    } g{this};

    storage_ = ::operator new(sizeof(S));
    stream_ = ::new(storage_) S(std::move(s));

    // Preallocate the coroutine frame
    auto& ref = *static_cast<S*>(stream_);
    read_coro<S>(this, ref, {}, nullptr, nullptr);

    g.committed = true;
}

//----------------------------------------------------------

struct any_read_stream::read_op
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
            // Store exception for later propagation
            // For now, just rethrow to let outer handler catch it
            throw;
        }

        template<class... Args>
        static void*
        operator new(
            std::size_t size,
            any_read_stream* wrapper,
            Args&&...)
        {
            return wrapper->alloc_frame(size);
        }

        template<class... Args>
        static void
        operator delete(void*, any_read_stream*, Args&&...) noexcept
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

            decltype(auto) await_resume()
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
any_read_stream::alloc_frame(std::size_t size)
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
any_read_stream::free_frame(void*, std::size_t)
{
    // Keep the frame cached for reuse
}

template<ReadStream S>
any_read_stream::read_op
any_read_stream::read_coro(
    any_read_stream*,
    S& stream,
    std::span<mutable_buffer const> bufs,
    std::error_code* out_ec,
    std::size_t* out_n)
{
    auto [err, bytes] = co_await stream.read_some(bufs);

    *out_ec = err;
    *out_n = bytes;
}

template<ReadStream S>
coro
any_read_stream::do_read_impl(
    void* stream,
    any_read_stream* wrapper,
    std::span<mutable_buffer const> buffers,
    coro h,
    executor_ref ex,
    std::stop_token token,
    std::error_code* ec,
    std::size_t* n)
{
    auto& s = *static_cast<S*>(stream);

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
        // Return caller's handle via executor dispatch
        return ex.dispatch(h);
    }

    // Operation is pending - caller will be resumed via symmetric transfer
    op.h_.promise().set_caller(h);
    op.h_ = nullptr;
    return std::noop_coroutine();
}

//----------------------------------------------------------

template<MutableBufferSequence MB>
auto
any_read_stream::read_some(MB buffers)
{
    struct awaitable
    {
        any_read_stream* self_;
        buffer_param<MB> bp_;
        std::error_code ec_;
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
                self_->stream_,
                self_,
                bp_.data(),
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
    return awaitable{this, buffer_param<MB>(buffers), {}, 0};
}

} // namespace capy
} // namespace boost

#endif
