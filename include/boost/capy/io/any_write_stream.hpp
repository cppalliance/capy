//
// Copyright (c) 2025 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_IO_ANY_WRITE_STREAM_HPP
#define BOOST_CAPY_IO_ANY_WRITE_STREAM_HPP

#include <boost/capy/detail/config.hpp>
#include <boost/capy/detail/await_suspend_helper.hpp>
#include <boost/capy/buffers.hpp>
#include <boost/capy/buffers/buffer_param.hpp>
#include <boost/capy/concept/io_awaitable.hpp>
#include <boost/capy/concept/write_stream.hpp>
#include <boost/capy/coro.hpp>
#include <boost/capy/ex/executor_ref.hpp>
#include <boost/capy/io_result.hpp>

#include <system_error>

#include <concepts>
#include <coroutine>
#include <cstddef>
#include <new>
#include <span>
#include <stop_token>
#include <utility>

namespace boost {
namespace capy {

/** Type-erased wrapper for any WriteStream.

    This class provides type erasure for any type satisfying the
    @ref WriteStream concept, enabling runtime polymorphism for
    write operations. It uses cached awaitable storage to achieve
    zero steady-state allocation after construction.

    The wrapper supports two construction modes:
    - **Owning**: Pass by value to transfer ownership. The wrapper
      allocates storage and owns the stream.
    - **Reference**: Pass a pointer to wrap without ownership. The
      pointed-to stream must outlive this wrapper.

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
    // Owning - takes ownership of the stream
    any_write_stream stream(socket{ioc});

    // Reference - wraps without ownership
    socket sock(ioc);
    any_write_stream stream(&sock);

    const_buffer buf(data, size);
    auto [ec, n] = co_await stream.write_some(std::span(&buf, 1));
    @endcode

    @see any_read_stream, any_stream, WriteStream
*/
class any_write_stream
{
    struct vtable;
    struct awaitable_ops;

    template<WriteStream S>
    struct vtable_for_impl;

    void* stream_ = nullptr;
    vtable const* vt_ = nullptr;
    void* cached_awaitable_ = nullptr;
    void* storage_ = nullptr;
    awaitable_ops const* active_ops_ = nullptr;

public:
    /** Destructor.

        Destroys the owned stream (if any) and releases the cached
        awaitable storage.
    */
    ~any_write_stream();

    /** Default constructor.

        Constructs an empty wrapper. Operations on a default-constructed
        wrapper result in undefined behavior.
    */
    any_write_stream() = default;

    /** Non-copyable.

        The awaitable cache is per-instance and cannot be shared.
    */
    any_write_stream(any_write_stream const&) = delete;
    any_write_stream& operator=(any_write_stream const&) = delete;

    /** Move constructor.

        Transfers ownership of the wrapped stream (if owned) and
        cached awaitable storage from `other`. After the move, `other` is
        in a default-constructed state.

        @param other The wrapper to move from.
    */
    any_write_stream(any_write_stream&& other) noexcept
        : stream_(std::exchange(other.stream_, nullptr))
        , vt_(std::exchange(other.vt_, nullptr))
        , cached_awaitable_(std::exchange(other.cached_awaitable_, nullptr))
        , storage_(std::exchange(other.storage_, nullptr))
        , active_ops_(std::exchange(other.active_ops_, nullptr))
    {
    }

    /** Move assignment operator.

        Destroys any owned stream and releases existing resources,
        then transfers ownership from `other`.

        @param other The wrapper to move from.
        @return Reference to this wrapper.
    */
    any_write_stream&
    operator=(any_write_stream&& other) noexcept;

    /** Construct by taking ownership of a WriteStream.

        Allocates storage and moves the stream into this wrapper.
        The wrapper owns the stream and will destroy it.

        @param s The stream to take ownership of.
    */
    template<WriteStream S>
        requires (!std::same_as<std::decay_t<S>, any_write_stream>)
    any_write_stream(S s);

    /** Construct by wrapping a WriteStream without ownership.

        Wraps the given stream by pointer. The stream must remain
        valid for the lifetime of this wrapper.

        @param s Pointer to the stream to wrap.
    */
    template<WriteStream S>
    any_write_stream(S* s);

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

    /** Initiate an asynchronous write operation.

        Writes data from the provided buffer sequence. The operation
        completes when at least one byte has been written, or an error
        occurs.

        @param buffers The buffer sequence containing data to write.
            Passed by value to ensure the sequence lives in the
            coroutine frame across suspension points.

        @return An awaitable yielding `(error_code,std::size_t)`.

        @par Preconditions
        The wrapper must contain a valid stream (`has_value() == true`).
    */
    template<ConstBufferSequence CB>
    auto
    write_some(CB buffers);

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
    template<WriteStream S>
    void
    rebind(S& new_stream) noexcept
    {
        if(vt_ != &vtable_for_impl<S>::value)
            std::terminate();
        stream_ = &new_stream;
    }
};

//----------------------------------------------------------

struct any_write_stream::awaitable_ops
{
    bool (*await_ready)(void*);
    coro (*await_suspend)(void*, coro, executor_ref, std::stop_token);
    io_result<std::size_t> (*await_resume)(void*);
    void (*destroy)(void*) noexcept;
};

struct any_write_stream::vtable
{
    void (*destroy)(void*) noexcept;
    std::size_t awaitable_size;
    std::size_t awaitable_align;
    awaitable_ops const* (*construct_awaitable)(
        void* stream,
        void* storage,
        std::span<const_buffer const> buffers);
};

template<WriteStream S>
struct any_write_stream::vtable_for_impl
{
    using Awaitable = decltype(std::declval<S&>().write_some(
        std::span<const_buffer const>{}));

    static void
    do_destroy_impl(void* stream) noexcept
    {
        static_cast<S*>(stream)->~S();
    }

    static awaitable_ops const*
    construct_awaitable_impl(
        void* stream,
        void* storage,
        std::span<const_buffer const> buffers)
    {
        auto& s = *static_cast<S*>(stream);
        ::new(storage) Awaitable(s.write_some(buffers));

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
        sizeof(Awaitable),
        alignof(Awaitable),
        &construct_awaitable_impl
    };
};

//----------------------------------------------------------

inline
any_write_stream::~any_write_stream()
{
    if(storage_)
    {
        vt_->destroy(stream_);
        ::operator delete(storage_);
    }
    if(cached_awaitable_)
        ::operator delete(cached_awaitable_);
}

inline any_write_stream&
any_write_stream::operator=(any_write_stream&& other) noexcept
{
    if(this != &other)
    {
        if(storage_)
        {
            vt_->destroy(stream_);
            ::operator delete(storage_);
        }
        if(cached_awaitable_)
            ::operator delete(cached_awaitable_);
        stream_ = std::exchange(other.stream_, nullptr);
        vt_ = std::exchange(other.vt_, nullptr);
        cached_awaitable_ = std::exchange(other.cached_awaitable_, nullptr);
        storage_ = std::exchange(other.storage_, nullptr);
        active_ops_ = std::exchange(other.active_ops_, nullptr);
    }
    return *this;
}

template<WriteStream S>
    requires (!std::same_as<std::decay_t<S>, any_write_stream>)
any_write_stream::any_write_stream(S s)
    : vt_(&vtable_for_impl<S>::value)
{
    struct guard {
        any_write_stream* self;
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

    // Preallocate the awaitable storage
    cached_awaitable_ = ::operator new(vt_->awaitable_size);

    g.committed = true;
}

template<WriteStream S>
any_write_stream::any_write_stream(S* s)
    : stream_(s)
    , vt_(&vtable_for_impl<S>::value)
{
    // Preallocate the awaitable storage
    cached_awaitable_ = ::operator new(vt_->awaitable_size);
}

//----------------------------------------------------------

template<ConstBufferSequence CB>
auto
any_write_stream::write_some(CB buffers)
{
    struct awaitable
    {
        any_write_stream* self_;
        buffer_param<CB> bp_;

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
                self_->stream_,
                self_->cached_awaitable_,
                bp_.data());

            // Check if underlying is immediately ready
            if(self_->active_ops_->await_ready(self_->cached_awaitable_))
                return h;

            // Forward to underlying awaitable
            return self_->active_ops_->await_suspend(
                self_->cached_awaitable_, h, ex, token);
        }

        io_result<std::size_t>
        await_resume()
        {
            struct guard {
                any_write_stream* self;
                ~guard() {
                    self->active_ops_->destroy(self->cached_awaitable_);
                    self->active_ops_ = nullptr;
                }
            } g{self_};
            return self_->active_ops_->await_resume(
                self_->cached_awaitable_);
        }
    };
    return awaitable{this, buffer_param<CB>(buffers)};
}

} // namespace capy
} // namespace boost

#endif
