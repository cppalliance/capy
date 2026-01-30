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
#include <boost/capy/buffers/buffer_copy.hpp>
#include <boost/capy/buffers/slice.hpp>
#include <boost/capy/concept/buffer_source.hpp>
#include <boost/capy/concept/io_awaitable.hpp>
#include <boost/capy/concept/read_source.hpp>
#include <boost/capy/coro.hpp>
#include <boost/capy/error.hpp>
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

/** Type-erased wrapper for any BufferSource.

    This class provides type erasure for any type satisfying the
    @ref BufferSource concept, enabling runtime polymorphism for
    buffer pull operations. The wrapper also satisfies @ref ReadSource,
    allowing it to be used with code expecting either interface.
    It uses a cached coroutine frame to achieve zero steady-state
    allocation after construction.

    The wrapper also satisfies @ref ReadSource through the templated
    @ref read method. This method copies data from the source's
    internal buffers into the caller's buffers, incurring one extra
    buffer copy compared to using @ref pull and @ref consume directly.

    The wrapper supports two construction modes:
    - **Owning**: Pass by value to transfer ownership. The wrapper
      allocates storage and owns the source.
    - **Reference**: Pass a pointer to wrap without ownership. The
      pointed-to source must outlive this wrapper.

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
    // Owning - takes ownership of the source
    any_buffer_source abs(some_buffer_source{args...});

    // Reference - wraps without ownership
    some_buffer_source src;
    any_buffer_source abs(&src);

    const_buffer arr[16];
    auto [ec, count] = co_await abs.pull(arr, 16);
    @endcode

    @see any_write_sink, BufferSource, ReadSource
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
    void* storage_ = nullptr;

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
        std::error_code* ec,
        std::size_t* count);

    template<BufferSource S>
    static pull_op
    pull_coro(
        any_buffer_source* wrapper,
        S& source,
        const_buffer* arr,
        std::size_t max_count,
        std::error_code* out_ec,
        std::size_t* out_count);

    void* alloc_frame(std::size_t size);
    void free_frame(void* p, std::size_t size);

public:
    /** Destructor.

        Destroys the owned source (if any) and releases the cached
        coroutine frame.
    */
    ~any_buffer_source();

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

        Transfers ownership of the wrapped source (if owned) and
        cached frame from `other`. After the move, `other` is
        in a default-constructed state.

        @param other The wrapper to move from.
    */
    any_buffer_source(any_buffer_source&& other) noexcept
        : source_(std::exchange(other.source_, nullptr))
        , vt_(std::exchange(other.vt_, nullptr))
        , cached_frame_(std::exchange(other.cached_frame_, nullptr))
        , cached_size_(std::exchange(other.cached_size_, 0))
        , storage_(std::exchange(other.storage_, nullptr))
    {
    }

    /** Move assignment operator.

        Destroys any owned source and releases existing resources,
        then transfers ownership from `other`.

        @param other The wrapper to move from.
        @return Reference to this wrapper.
    */
    any_buffer_source&
    operator=(any_buffer_source&& other) noexcept;

    /** Construct by taking ownership of a BufferSource.

        Allocates storage and moves the source into this wrapper.
        The wrapper owns the source and will destroy it.

        @param s The source to take ownership of.
    */
    template<BufferSource S>
        requires (!std::same_as<std::decay_t<S>, any_buffer_source>)
    any_buffer_source(S s);

    /** Construct by wrapping a BufferSource without ownership.

        Wraps the given source by pointer. The source must remain
        valid for the lifetime of this wrapper.

        @param s Pointer to the source to wrap.
    */
    template<BufferSource S>
    any_buffer_source(S* s) noexcept
        : source_(s)
        , vt_(&vtable_for_impl<S>::value)
    {
        // Preallocate coroutine frame to find max size
        pull_coro<S>(this, *s, nullptr, 0, nullptr, nullptr);
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

    /** Consume bytes from the source.

        Advances the internal read position of the underlying source
        by the specified number of bytes. The next call to @ref pull
        returns data starting after the consumed bytes.

        @param n The number of bytes to consume. Must not exceed the
        total size of buffers returned by the previous @ref pull.

        @par Preconditions
        The wrapper must contain a valid source (`has_value() == true`).
    */
    void
    consume(std::size_t n) noexcept;

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

    /** Read data into a mutable buffer sequence.

        Fills the provided buffer sequence by pulling data from the
        underlying source and copying it into the caller's buffers.
        This satisfies @ref ReadSource but incurs a copy; for zero-copy
        access, use @ref pull and @ref consume instead.

        @note This operation copies data from the source's internal
        buffers into the caller's buffers. For zero-copy reads,
        use @ref pull and @ref consume directly.

        @param buffers The buffer sequence to fill.

        @return An awaitable yielding `(error_code,std::size_t)`.
            On success, `n == buffer_size(buffers)`.
            On EOF, `ec == error::eof` and `n` is bytes transferred.

        @par Preconditions
        The wrapper must contain a valid source (`has_value() == true`).

        @see pull, consume
    */
    template<MutableBufferSequence MB>
    task<io_result<std::size_t>>
    read(MB buffers);

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
    void (*destroy)(void*) noexcept;

    coro (*do_pull)(
        void* source,
        any_buffer_source* wrapper,
        const_buffer* arr,
        std::size_t max_count,
        coro h,
        executor_ref ex,
        std::stop_token token,
        std::error_code* ec,
        std::size_t* count);
    void (*do_consume)(void* source, std::size_t n) noexcept;
};

template<BufferSource S>
struct any_buffer_source::vtable_for_impl
{
    static void
    do_destroy_impl(void* source) noexcept
    {
        static_cast<S*>(source)->~S();
    }

    static void
    do_consume_impl(void* source, std::size_t n) noexcept
    {
        static_cast<S*>(source)->consume(n);
    }

    static constexpr vtable value = {
        &do_destroy_impl,
        &any_buffer_source::do_pull_impl<S>,
        &do_consume_impl
    };
};

//----------------------------------------------------------

inline
any_buffer_source::~any_buffer_source()
{
    if(storage_)
    {
        vt_->destroy(source_);
        ::operator delete(storage_);
    }
    if(cached_frame_)
        ::operator delete(cached_frame_);
}

inline any_buffer_source&
any_buffer_source::operator=(any_buffer_source&& other) noexcept
{
    if(this != &other)
    {
        if(storage_)
        {
            vt_->destroy(source_);
            ::operator delete(storage_);
        }
        if(cached_frame_)
            ::operator delete(cached_frame_);
        source_ = std::exchange(other.source_, nullptr);
        vt_ = std::exchange(other.vt_, nullptr);
        cached_frame_ = std::exchange(other.cached_frame_, nullptr);
        cached_size_ = std::exchange(other.cached_size_, 0);
        storage_ = std::exchange(other.storage_, nullptr);
    }
    return *this;
}

template<BufferSource S>
    requires (!std::same_as<std::decay_t<S>, any_buffer_source>)
any_buffer_source::any_buffer_source(S s)
    : vt_(&vtable_for_impl<S>::value)
{
    struct guard {
        any_buffer_source* self;
        bool committed = false;
        ~guard() {
            if(!committed && self->storage_) {
                self->vt_->destroy(self->source_);
                ::operator delete(self->storage_);
                self->storage_ = nullptr;
                self->source_ = nullptr;
            }
        }
    } g{this};

    storage_ = ::operator new(sizeof(S));
    source_ = ::new(storage_) S(std::move(s));

    // Preallocate coroutine frame to find max size
    auto& ref = *static_cast<S*>(source_);
    pull_coro<S>(this, ref, nullptr, 0, nullptr, nullptr);

    g.committed = true;
}

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

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmismatched-new-delete"
#endif

template<BufferSource S>
any_buffer_source::pull_op
any_buffer_source::pull_coro(
    any_buffer_source*,
    S& source,
    const_buffer* arr,
    std::size_t max_count,
    std::error_code* out_ec,
    std::size_t* out_count)
{
    auto [err, count] = co_await source.pull(arr, max_count);

    *out_ec = err;
    *out_count = count;
}

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

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
    std::error_code* ec,
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

inline void
any_buffer_source::consume(std::size_t n) noexcept
{
    vt_->do_consume(source_, n);
}

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
        std::error_code ec_;
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

template<MutableBufferSequence MB>
task<io_result<std::size_t>>
any_buffer_source::read(MB buffers)
{
    std::size_t total = 0;
    auto dest = sans_prefix(buffers, 0);

    while(!buffer_empty(dest))
    {
        const_buffer arr[detail::max_iovec_];
        auto [ec, count] = co_await pull(arr, detail::max_iovec_);

        if(ec)
            co_return {ec, total};

        if(count == 0)
            co_return {error::eof, total};

        auto n = buffer_copy(dest, std::span(arr, count));
        consume(n);
        total += n;
        dest = sans_prefix(dest, n);
    }

    co_return {{}, total};
}

//----------------------------------------------------------

static_assert(BufferSource<any_buffer_source>);
static_assert(ReadSource<any_buffer_source>);

} // namespace capy
} // namespace boost

#endif
