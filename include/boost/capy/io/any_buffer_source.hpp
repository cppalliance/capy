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
#include <boost/capy/detail/await_suspend_helper.hpp>
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

#include <concepts>
#include <coroutine>
#include <cstddef>
#include <exception>
#include <new>
#include <span>
#include <stop_token>
#include <system_error>
#include <utility>

namespace boost {
namespace capy {

/** Type-erased wrapper for any BufferSource.

    This class provides type erasure for any type satisfying the
    @ref BufferSource concept, enabling runtime polymorphism for
    buffer pull operations. The wrapper also satisfies @ref ReadSource,
    allowing it to be used with code expecting either interface.
    It uses cached awaitable storage to achieve zero steady-state
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

    @par Awaitable Preallocation
    The constructor preallocates storage for the type-erased awaitable.
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
    auto [ec, bufs] = co_await abs.pull(arr);
    @endcode

    @see any_buffer_sink, BufferSource, ReadSource
*/
class any_buffer_source
{
    struct vtable;
    struct awaitable_ops;

    template<BufferSource S>
    struct vtable_for_impl;

    void* source_ = nullptr;
    vtable const* vt_ = nullptr;
    void* cached_awaitable_ = nullptr;
    void* storage_ = nullptr;
    awaitable_ops const* active_ops_ = nullptr;

public:
    /** Destructor.

        Destroys the owned source (if any) and releases the cached
        awaitable storage.
    */
    ~any_buffer_source();

    /** Default constructor.

        Constructs an empty wrapper. Operations on a default-constructed
        wrapper result in undefined behavior.
    */
    any_buffer_source() = default;

    /** Non-copyable.

        The awaitable cache is per-instance and cannot be shared.
    */
    any_buffer_source(any_buffer_source const&) = delete;
    any_buffer_source& operator=(any_buffer_source const&) = delete;

    /** Move constructor.

        Transfers ownership of the wrapped source (if owned) and
        cached awaitable storage from `other`. After the move, `other` is
        in a default-constructed state.

        @param other The wrapper to move from.
    */
    any_buffer_source(any_buffer_source&& other) noexcept
        : source_(std::exchange(other.source_, nullptr))
        , vt_(std::exchange(other.vt_, nullptr))
        , cached_awaitable_(std::exchange(other.cached_awaitable_, nullptr))
        , storage_(std::exchange(other.storage_, nullptr))
        , active_ops_(std::exchange(other.active_ops_, nullptr))
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
    any_buffer_source(S* s);

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

        Fills the provided span with buffer descriptors from the
        underlying source. The operation completes when data is
        available, the source is exhausted, or an error occurs.

        @param dest Span of const_buffer to fill.

        @return An awaitable yielding `(error_code,std::span<const_buffer>)`.
            On success with data, a non-empty span of filled buffers.
            On success with empty span, source is exhausted.

        @par Preconditions
        The wrapper must contain a valid source (`has_value() == true`).
    */
    auto
    pull(std::span<const_buffer> dest);

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

struct any_buffer_source::awaitable_ops
{
    bool (*await_ready)(void*);
    coro (*await_suspend)(void*, coro, executor_ref, std::stop_token);
    io_result<std::span<const_buffer>> (*await_resume)(void*);
    void (*destroy)(void*) noexcept;
};

struct any_buffer_source::vtable
{
    void (*destroy)(void*) noexcept;
    void (*do_consume)(void* source, std::size_t n) noexcept;
    std::size_t awaitable_size;
    std::size_t awaitable_align;
    awaitable_ops const* (*construct_awaitable)(
        void* source,
        void* storage,
        std::span<const_buffer> dest);
};

template<BufferSource S>
struct any_buffer_source::vtable_for_impl
{
    using Awaitable = decltype(std::declval<S&>().pull(
        std::declval<std::span<const_buffer>>()));

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

    static awaitable_ops const*
    construct_awaitable_impl(
        void* source,
        void* storage,
        std::span<const_buffer> dest)
    {
        auto& s = *static_cast<S*>(source);
        ::new(storage) Awaitable(s.pull(dest));

        static constexpr awaitable_ops ops = {
            +[](void* p) {
                return static_cast<Awaitable*>(p)->await_ready();
            },
            +[](void* p, coro h, executor_ref ex, std::stop_token token) {
                return detail::call_await_suspend(
                    static_cast<Awaitable*>(p), h, ex, token);
            },
            +[](void* p) {
                return static_cast<Awaitable*>(p)->await_resume();
            },
            +[](void* p) noexcept {
                static_cast<Awaitable*>(p)->~Awaitable();
            }
        };
        return &ops;
    }

    static constexpr vtable value = {
        &do_destroy_impl,
        &do_consume_impl,
        sizeof(Awaitable),
        alignof(Awaitable),
        &construct_awaitable_impl
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
    if(cached_awaitable_)
        ::operator delete(cached_awaitable_);
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
        if(cached_awaitable_)
            ::operator delete(cached_awaitable_);
        source_ = std::exchange(other.source_, nullptr);
        vt_ = std::exchange(other.vt_, nullptr);
        cached_awaitable_ = std::exchange(other.cached_awaitable_, nullptr);
        storage_ = std::exchange(other.storage_, nullptr);
        active_ops_ = std::exchange(other.active_ops_, nullptr);
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

    // Preallocate the awaitable storage
    cached_awaitable_ = ::operator new(vt_->awaitable_size);

    g.committed = true;
}

template<BufferSource S>
any_buffer_source::any_buffer_source(S* s)
    : source_(s)
    , vt_(&vtable_for_impl<S>::value)
{
    // Preallocate the awaitable storage
    cached_awaitable_ = ::operator new(vt_->awaitable_size);
}

//----------------------------------------------------------

inline void
any_buffer_source::consume(std::size_t n) noexcept
{
    vt_->do_consume(source_, n);
}

inline auto
any_buffer_source::pull(std::span<const_buffer> dest)
{
    struct awaitable
    {
        any_buffer_source* self_;
        std::span<const_buffer> dest_;

        bool
        await_ready() const noexcept
        {
            return false;
        }

        coro
        await_suspend(coro h, executor_ref ex, std::stop_token token)
        {
            // Construct the underlying awaitable into cached storage
            self_->active_ops_ = self_->vt_->construct_awaitable(
                self_->source_,
                self_->cached_awaitable_,
                dest_);

            // Check if underlying is immediately ready
            if(self_->active_ops_->await_ready(self_->cached_awaitable_))
                return h;

            // Forward to underlying awaitable
            return self_->active_ops_->await_suspend(
                self_->cached_awaitable_, h, ex, token);
        }

        io_result<std::span<const_buffer>>
        await_resume()
        {
            struct guard {
                any_buffer_source* self;
                ~guard() {
                    self->active_ops_->destroy(self->cached_awaitable_);
                    self->active_ops_ = nullptr;
                }
            } g{self_};
            return self_->active_ops_->await_resume(
                self_->cached_awaitable_);
        }
    };
    return awaitable{this, dest};
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
        auto [ec, bufs] = co_await pull(arr);

        if(ec)
            co_return {ec, total};

        if(bufs.empty())
            co_return {error::eof, total};

        auto n = buffer_copy(dest, bufs);
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
