//
// Copyright (c) 2025 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_IO_ANY_BUFFER_SINK_HPP
#define BOOST_CAPY_IO_ANY_BUFFER_SINK_HPP

#include <boost/capy/detail/config.hpp>
#include <boost/capy/buffers.hpp>
#include <boost/capy/buffers/buffer_copy.hpp>
#include <boost/capy/buffers/buffer_param.hpp>
#include <boost/capy/concept/buffer_sink.hpp>
#include <boost/capy/concept/io_awaitable.hpp>
#include <boost/capy/concept/write_sink.hpp>
#include <boost/capy/coro.hpp>
#include <boost/capy/ex/executor_ref.hpp>
#include <boost/capy/io_result.hpp>
#include <boost/capy/task.hpp>

#include <boost/system/error_code.hpp>

#include <concepts>
#include <coroutine>
#include <cstddef>
#include <exception>
#include <stop_token>
#include <utility>

namespace boost {
namespace capy {

/** Type-erased wrapper for any BufferSink.

    This class provides type erasure for any type satisfying the
    @ref BufferSink concept, enabling runtime polymorphism for
    buffer sink operations. It uses a cached coroutine frame to achieve
    zero steady-state allocation after construction.

    The wrapper also satisfies @ref WriteSink through templated
    @ref write methods. These methods copy data from the caller's
    buffers into the sink's internal storage, incurring one extra
    buffer copy compared to using @ref prepare and @ref commit
    directly.

    The wrapper has reference semantics - it wraps an existing
    sink without taking ownership. The wrapped sink must
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
    some_buffer_sink sink;
    any_buffer_sink abs(sink);

    mutable_buffer arr[16];
    std::size_t count = abs.prepare(arr, 16);
    // Write data into arr[0..count)
    auto [ec] = co_await abs.commit(bytes_written);
    auto [ec2] = co_await abs.commit_eof();
    @endcode

    @see any_buffer_source, BufferSink, WriteSink
*/
class any_buffer_sink
{
    struct vtable;

    template<BufferSink S>
    struct vtable_for_impl;

    struct commit_op;

    void* sink_ = nullptr;
    vtable const* vt_ = nullptr;
    void* cached_frame_ = nullptr;
    std::size_t cached_size_ = 0;

    template<BufferSink S>
    static std::size_t
    do_prepare_impl(
        void* sink,
        mutable_buffer* arr,
        std::size_t max_count);

    template<BufferSink S>
    static coro
    do_commit_impl(
        void* sink,
        any_buffer_sink* wrapper,
        std::size_t n,
        bool eof,
        coro h,
        executor_ref ex,
        std::stop_token token,
        system::error_code* ec);

    template<BufferSink S>
    static coro
    do_commit_eof_impl(
        void* sink,
        any_buffer_sink* wrapper,
        coro h,
        executor_ref ex,
        std::stop_token token,
        system::error_code* ec);

    template<BufferSink S>
    static commit_op
    commit_coro(
        any_buffer_sink* wrapper,
        S& sink,
        std::size_t n,
        system::error_code* out_ec);

    template<BufferSink S>
    static commit_op
    commit_with_eof_coro(
        any_buffer_sink* wrapper,
        S& sink,
        std::size_t n,
        bool eof,
        system::error_code* out_ec);

    template<BufferSink S>
    static commit_op
    commit_eof_coro(
        any_buffer_sink* wrapper,
        S& sink,
        system::error_code* out_ec);

    void* alloc_frame(std::size_t size);
    void free_frame(void* p, std::size_t size);

public:
    /** Destructor.

        Releases the cached coroutine frame if any.
    */
    ~any_buffer_sink()
    {
        if(cached_frame_)
            ::operator delete(cached_frame_);
    }

    /** Default constructor.

        Constructs an empty wrapper. Operations on a default-constructed
        wrapper result in undefined behavior.
    */
    any_buffer_sink() = default;

    /** Non-copyable.

        The frame cache is per-instance and cannot be shared.
    */
    any_buffer_sink(any_buffer_sink const&) = delete;
    any_buffer_sink& operator=(any_buffer_sink const&) = delete;

    /** Move constructor.

        Transfers ownership of the wrapped sink reference and
        cached frame from `other`. After the move, `other` is
        in a default-constructed state.

        @param other The wrapper to move from.
    */
    any_buffer_sink(any_buffer_sink&& other) noexcept
        : sink_(std::exchange(other.sink_, nullptr))
        , vt_(std::exchange(other.vt_, nullptr))
        , cached_frame_(std::exchange(other.cached_frame_, nullptr))
        , cached_size_(std::exchange(other.cached_size_, 0))
    {
    }

    /** Rebinding move constructor.

        Transfers the cached frame and vtable from `other`, but binds
        to a new sink object. Used by owning wrappers when the owned
        object moves to a new location.

        @param other The wrapper to move state from.
        @param new_sink The new sink to bind to. Must be the same
            type as the original sink.
    */
    template<BufferSink S>
    any_buffer_sink(any_buffer_sink&& other, S& new_sink) noexcept
        : sink_(&new_sink)
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
    any_buffer_sink&
    operator=(any_buffer_sink&& other) noexcept
    {
        if(this != &other)
        {
            if(cached_frame_)
                ::operator delete(cached_frame_);
            sink_ = std::exchange(other.sink_, nullptr);
            vt_ = std::exchange(other.vt_, nullptr);
            cached_frame_ = std::exchange(other.cached_frame_, nullptr);
            cached_size_ = std::exchange(other.cached_size_, 0);
        }
        return *this;
    }

    /** Construct from a BufferSink.

        Wraps the given sink and preallocates the internal
        coroutine frame. The sink must remain valid for the
        lifetime of this wrapper.

        @param s The sink to wrap.
    */
    template<BufferSink S>
        requires (!std::same_as<std::decay_t<S>, any_buffer_sink>)
    any_buffer_sink(S& s) noexcept
        : sink_(&s)
        , vt_(&vtable_for_impl<S>::value)
    {
        // Preallocate coroutine frames to find max size
        commit_coro<S>(this, s, 0, nullptr);
        commit_with_eof_coro<S>(this, s, 0, false, nullptr);
        commit_eof_coro<S>(this, s, nullptr);
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

    /** Prepare writable buffers.

        Fills the provided array with mutable buffer descriptors
        pointing to the underlying sink's internal storage. This
        operation is synchronous.

        @param arr Pointer to array of mutable_buffer to fill.
        @param max_count Maximum number of buffers to fill.

        @return The number of buffers filled.

        @par Preconditions
        The wrapper must contain a valid sink (`has_value() == true`).
    */
    std::size_t
    prepare(mutable_buffer* arr, std::size_t max_count);

    /** Commit bytes written to the prepared buffers.

        Commits `n` bytes written to the buffers returned by the
        most recent call to @ref prepare. The operation may trigger
        underlying I/O.

        @param n The number of bytes to commit.

        @return An awaitable yielding `(error_code)`.

        @par Preconditions
        The wrapper must contain a valid sink (`has_value() == true`).
    */
    auto
    commit(std::size_t n);

    /** Commit bytes written with optional end-of-stream.

        Commits `n` bytes written to the buffers returned by the
        most recent call to @ref prepare. If `eof` is true, also
        signals end-of-stream.

        @param n The number of bytes to commit.
        @param eof If true, signals end-of-stream after committing.

        @return An awaitable yielding `(error_code)`.

        @par Preconditions
        The wrapper must contain a valid sink (`has_value() == true`).
    */
    auto
    commit(std::size_t n, bool eof);

    /** Signal end-of-stream.

        Indicates that no more data will be written to the sink.
        The operation completes when the sink is finalized, or
        an error occurs.

        @return An awaitable yielding `(error_code)`.

        @par Preconditions
        The wrapper must contain a valid sink (`has_value() == true`).
    */
    auto
    commit_eof();

    /** Write data from a buffer sequence.

        Writes all data from the buffer sequence to the underlying
        sink. This method satisfies the @ref WriteSink concept.

        @note This operation copies data from the caller's buffers
        into the sink's internal buffers. For zero-copy writes,
        use @ref prepare and @ref commit directly.

        @param buffers The buffer sequence to write.

        @return An awaitable yielding `(error_code,std::size_t)`.

        @par Preconditions
        The wrapper must contain a valid sink (`has_value() == true`).
    */
    template<ConstBufferSequence CB>
    task<io_result<std::size_t>>
    write(CB buffers);

    /** Write data with optional end-of-stream.

        Writes all data from the buffer sequence to the underlying
        sink, optionally finalizing it afterwards. This method
        satisfies the @ref WriteSink concept.

        @note This operation copies data from the caller's buffers
        into the sink's internal buffers. For zero-copy writes,
        use @ref prepare and @ref commit directly.

        @param buffers The buffer sequence to write.
        @param eof If true, finalize the sink after writing.

        @return An awaitable yielding `(error_code,std::size_t)`.

        @par Preconditions
        The wrapper must contain a valid sink (`has_value() == true`).
    */
    template<ConstBufferSequence CB>
    task<io_result<std::size_t>>
    write(CB buffers, bool eof);

    /** Signal end-of-stream.

        Indicates that no more data will be written to the sink.
        This method satisfies the @ref WriteSink concept.

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
    template<BufferSink S>
    void
    rebind(S& new_sink) noexcept
    {
        if(vt_ != &vtable_for_impl<S>::value)
            std::terminate();
        sink_ = &new_sink;
    }
};

//----------------------------------------------------------

struct any_buffer_sink::vtable
{
    std::size_t (*do_prepare)(
        void* sink,
        mutable_buffer* arr,
        std::size_t max_count);

    coro (*do_commit)(
        void* sink,
        any_buffer_sink* wrapper,
        std::size_t n,
        bool eof,
        coro h,
        executor_ref ex,
        std::stop_token token,
        system::error_code* ec);

    coro (*do_commit_eof)(
        void* sink,
        any_buffer_sink* wrapper,
        coro h,
        executor_ref ex,
        std::stop_token token,
        system::error_code* ec);
};

template<BufferSink S>
struct any_buffer_sink::vtable_for_impl
{
    static constexpr vtable value = {
        &any_buffer_sink::do_prepare_impl<S>,
        &any_buffer_sink::do_commit_impl<S>,
        &any_buffer_sink::do_commit_eof_impl<S>
    };
};

//----------------------------------------------------------

struct any_buffer_sink::commit_op
{
    struct promise_type
    {
        executor_ref executor_;
        std::stop_token stop_token_;
        coro caller_h_{};

        promise_type() = default;

        commit_op
        get_return_object() noexcept
        {
            return commit_op{
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
            any_buffer_sink* wrapper,
            Args&&...)
        {
            return wrapper->alloc_frame(size);
        }

        template<class... Args>
        static void
        operator delete(void*, any_buffer_sink*, Args&&...) noexcept
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

    ~commit_op()
    {
        if(h_)
            h_.destroy();
    }

    commit_op(commit_op const&) = delete;
    commit_op& operator=(commit_op const&) = delete;

    commit_op(commit_op&& other) noexcept
        : h_(std::exchange(other.h_, nullptr))
    {
    }

    commit_op& operator=(commit_op&& other) noexcept
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
    commit_op(std::coroutine_handle<promise_type> h) noexcept
        : h_(h)
    {
    }
};

//----------------------------------------------------------

inline void*
any_buffer_sink::alloc_frame(std::size_t size)
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
any_buffer_sink::free_frame(void*, std::size_t)
{
    // Keep the frame cached for reuse
}

template<BufferSink S>
std::size_t
any_buffer_sink::do_prepare_impl(
    void* sink,
    mutable_buffer* arr,
    std::size_t max_count)
{
    auto& s = *static_cast<S*>(sink);
    return s.prepare(arr, max_count);
}

template<BufferSink S>
any_buffer_sink::commit_op
any_buffer_sink::commit_coro(
    any_buffer_sink*,
    S& sink,
    std::size_t n,
    system::error_code* out_ec)
{
    auto [err] = co_await sink.commit(n);
    *out_ec = err;
}

template<BufferSink S>
any_buffer_sink::commit_op
any_buffer_sink::commit_with_eof_coro(
    any_buffer_sink*,
    S& sink,
    std::size_t n,
    bool eof,
    system::error_code* out_ec)
{
    auto [err] = co_await sink.commit(n, eof);
    *out_ec = err;
}

template<BufferSink S>
any_buffer_sink::commit_op
any_buffer_sink::commit_eof_coro(
    any_buffer_sink*,
    S& sink,
    system::error_code* out_ec)
{
    auto [err] = co_await sink.commit_eof();
    *out_ec = err;
}

template<BufferSink S>
coro
any_buffer_sink::do_commit_impl(
    void* sink,
    any_buffer_sink* wrapper,
    std::size_t n,
    bool eof,
    coro h,
    executor_ref ex,
    std::stop_token token,
    system::error_code* ec)
{
    auto& s = *static_cast<S*>(sink);

    // Create coroutine - frame is cached in wrapper
    auto op = eof
        ? commit_with_eof_coro<S>(wrapper, s, n, eof, ec)
        : commit_coro<S>(wrapper, s, n, ec);

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

template<BufferSink S>
coro
any_buffer_sink::do_commit_eof_impl(
    void* sink,
    any_buffer_sink* wrapper,
    coro h,
    executor_ref ex,
    std::stop_token token,
    system::error_code* ec)
{
    auto& s = *static_cast<S*>(sink);

    // Create coroutine - frame is cached in wrapper
    auto op = commit_eof_coro<S>(wrapper, s, ec);

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

inline std::size_t
any_buffer_sink::prepare(
    mutable_buffer* arr,
    std::size_t max_count)
{
    return vt_->do_prepare(sink_, arr, max_count);
}

inline auto
any_buffer_sink::commit(std::size_t n)
{
    struct awaitable
    {
        any_buffer_sink* self_;
        std::size_t n_;
        system::error_code ec_;

        bool
        await_ready() const noexcept
        {
            return false;
        }

        coro
        await_suspend(coro h, executor_ref ex, std::stop_token token)
        {
            return self_->vt_->do_commit(
                self_->sink_,
                self_,
                n_,
                false,
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
    return awaitable{this, n, {}};
}

inline auto
any_buffer_sink::commit(std::size_t n, bool eof)
{
    struct awaitable
    {
        any_buffer_sink* self_;
        std::size_t n_;
        bool eof_;
        system::error_code ec_;

        bool
        await_ready() const noexcept
        {
            return false;
        }

        coro
        await_suspend(coro h, executor_ref ex, std::stop_token token)
        {
            return self_->vt_->do_commit(
                self_->sink_,
                self_,
                n_,
                eof_,
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
    return awaitable{this, n, eof, {}};
}

inline auto
any_buffer_sink::commit_eof()
{
    struct awaitable
    {
        any_buffer_sink* self_;
        system::error_code ec_;

        bool
        await_ready() const noexcept
        {
            return false;
        }

        coro
        await_suspend(coro h, executor_ref ex, std::stop_token token)
        {
            return self_->vt_->do_commit_eof(
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

//----------------------------------------------------------

template<ConstBufferSequence CB>
task<io_result<std::size_t>>
any_buffer_sink::write(CB buffers)
{
    return write(buffers, false);
}

template<ConstBufferSequence CB>
task<io_result<std::size_t>>
any_buffer_sink::write(CB buffers, bool eof)
{
    buffer_param<CB> bp(buffers);
    std::size_t total = 0;

    for(;;)
    {
        auto src = bp.data();
        if(src.empty())
            break;

        mutable_buffer arr[detail::max_iovec_];
        std::size_t count = prepare(arr, detail::max_iovec_);
        if(count == 0)
        {
            auto [ec] = co_await commit(0);
            if(ec.failed())
                co_return {ec, total};
            continue;
        }

        auto n = buffer_copy(std::span(arr, count), src);
        auto [ec] = co_await commit(n);
        if(ec.failed())
            co_return {ec, total};
        bp.consume(n);
        total += n;
    }

    if(eof)
    {
        auto [ec] = co_await commit_eof();
        if(ec.failed())
            co_return {ec, total};
    }

    co_return {{}, total};
}

inline auto
any_buffer_sink::write_eof()
{
    return commit_eof();
}

//----------------------------------------------------------

static_assert(WriteSink<any_buffer_sink>);

} // namespace capy
} // namespace boost

#endif
