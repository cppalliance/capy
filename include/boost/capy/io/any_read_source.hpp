//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_IO_ANY_READ_SOURCE_HPP
#define BOOST_CAPY_IO_ANY_READ_SOURCE_HPP

#include <boost/capy/detail/config.hpp>
#include <boost/capy/detail/await_suspend_helper.hpp>
#include <boost/capy/buffers.hpp>
#include <boost/capy/detail/buffer_array.hpp>
#include <boost/capy/buffers/buffer_param.hpp>
#include <boost/capy/concept/io_awaitable.hpp>
#include <boost/capy/concept/read_source.hpp>
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

/** Type-erased wrapper for any ReadSource.

    This class provides type erasure for any type satisfying the
    @ref ReadSource concept, enabling runtime polymorphism for
    source read operations. It uses cached awaitable storage to achieve
    zero steady-state allocation after construction.

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

    @par Immediate Completion
    Operations complete immediately without suspending when the
    buffer sequence is empty, or when the underlying source's
    awaitable reports readiness via `await_ready`.

    @par Thread Safety
    Not thread-safe. Concurrent operations on the same wrapper
    are undefined behavior.

    @par Example
    @code
    // Owning - takes ownership of the source
    any_read_source rs(some_source{args...});

    // Reference - wraps without ownership
    some_source source;
    any_read_source rs(&source);

    mutable_buffer buf(data, size);
    auto [ec, n] = co_await rs.read(std::span(&buf, 1));
    @endcode

    @see any_read_stream, ReadSource
*/
class any_read_source
{
    struct vtable;
    struct awaitable_ops;

    template<ReadSource S>
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
    ~any_read_source();

    /** Construct a default instance.

        Constructs an empty wrapper. Operations on a default-constructed
        wrapper result in undefined behavior.
    */
    any_read_source() = default;

    /** Non-copyable.

        The awaitable cache is per-instance and cannot be shared.
    */
    any_read_source(any_read_source const&) = delete;
    any_read_source& operator=(any_read_source const&) = delete;

    /** Construct by moving.

        Transfers ownership of the wrapped source (if owned) and
        cached awaitable storage from `other`. After the move, `other` is
        in a default-constructed state.

        @param other The wrapper to move from.
    */
    any_read_source(any_read_source&& other) noexcept
        : source_(std::exchange(other.source_, nullptr))
        , vt_(std::exchange(other.vt_, nullptr))
        , cached_awaitable_(std::exchange(other.cached_awaitable_, nullptr))
        , storage_(std::exchange(other.storage_, nullptr))
        , active_ops_(std::exchange(other.active_ops_, nullptr))
    {
    }

    /** Assign by moving.

        Destroys any owned source and releases existing resources,
        then transfers ownership from `other`.

        @param other The wrapper to move from.
        @return Reference to this wrapper.
    */
    any_read_source&
    operator=(any_read_source&& other) noexcept;

    /** Construct by taking ownership of a ReadSource.

        Allocates storage and moves the source into this wrapper.
        The wrapper owns the source and will destroy it.

        @param s The source to take ownership of.
    */
    template<ReadSource S>
        requires (!std::same_as<std::decay_t<S>, any_read_source>)
    any_read_source(S s);

    /** Construct by wrapping a ReadSource without ownership.

        Wraps the given source by pointer. The source must remain
        valid for the lifetime of this wrapper.

        @param s Pointer to the source to wrap.
    */
    template<ReadSource S>
    any_read_source(S* s);

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

    /** Initiate a partial read operation.

        Attempt to read up to `buffer_size( buffers )` bytes into
        the provided buffer sequence. May fill less than the
        full sequence.

        @param buffers The buffer sequence to read into.

        @return An awaitable that await-returns `(error_code,std::size_t)`.

        @par Immediate Completion
        The operation completes immediately without suspending
        the calling coroutine when:
        @li The buffer sequence is empty, returning `{error_code{}, 0}`.
        @li The underlying source's awaitable reports immediate
            readiness via `await_ready`.

        @note This is a partial operation and may not process the
        entire buffer sequence. Use @ref read for guaranteed
        complete transfer.

        @par Preconditions
        The wrapper must contain a valid source (`has_value() == true`).
        The caller must not call this function again after a prior
        call returned an error (including EOF).
    */
    template<MutableBufferSequence MB>
    auto
    read_some(MB buffers);

    /** Initiate a complete read operation.

        Reads data into the provided buffer sequence by forwarding
        to the underlying source's `read` operation. Large buffer
        sequences are processed in windows, with each window
        forwarded as a separate `read` call to the underlying source.
        The operation completes when the entire buffer sequence is
        filled, end-of-file is reached, or an error occurs.

        @param buffers The buffer sequence to read into.

        @return An awaitable that await-returns `(error_code,std::size_t)`.

        @par Immediate Completion
        The operation completes immediately without suspending
        the calling coroutine when:
        @li The buffer sequence is empty, returning `{error_code{}, 0}`.
        @li The underlying source's `read` awaitable reports
            immediate readiness via `await_ready`.

        @par Postconditions
        Exactly one of the following is true on return:
        @li **Success**: `!ec` and `n == buffer_size(buffers)`.
            The entire buffer was filled.
        @li **End-of-stream or Error**: `ec` and `n` indicates
            the number of bytes transferred before the failure.

        @par Preconditions
        The wrapper must contain a valid source (`has_value() == true`).
        The caller must not call this function again after a prior
        call returned an error (including EOF).
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
    template<ReadSource S>
    void
    rebind(S& new_source) noexcept
    {
        if(vt_ != &vtable_for_impl<S>::value)
            std::terminate();
        source_ = &new_source;
    }

private:
    auto
    read_(std::span<mutable_buffer const> buffers);
};

// ordered by call sequence for cache line coherence
struct any_read_source::awaitable_ops
{
    bool (*await_ready)(void*);
    std::coroutine_handle<> (*await_suspend)(void*, std::coroutine_handle<>, io_env const*);
    io_result<std::size_t> (*await_resume)(void*);
    void (*destroy)(void*) noexcept;
};

// ordered by call frequency for cache line coherence
struct any_read_source::vtable
{
    awaitable_ops const* (*construct_read_some_awaitable)(
        void* source,
        void* storage,
        std::span<mutable_buffer const> buffers);
    awaitable_ops const* (*construct_read_awaitable)(
        void* source,
        void* storage,
        std::span<mutable_buffer const> buffers);
    std::size_t awaitable_size;
    std::size_t awaitable_align;
    void (*destroy)(void*) noexcept;
};

template<ReadSource S>
struct any_read_source::vtable_for_impl
{
    using ReadSomeAwaitable = decltype(std::declval<S&>().read_some(
        std::span<mutable_buffer const>{}));
    using ReadAwaitable = decltype(std::declval<S&>().read(
        std::span<mutable_buffer const>{}));

    static void
    do_destroy_impl(void* source) noexcept
    {
        static_cast<S*>(source)->~S();
    }

    static awaitable_ops const*
    construct_read_some_awaitable_impl(
        void* source,
        void* storage,
        std::span<mutable_buffer const> buffers)
    {
        auto& s = *static_cast<S*>(source);
        ::new(storage) ReadSomeAwaitable(s.read_some(buffers));

        static constexpr awaitable_ops ops = {
            +[](void* p) {
                return static_cast<ReadSomeAwaitable*>(p)->await_ready();
            },
            +[](void* p, std::coroutine_handle<> h, io_env const* env) {
                return detail::call_await_suspend(
                    static_cast<ReadSomeAwaitable*>(p), h, env);
            },
            +[](void* p) {
                return static_cast<ReadSomeAwaitable*>(p)->await_resume();
            },
            +[](void* p) noexcept {
                static_cast<ReadSomeAwaitable*>(p)->~ReadSomeAwaitable();
            }
        };
        return &ops;
    }

    static awaitable_ops const*
    construct_read_awaitable_impl(
        void* source,
        void* storage,
        std::span<mutable_buffer const> buffers)
    {
        auto& s = *static_cast<S*>(source);
        ::new(storage) ReadAwaitable(s.read(buffers));

        static constexpr awaitable_ops ops = {
            +[](void* p) {
                return static_cast<ReadAwaitable*>(p)->await_ready();
            },
            +[](void* p, std::coroutine_handle<> h, io_env const* env) {
                return detail::call_await_suspend(
                    static_cast<ReadAwaitable*>(p), h, env);
            },
            +[](void* p) {
                return static_cast<ReadAwaitable*>(p)->await_resume();
            },
            +[](void* p) noexcept {
                static_cast<ReadAwaitable*>(p)->~ReadAwaitable();
            }
        };
        return &ops;
    }

    static constexpr std::size_t max_awaitable_size =
        sizeof(ReadSomeAwaitable) > sizeof(ReadAwaitable)
            ? sizeof(ReadSomeAwaitable)
            : sizeof(ReadAwaitable);
    static constexpr std::size_t max_awaitable_align =
        alignof(ReadSomeAwaitable) > alignof(ReadAwaitable)
            ? alignof(ReadSomeAwaitable)
            : alignof(ReadAwaitable);

    static constexpr vtable value = {
        &construct_read_some_awaitable_impl,
        &construct_read_awaitable_impl,
        max_awaitable_size,
        max_awaitable_align,
        &do_destroy_impl
    };
};

inline
any_read_source::~any_read_source()
{
    if(storage_)
    {
        vt_->destroy(source_);
        ::operator delete(storage_);
    }
    if(cached_awaitable_)
    {
        if(active_ops_)
            active_ops_->destroy(cached_awaitable_);
        ::operator delete(cached_awaitable_);
    }
}

inline any_read_source&
any_read_source::operator=(any_read_source&& other) noexcept
{
    if(this != &other)
    {
        if(storage_)
        {
            vt_->destroy(source_);
            ::operator delete(storage_);
        }
        if(cached_awaitable_)
        {
            if(active_ops_)
                active_ops_->destroy(cached_awaitable_);
            ::operator delete(cached_awaitable_);
        }
        source_ = std::exchange(other.source_, nullptr);
        vt_ = std::exchange(other.vt_, nullptr);
        cached_awaitable_ = std::exchange(other.cached_awaitable_, nullptr);
        storage_ = std::exchange(other.storage_, nullptr);
        active_ops_ = std::exchange(other.active_ops_, nullptr);
    }
    return *this;
}

template<ReadSource S>
    requires (!std::same_as<std::decay_t<S>, any_read_source>)
any_read_source::any_read_source(S s)
    : vt_(&vtable_for_impl<S>::value)
{
    struct guard {
        any_read_source* self;
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

template<ReadSource S>
any_read_source::any_read_source(S* s)
    : source_(s)
    , vt_(&vtable_for_impl<S>::value)
{
    // Preallocate the awaitable storage
    cached_awaitable_ = ::operator new(vt_->awaitable_size);
}

template<MutableBufferSequence MB>
auto
any_read_source::read_some(MB buffers)
{
    struct awaitable
    {
        any_read_source* self_;
        detail::mutable_buffer_array<detail::max_iovec_> ba_;

        awaitable(any_read_source* self, MB const& buffers)
            : self_(self)
            , ba_(buffers)
        {
        }

        bool
        await_ready() const noexcept
        {
            return ba_.to_span().empty();
        }

        std::coroutine_handle<>
        await_suspend(std::coroutine_handle<> h, io_env const* env)
        {
            self_->active_ops_ = self_->vt_->construct_read_some_awaitable(
                self_->source_,
                self_->cached_awaitable_,
                ba_.to_span());

            if(self_->active_ops_->await_ready(self_->cached_awaitable_))
                return h;

            return self_->active_ops_->await_suspend(
                self_->cached_awaitable_, h, env);
        }

        io_result<std::size_t>
        await_resume()
        {
            if(ba_.to_span().empty())
                return {{}, 0};

            struct guard {
                any_read_source* self;
                ~guard() {
                    self->active_ops_->destroy(self->cached_awaitable_);
                    self->active_ops_ = nullptr;
                }
            } g{self_};
            return self_->active_ops_->await_resume(
                self_->cached_awaitable_);
        }
    };
    return awaitable(this, buffers);
}

inline auto
any_read_source::read_(std::span<mutable_buffer const> buffers)
{
    struct awaitable
    {
        any_read_source* self_;
        std::span<mutable_buffer const> buffers_;

        bool
        await_ready() const noexcept
        {
            return false;
        }

        std::coroutine_handle<>
        await_suspend(std::coroutine_handle<> h, io_env const* env)
        {
            self_->active_ops_ = self_->vt_->construct_read_awaitable(
                self_->source_,
                self_->cached_awaitable_,
                buffers_);

            if(self_->active_ops_->await_ready(self_->cached_awaitable_))
                return h;

            return self_->active_ops_->await_suspend(
                self_->cached_awaitable_, h, env);
        }

        io_result<std::size_t>
        await_resume()
        {
            struct guard {
                any_read_source* self;
                ~guard() {
                    self->active_ops_->destroy(self->cached_awaitable_);
                    self->active_ops_ = nullptr;
                }
            } g{self_};
            return self_->active_ops_->await_resume(
                self_->cached_awaitable_);
        }
    };
    return awaitable{this, buffers};
}

template<MutableBufferSequence MB>
io_task<std::size_t>
any_read_source::read(MB buffers)
{
    buffer_param bp(buffers);
    std::size_t total = 0;

    for(;;)
    {
        auto bufs = bp.data();
        if(bufs.empty())
            break;

        auto [ec, n] = co_await read_(bufs);
        total += n;
        if(ec)
            co_return {ec, total};
        bp.consume(n);
    }

    co_return {{}, total};
}

} // namespace capy
} // namespace boost

#endif
