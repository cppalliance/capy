//
// Copyright (c) 2025 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_IO_ANY_WRITE_SINK_HPP
#define BOOST_CAPY_IO_ANY_WRITE_SINK_HPP

#include <boost/capy/detail/config.hpp>
#include <boost/capy/buffers.hpp>
#include <boost/capy/buffers/buffer_param.hpp>
#include <boost/capy/concept/io_awaitable.hpp>
#include <boost/capy/concept/write_sink.hpp>
#include <boost/capy/coro.hpp>
#include <boost/capy/ex/executor_ref.hpp>
#include <boost/capy/io_result.hpp>
#include <boost/capy/task.hpp>

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

/** Type-erased wrapper for any WriteSink.

    This class provides type erasure for any type satisfying the
    @ref WriteSink concept, enabling runtime polymorphism for
    sink write operations. It uses a cached coroutine frame to achieve
    zero steady-state allocation after construction.

    The wrapper supports two construction modes:
    - **Owning**: Pass by value to transfer ownership. The wrapper
      allocates storage and owns the sink.
    - **Reference**: Pass a pointer to wrap without ownership. The
      pointed-to sink must outlive this wrapper.

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
    // Owning - takes ownership of the sink
    any_write_sink ws(some_sink{args...});

    // Reference - wraps without ownership
    some_sink sink;
    any_write_sink ws(&sink);

    const_buffer buf(data, size);
    auto [ec, n] = co_await ws.write(std::span(&buf, 1));
    auto [ec2] = co_await ws.write_eof();
    @endcode

    @see any_write_stream, WriteSink
*/
class any_write_sink
{
    struct vtable;

    template<WriteSink S>
    struct vtable_for_impl;

    struct write_op;
    struct write_eof_op;

    void* sink_ = nullptr;
    vtable const* vt_ = nullptr;
    void* cached_frame_ = nullptr;
    std::size_t cached_size_ = 0;
    void* storage_ = nullptr;

    template<WriteSink S>
    static coro
    do_write_impl(
        void* sink,
        any_write_sink* wrapper,
        std::span<const_buffer const> buffers,
        bool eof,
        coro h,
        executor_ref ex,
        std::stop_token token,
        std::error_code* ec,
        std::size_t* n);

    template<WriteSink S>
    static coro
    do_write_eof_impl(
        void* sink,
        any_write_sink* wrapper,
        coro h,
        executor_ref ex,
        std::stop_token token,
        std::error_code* ec);

    template<WriteSink S>
    static write_op
    write_coro(
        any_write_sink* wrapper,
        S& sink,
        std::span<const_buffer const> bufs,
        std::error_code* out_ec,
        std::size_t* out_n);

    template<WriteSink S>
    static write_op
    write_with_eof_coro(
        any_write_sink* wrapper,
        S& sink,
        std::span<const_buffer const> bufs,
        bool eof,
        std::error_code* out_ec,
        std::size_t* out_n);

    template<WriteSink S>
    static write_eof_op
    write_eof_coro(
        any_write_sink* wrapper,
        S& sink,
        std::error_code* out_ec);

    void* alloc_frame(std::size_t size);
    void free_frame(void* p, std::size_t size);

public:
    /** Destructor.

        Destroys the owned sink (if any) and releases the cached
        coroutine frame.
    */
    ~any_write_sink();

    /** Default constructor.

        Constructs an empty wrapper. Operations on a default-constructed
        wrapper result in undefined behavior.
    */
    any_write_sink() = default;

    /** Non-copyable.

        The frame cache is per-instance and cannot be shared.
    */
    any_write_sink(any_write_sink const&) = delete;
    any_write_sink& operator=(any_write_sink const&) = delete;

    /** Move constructor.

        Transfers ownership of the wrapped sink (if owned) and
        cached frame from `other`. After the move, `other` is
        in a default-constructed state.

        @param other The wrapper to move from.
    */
    any_write_sink(any_write_sink&& other) noexcept
        : sink_(std::exchange(other.sink_, nullptr))
        , vt_(std::exchange(other.vt_, nullptr))
        , cached_frame_(std::exchange(other.cached_frame_, nullptr))
        , cached_size_(std::exchange(other.cached_size_, 0))
        , storage_(std::exchange(other.storage_, nullptr))
    {
    }

    /** Move assignment operator.

        Destroys any owned sink and releases existing resources,
        then transfers ownership from `other`.

        @param other The wrapper to move from.
        @return Reference to this wrapper.
    */
    any_write_sink&
    operator=(any_write_sink&& other) noexcept;

    /** Construct by taking ownership of a WriteSink.

        Allocates storage and moves the sink into this wrapper.
        The wrapper owns the sink and will destroy it.

        @param s The sink to take ownership of.
    */
    template<WriteSink S>
        requires (!std::same_as<std::decay_t<S>, any_write_sink>)
    any_write_sink(S s);

    /** Construct by wrapping a WriteSink without ownership.

        Wraps the given sink by pointer. The sink must remain
        valid for the lifetime of this wrapper.

        @param s Pointer to the sink to wrap.
    */
    template<WriteSink S>
    any_write_sink(S* s) noexcept
        : sink_(s)
        , vt_(&vtable_for_impl<S>::value)
    {
        // Preallocate coroutine frames to find max size
        write_coro<S>(this, *s, {}, nullptr, nullptr);
        write_with_eof_coro<S>(this, *s, {}, false, nullptr, nullptr);
        write_eof_coro<S>(this, *s, nullptr);
    }

    /** Check if the wrapper contains a valid sink.

        @return `true` if wrapping a sink, `false` if default-constructed
            or moved-from.
    */
    bool
    has_value() const noexcept
    {
        return sink_ != nullptr;
    }

    /** Check if the wrapper contains a valid sink.

        @return `true` if wrapping a sink, `false` if default-constructed
            or moved-from.
    */
    explicit
    operator bool() const noexcept
    {
        return has_value();
    }

    /** Initiate an asynchronous write operation.

        Writes data from the provided buffer sequence. The operation
        completes when all bytes have been consumed, or an error
        occurs.

        @param buffers The buffer sequence containing data to write.
            Passed by value to ensure the sequence lives in the
            coroutine frame across suspension points.

        @return An awaitable yielding `(error_code,std::size_t)`.

        @par Preconditions
        The wrapper must contain a valid sink (`has_value() == true`).
    */
    template<ConstBufferSequence CB>
    task<io_result<std::size_t>>
    write(CB buffers);

    /** Initiate an asynchronous write operation with optional EOF.

        Writes data from the provided buffer sequence, optionally
        finalizing the sink afterwards. The operation completes when
        all bytes have been consumed and (if eof is true) the sink
        is finalized, or an error occurs.

        @param buffers The buffer sequence containing data to write.
            Passed by value to ensure the sequence lives in the
            coroutine frame across suspension points.

        @param eof If `true`, the sink is finalized after writing
            the data.

        @return An awaitable yielding `(error_code,std::size_t)`.

        @par Preconditions
        The wrapper must contain a valid sink (`has_value() == true`).
    */
    template<ConstBufferSequence CB>
    task<io_result<std::size_t>>
    write(CB buffers, bool eof);

    /** Signal end of data.

        Indicates that no more data will be written to the sink.
        The operation completes when the sink is finalized, or
        an error occurs.

        @return An awaitable yielding `(error_code)`.

        @par Preconditions
        The wrapper must contain a valid sink (`has_value() == true`).
    */
    auto
    write_eof();

protected:
    /** Rebind to a new sink after move.

        Updates the internal pointer to reference a new sink object.
        Used by owning wrappers after move assignment when the owned
        object has moved to a new location.

        @param new_sink The new sink to bind to. Must be the same
            type as the original sink.

        @note Terminates if called with a sink of different type
            than the original.
    */
    template<WriteSink S>
    void
    rebind(S& new_sink) noexcept
    {
        if(vt_ != &vtable_for_impl<S>::value)
            std::terminate();
        sink_ = &new_sink;
    }

private:
    auto
    write_some_(std::span<const_buffer const> buffers, bool eof);
};

//----------------------------------------------------------

struct any_write_sink::vtable
{
    void (*destroy)(void*) noexcept;

    coro (*do_write)(
        void* sink,
        any_write_sink* wrapper,
        std::span<const_buffer const> buffers,
        bool eof,
        coro h,
        executor_ref ex,
        std::stop_token token,
        std::error_code* ec,
        std::size_t* n);

    coro (*do_write_eof)(
        void* sink,
        any_write_sink* wrapper,
        coro h,
        executor_ref ex,
        std::stop_token token,
        std::error_code* ec);
};

template<WriteSink S>
struct any_write_sink::vtable_for_impl
{
    static void
    do_destroy_impl(void* sink) noexcept
    {
        static_cast<S*>(sink)->~S();
    }

    static constexpr vtable value = {
        &do_destroy_impl,
        &any_write_sink::do_write_impl<S>,
        &any_write_sink::do_write_eof_impl<S>
    };
};

//----------------------------------------------------------

inline
any_write_sink::~any_write_sink()
{
    if(storage_)
    {
        vt_->destroy(sink_);
        ::operator delete(storage_);
    }
    if(cached_frame_)
        ::operator delete(cached_frame_);
}

inline any_write_sink&
any_write_sink::operator=(any_write_sink&& other) noexcept
{
    if(this != &other)
    {
        if(storage_)
        {
            vt_->destroy(sink_);
            ::operator delete(storage_);
        }
        if(cached_frame_)
            ::operator delete(cached_frame_);
        sink_ = std::exchange(other.sink_, nullptr);
        vt_ = std::exchange(other.vt_, nullptr);
        cached_frame_ = std::exchange(other.cached_frame_, nullptr);
        cached_size_ = std::exchange(other.cached_size_, 0);
        storage_ = std::exchange(other.storage_, nullptr);
    }
    return *this;
}

template<WriteSink S>
    requires (!std::same_as<std::decay_t<S>, any_write_sink>)
any_write_sink::any_write_sink(S s)
    : vt_(&vtable_for_impl<S>::value)
{
    struct guard {
        any_write_sink* self;
        bool committed = false;
        ~guard() {
            if(!committed && self->storage_) {
                self->vt_->destroy(self->sink_);
                ::operator delete(self->storage_);
                self->storage_ = nullptr;
                self->sink_ = nullptr;
            }
        }
    } g{this};

    storage_ = ::operator new(sizeof(S));
    sink_ = ::new(storage_) S(std::move(s));

    // Preallocate coroutine frames to find max size
    auto& ref = *static_cast<S*>(sink_);
    write_coro<S>(this, ref, {}, nullptr, nullptr);
    write_with_eof_coro<S>(this, ref, {}, false, nullptr, nullptr);
    write_eof_coro<S>(this, ref, nullptr);

    g.committed = true;
}

//----------------------------------------------------------

struct any_write_sink::write_op
{
    struct promise_type
    {
        executor_ref executor_;
        std::stop_token stop_token_;
        coro caller_h_{};

        promise_type() = default;

        write_op
        get_return_object() noexcept
        {
            return write_op{
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
            any_write_sink* wrapper,
            Args&&...)
        {
            return wrapper->alloc_frame(size);
        }

        template<class... Args>
        static void
        operator delete(void*, any_write_sink*, Args&&...) noexcept
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

    ~write_op()
    {
        if(h_)
            h_.destroy();
    }

    write_op(write_op const&) = delete;
    write_op& operator=(write_op const&) = delete;

    write_op(write_op&& other) noexcept
        : h_(std::exchange(other.h_, nullptr))
    {
    }

    write_op& operator=(write_op&& other) noexcept
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
    write_op(std::coroutine_handle<promise_type> h) noexcept
        : h_(h)
    {
    }
};

//----------------------------------------------------------

struct any_write_sink::write_eof_op
{
    struct promise_type
    {
        executor_ref executor_;
        std::stop_token stop_token_;
        coro caller_h_{};

        promise_type() = default;

        write_eof_op
        get_return_object() noexcept
        {
            return write_eof_op{
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
            any_write_sink* wrapper,
            Args&&...)
        {
            return wrapper->alloc_frame(size);
        }

        template<class... Args>
        static void
        operator delete(void*, any_write_sink*, Args&&...) noexcept
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

    ~write_eof_op()
    {
        if(h_)
            h_.destroy();
    }

    write_eof_op(write_eof_op const&) = delete;
    write_eof_op& operator=(write_eof_op const&) = delete;

    write_eof_op(write_eof_op&& other) noexcept
        : h_(std::exchange(other.h_, nullptr))
    {
    }

    write_eof_op& operator=(write_eof_op&& other) noexcept
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
    write_eof_op(std::coroutine_handle<promise_type> h) noexcept
        : h_(h)
    {
    }
};

//----------------------------------------------------------

inline void*
any_write_sink::alloc_frame(std::size_t size)
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
any_write_sink::free_frame(void*, std::size_t)
{
    // Keep the frame cached for reuse
}

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmismatched-new-delete"
#endif

template<WriteSink S>
any_write_sink::write_op
any_write_sink::write_coro(
    any_write_sink*,
    S& sink,
    std::span<const_buffer const> bufs,
    std::error_code* out_ec,
    std::size_t* out_n)
{
    auto [err, bytes] = co_await sink.write(bufs);

    *out_ec = err;
    *out_n = bytes;
}

template<WriteSink S>
any_write_sink::write_op
any_write_sink::write_with_eof_coro(
    any_write_sink*,
    S& sink,
    std::span<const_buffer const> bufs,
    bool eof,
    std::error_code* out_ec,
    std::size_t* out_n)
{
    auto [err, bytes] = co_await sink.write(bufs, eof);

    *out_ec = err;
    *out_n = bytes;
}

template<WriteSink S>
any_write_sink::write_eof_op
any_write_sink::write_eof_coro(
    any_write_sink*,
    S& sink,
    std::error_code* out_ec)
{
    auto [err] = co_await sink.write_eof();

    *out_ec = err;
}

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

template<WriteSink S>
coro
any_write_sink::do_write_impl(
    void* sink,
    any_write_sink* wrapper,
    std::span<const_buffer const> buffers,
    bool eof,
    coro h,
    executor_ref ex,
    std::stop_token token,
    std::error_code* ec,
    std::size_t* n)
{
    auto& s = *static_cast<S*>(sink);

    // Create coroutine - frame is cached in wrapper
    auto op = eof
        ? write_with_eof_coro<S>(wrapper, s, buffers, eof, ec, n)
        : write_coro<S>(wrapper, s, buffers, ec, n);

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

template<WriteSink S>
coro
any_write_sink::do_write_eof_impl(
    void* sink,
    any_write_sink* wrapper,
    coro h,
    executor_ref ex,
    std::stop_token token,
    std::error_code* ec)
{
    auto& s = *static_cast<S*>(sink);

    // Create coroutine - frame is cached in wrapper
    auto op = write_eof_coro<S>(wrapper, s, ec);

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
any_write_sink::write_some_(
    std::span<const_buffer const> buffers,
    bool eof)
{
    struct awaitable
    {
        any_write_sink* self_;
        std::span<const_buffer const> buffers_;
        bool eof_;
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
            return self_->vt_->do_write(
                self_->sink_,
                self_,
                buffers_,
                eof_,
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
    return awaitable{this, buffers, eof, {}, 0};
}

inline auto
any_write_sink::write_eof()
{
    struct awaitable
    {
        any_write_sink* self_;
        std::error_code ec_;

        bool
        await_ready() const noexcept
        {
            return false;
        }

        coro
        await_suspend(coro h, executor_ref ex, std::stop_token token)
        {
            return self_->vt_->do_write_eof(
                self_->sink_,
                self_,
                h,
                ex,
                token,
                &ec_);
        }

        io_result<>
        await_resume() const noexcept
        {
            return {ec_};
        }
    };
    return awaitable{this, {}};
}

template<ConstBufferSequence CB>
task<io_result<std::size_t>>
any_write_sink::write(CB buffers)
{
    return write(buffers, false);
}

template<ConstBufferSequence CB>
task<io_result<std::size_t>>
any_write_sink::write(CB buffers, bool eof)
{
    buffer_param<CB> bp(buffers);
    std::size_t total = 0;

    for(;;)
    {
        auto bufs = bp.data();
        if(bufs.empty())
            break;

        auto [ec, n] = co_await write_some_(bufs, false);
        if(ec)
            co_return {ec, total + n};
        bp.consume(n);
        total += n;
    }

    if(eof)
    {
        auto [ec] = co_await write_eof();
        if(ec)
            co_return {ec, total};
    }

    co_return {{}, total};
}

} // namespace capy
} // namespace boost

#endif
