//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
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
#include <boost/capy/buffers/buffer_param.hpp>
#include <boost/capy/buffers/slice.hpp>
#include <boost/capy/concept/buffer_source.hpp>
#include <boost/capy/concept/io_awaitable.hpp>
#include <boost/capy/concept/read_source.hpp>
#include <boost/capy/error.hpp>
#include <boost/capy/ex/io_env.hpp>
#include <boost/capy/io_result.hpp>
#include <boost/capy/io_task.hpp>

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
    buffer pull operations. It uses cached awaitable storage to achieve
    zero steady-state allocation after construction.

    The wrapper also satisfies @ref ReadSource. When the wrapped type
    satisfies only @ref BufferSource, the read operations are
    synthesized using @ref pull and @ref consume with an extra
    buffer copy. When the wrapped type satisfies both @ref BufferSource
    and @ref ReadSource, the native read operations are forwarded
    directly across the virtual boundary, avoiding the copy.

    The wrapper supports two construction modes:
    - **Owning**: Pass by value to transfer ownership. The wrapper
      allocates storage and owns the source.
    - **Reference**: Pass a pointer to wrap without ownership. The
      pointed-to source must outlive this wrapper.

    Within each mode, the vtable is populated at compile time based
    on whether the wrapped type also satisfies @ref ReadSource:
    - **BufferSource only**: @ref read_some and @ref read are
      synthesized from @ref pull and @ref consume, incurring one
      buffer copy per operation.
    - **BufferSource + ReadSource**: All read operations are
      forwarded natively through the type-erased boundary with
      no extra copy.

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

    // ReadSource interface also available
    char buf[64];
    auto [ec2, n] = co_await abs.read_some(mutable_buffer(buf, 64));
    @endcode

    @see any_buffer_sink, BufferSource, ReadSource
*/
class any_buffer_source
{
    struct vtable;
    struct awaitable_ops;
    struct read_awaitable_ops;

    template<BufferSource S>
    struct vtable_for_impl;

    // hot-path members first for cache locality
    void* source_ = nullptr;
    vtable const* vt_ = nullptr;
    void* cached_awaitable_ = nullptr;
    awaitable_ops const* active_ops_ = nullptr;
    read_awaitable_ops const* active_read_ops_ = nullptr;
    void* storage_ = nullptr;

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
        , active_ops_(std::exchange(other.active_ops_, nullptr))
        , active_read_ops_(std::exchange(other.active_read_ops_, nullptr))
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
        The wrapper owns the source and will destroy it. If `S` also
        satisfies @ref ReadSource, native read operations are
        forwarded through the virtual boundary.

        @param s The source to take ownership of.
    */
    template<BufferSource S>
        requires (!std::same_as<std::decay_t<S>, any_buffer_source>)
    any_buffer_source(S s);

    /** Construct by wrapping a BufferSource without ownership.

        Wraps the given source by pointer. The source must remain
        valid for the lifetime of this wrapper. If `S` also
        satisfies @ref ReadSource, native read operations are
        forwarded through the virtual boundary.

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
            On EOF, `ec == cond::eof` and span is empty.

        @par Preconditions
        The wrapper must contain a valid source (`has_value() == true`).
        The caller must not call this function again after a prior
        call returned an error.
    */
    auto
    pull(std::span<const_buffer> dest);

    /** Read some data into a mutable buffer sequence.

        Reads one or more bytes into the caller's buffers. May fill
        less than the full sequence.

        When the wrapped type provides native @ref ReadSource support,
        the operation forwards directly. Otherwise it is synthesized
        from @ref pull, @ref buffer_copy, and @ref consume.

        @param buffers The buffer sequence to fill.

        @return An awaitable yielding `(error_code,std::size_t)`.

        @par Preconditions
        The wrapper must contain a valid source (`has_value() == true`).
        The caller must not call this function again after a prior
        call returned an error (including EOF).

        @see pull, consume
    */
    template<MutableBufferSequence MB>
    io_task<std::size_t>
    read_some(MB buffers);

    /** Read data into a mutable buffer sequence.

        Fills the provided buffer sequence completely. When the
        wrapped type provides native @ref ReadSource support, each
        window is forwarded directly. Otherwise the data is
        synthesized from @ref pull, @ref buffer_copy, and @ref consume.

        @param buffers The buffer sequence to fill.

        @return An awaitable yielding `(error_code,std::size_t)`.
            On success, `n == buffer_size(buffers)`.
            On EOF, `ec == error::eof` and `n` is bytes transferred.

        @par Preconditions
        The wrapper must contain a valid source (`has_value() == true`).
        The caller must not call this function again after a prior
        call returned an error (including EOF).

        @see pull, consume
    */
    template<MutableBufferSequence MB>
    io_task<std::size_t>
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

private:
    /** Forward a partial read through the vtable.

        Constructs the underlying `read_some` awaitable in
        cached storage and returns a type-erased awaitable.
    */
    auto
    read_some_(std::span<mutable_buffer const> buffers);

    /** Forward a complete read through the vtable.

        Constructs the underlying `read` awaitable in
        cached storage and returns a type-erased awaitable.
    */
    auto
    read_(std::span<mutable_buffer const> buffers);
};

//----------------------------------------------------------

/** Type-erased ops for awaitables yielding `io_result<std::span<const_buffer>>`. */
struct any_buffer_source::awaitable_ops
{
    bool (*await_ready)(void*);
    std::coroutine_handle<> (*await_suspend)(void*, std::coroutine_handle<>, io_env const*);
    io_result<std::span<const_buffer>> (*await_resume)(void*);
    void (*destroy)(void*) noexcept;
};

/** Type-erased ops for awaitables yielding `io_result<std::size_t>`. */
struct any_buffer_source::read_awaitable_ops
{
    bool (*await_ready)(void*);
    std::coroutine_handle<> (*await_suspend)(void*, std::coroutine_handle<>, io_env const*);
    io_result<std::size_t> (*await_resume)(void*);
    void (*destroy)(void*) noexcept;
};

struct any_buffer_source::vtable
{
    // BufferSource ops (always populated)
    void (*destroy)(void*) noexcept;
    void (*do_consume)(void* source, std::size_t n) noexcept;
    std::size_t awaitable_size;
    std::size_t awaitable_align;
    awaitable_ops const* (*construct_awaitable)(
        void* source,
        void* storage,
        std::span<const_buffer> dest);

    // ReadSource forwarding (null when wrapped type is BufferSource-only)
    read_awaitable_ops const* (*construct_read_some_awaitable)(
        void* source,
        void* storage,
        std::span<mutable_buffer const> buffers);
    read_awaitable_ops const* (*construct_read_awaitable)(
        void* source,
        void* storage,
        std::span<mutable_buffer const> buffers);
};

template<BufferSource S>
struct any_buffer_source::vtable_for_impl
{
    using PullAwaitable = decltype(std::declval<S&>().pull(
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
        ::new(storage) PullAwaitable(s.pull(dest));

        static constexpr awaitable_ops ops = {
            +[](void* p) {
                return static_cast<PullAwaitable*>(p)->await_ready();
            },
            +[](void* p, std::coroutine_handle<> h, io_env const* env) {
                return detail::call_await_suspend(
                    static_cast<PullAwaitable*>(p), h, env);
            },
            +[](void* p) {
                return static_cast<PullAwaitable*>(p)->await_resume();
            },
            +[](void* p) noexcept {
                static_cast<PullAwaitable*>(p)->~PullAwaitable();
            }
        };
        return &ops;
    }

    //------------------------------------------------------
    // ReadSource forwarding (only instantiated when ReadSource<S>)

    static read_awaitable_ops const*
    construct_read_some_awaitable_impl(
        void* source,
        void* storage,
        std::span<mutable_buffer const> buffers)
        requires ReadSource<S>
    {
        using Aw = decltype(std::declval<S&>().read_some(
            std::span<mutable_buffer const>{}));
        auto& s = *static_cast<S*>(source);
        ::new(storage) Aw(s.read_some(buffers));

        static constexpr read_awaitable_ops ops = {
            +[](void* p) {
                return static_cast<Aw*>(p)->await_ready();
            },
            +[](void* p, std::coroutine_handle<> h, io_env const* env) {
                return detail::call_await_suspend(
                    static_cast<Aw*>(p), h, env);
            },
            +[](void* p) {
                return static_cast<Aw*>(p)->await_resume();
            },
            +[](void* p) noexcept {
                static_cast<Aw*>(p)->~Aw();
            }
        };
        return &ops;
    }

    static read_awaitable_ops const*
    construct_read_awaitable_impl(
        void* source,
        void* storage,
        std::span<mutable_buffer const> buffers)
        requires ReadSource<S>
    {
        using Aw = decltype(std::declval<S&>().read(
            std::span<mutable_buffer const>{}));
        auto& s = *static_cast<S*>(source);
        ::new(storage) Aw(s.read(buffers));

        static constexpr read_awaitable_ops ops = {
            +[](void* p) {
                return static_cast<Aw*>(p)->await_ready();
            },
            +[](void* p, std::coroutine_handle<> h, io_env const* env) {
                return detail::call_await_suspend(
                    static_cast<Aw*>(p), h, env);
            },
            +[](void* p) {
                return static_cast<Aw*>(p)->await_resume();
            },
            +[](void* p) noexcept {
                static_cast<Aw*>(p)->~Aw();
            }
        };
        return &ops;
    }

    //------------------------------------------------------

    static consteval std::size_t
    compute_max_size() noexcept
    {
        std::size_t s = sizeof(PullAwaitable);
        if constexpr (ReadSource<S>)
        {
            using RS = decltype(std::declval<S&>().read_some(
                std::span<mutable_buffer const>{}));
            using R = decltype(std::declval<S&>().read(
                std::span<mutable_buffer const>{}));

            if(sizeof(RS) > s) s = sizeof(RS);
            if(sizeof(R) > s) s = sizeof(R);
        }
        return s;
    }

    static consteval std::size_t
    compute_max_align() noexcept
    {
        std::size_t a = alignof(PullAwaitable);
        if constexpr (ReadSource<S>)
        {
            using RS = decltype(std::declval<S&>().read_some(
                std::span<mutable_buffer const>{}));
            using R = decltype(std::declval<S&>().read(
                std::span<mutable_buffer const>{}));

            if(alignof(RS) > a) a = alignof(RS);
            if(alignof(R) > a) a = alignof(R);
        }
        return a;
    }

    static consteval vtable
    make_vtable() noexcept
    {
        vtable v{};
        v.destroy = &do_destroy_impl;
        v.do_consume = &do_consume_impl;
        v.awaitable_size = compute_max_size();
        v.awaitable_align = compute_max_align();
        v.construct_awaitable = &construct_awaitable_impl;
        v.construct_read_some_awaitable = nullptr;
        v.construct_read_awaitable = nullptr;

        if constexpr (ReadSource<S>)
        {
            v.construct_read_some_awaitable =
                &construct_read_some_awaitable_impl;
            v.construct_read_awaitable =
                &construct_read_awaitable_impl;
        }
        return v;
    }

    static constexpr vtable value = make_vtable();
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
        active_read_ops_ = std::exchange(other.active_read_ops_, nullptr);
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

    cached_awaitable_ = ::operator new(vt_->awaitable_size);

    g.committed = true;
}

template<BufferSource S>
any_buffer_source::any_buffer_source(S* s)
    : source_(s)
    , vt_(&vtable_for_impl<S>::value)
{
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
        await_ready()
        {
            self_->active_ops_ = self_->vt_->construct_awaitable(
                self_->source_,
                self_->cached_awaitable_,
                dest_);
            return self_->active_ops_->await_ready(self_->cached_awaitable_);
        }

        std::coroutine_handle<>
        await_suspend(std::coroutine_handle<> h, io_env const* env)
        {
            return self_->active_ops_->await_suspend(
                self_->cached_awaitable_, h, env);
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

//----------------------------------------------------------
// Private helpers for native ReadSource forwarding

inline auto
any_buffer_source::read_some_(
    std::span<mutable_buffer const> buffers)
{
    struct awaitable
    {
        any_buffer_source* self_;
        std::span<mutable_buffer const> buffers_;

        bool
        await_ready() const noexcept
        {
            return false;
        }

        std::coroutine_handle<>
        await_suspend(std::coroutine_handle<> h, io_env const* env)
        {
            self_->active_read_ops_ =
                self_->vt_->construct_read_some_awaitable(
                    self_->source_,
                    self_->cached_awaitable_,
                    buffers_);

            if(self_->active_read_ops_->await_ready(
                self_->cached_awaitable_))
                return h;

            return self_->active_read_ops_->await_suspend(
                self_->cached_awaitable_, h, env);
        }

        io_result<std::size_t>
        await_resume()
        {
            struct guard {
                any_buffer_source* self;
                ~guard() {
                    self->active_read_ops_->destroy(
                        self->cached_awaitable_);
                    self->active_read_ops_ = nullptr;
                }
            } g{self_};
            return self_->active_read_ops_->await_resume(
                self_->cached_awaitable_);
        }
    };
    return awaitable{this, buffers};
}

inline auto
any_buffer_source::read_(
    std::span<mutable_buffer const> buffers)
{
    struct awaitable
    {
        any_buffer_source* self_;
        std::span<mutable_buffer const> buffers_;

        bool
        await_ready() const noexcept
        {
            return false;
        }

        std::coroutine_handle<>
        await_suspend(std::coroutine_handle<> h, io_env const* env)
        {
            self_->active_read_ops_ =
                self_->vt_->construct_read_awaitable(
                    self_->source_,
                    self_->cached_awaitable_,
                    buffers_);

            if(self_->active_read_ops_->await_ready(
                self_->cached_awaitable_))
                return h;

            return self_->active_read_ops_->await_suspend(
                self_->cached_awaitable_, h, env);
        }

        io_result<std::size_t>
        await_resume()
        {
            struct guard {
                any_buffer_source* self;
                ~guard() {
                    self->active_read_ops_->destroy(
                        self->cached_awaitable_);
                    self->active_read_ops_ = nullptr;
                }
            } g{self_};
            return self_->active_read_ops_->await_resume(
                self_->cached_awaitable_);
        }
    };
    return awaitable{this, buffers};
}

//----------------------------------------------------------
// Public ReadSource methods

template<MutableBufferSequence MB>
io_task<std::size_t>
any_buffer_source::read_some(MB buffers)
{
    buffer_param<MB> bp(buffers);
    auto dest = bp.data();
    if(dest.empty())
        co_return {{}, 0};

    // Native ReadSource path
    if(vt_->construct_read_some_awaitable)
        co_return co_await read_some_(dest);

    // Synthesized path: pull + buffer_copy + consume
    const_buffer arr[detail::max_iovec_];
    auto [ec, bufs] = co_await pull(arr);
    if(ec)
        co_return {ec, 0};

    auto n = buffer_copy(dest, bufs);
    consume(n);
    co_return {{}, n};
}

template<MutableBufferSequence MB>
io_task<std::size_t>
any_buffer_source::read(MB buffers)
{
    buffer_param<MB> bp(buffers);
    std::size_t total = 0;

    // Native ReadSource path
    if(vt_->construct_read_awaitable)
    {
        for(;;)
        {
            auto dest = bp.data();
            if(dest.empty())
                break;

            auto [ec, n] = co_await read_(dest);
            total += n;
            if(ec)
                co_return {ec, total};
            bp.consume(n);
        }
        co_return {{}, total};
    }

    // Synthesized path: pull + buffer_copy + consume
    for(;;)
    {
        auto dest = bp.data();
        if(dest.empty())
            break;

        const_buffer arr[detail::max_iovec_];
        auto [ec, bufs] = co_await pull(arr);

        if(ec)
            co_return {ec, total};

        auto n = buffer_copy(dest, bufs);
        consume(n);
        total += n;
        bp.consume(n);
    }

    co_return {{}, total};
}

//----------------------------------------------------------

static_assert(BufferSource<any_buffer_source>);
static_assert(ReadSource<any_buffer_source>);

} // namespace capy
} // namespace boost

#endif
