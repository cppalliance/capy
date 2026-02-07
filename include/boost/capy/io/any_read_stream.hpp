//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_IO_ANY_READ_STREAM_HPP
#define BOOST_CAPY_IO_ANY_READ_STREAM_HPP

#include <boost/capy/detail/config.hpp>
#include <boost/capy/detail/await_suspend_helper.hpp>
#include <boost/capy/buffers.hpp>
#include <boost/capy/buffers/buffer_array.hpp>
#include <boost/capy/concept/io_awaitable.hpp>
#include <boost/capy/concept/read_stream.hpp>
#include <boost/capy/coro.hpp>
#include <boost/capy/ex/executor_ref.hpp>
#include <boost/capy/io_result.hpp>

#include <concepts>
#include <coroutine>
#include <cstddef>
#include <new>
#include <span>
#include <stop_token>
#include <system_error>
#include <utility>

namespace boost {
namespace capy {

/** Type-erased wrapper for any ReadStream.

    This class provides type erasure for any type satisfying the
    @ref ReadStream concept, enabling runtime polymorphism for
    read operations. It uses cached awaitable storage to achieve
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

    @par Immediate Completion
    When the underlying stream's awaitable reports ready immediately
    (e.g. buffered data already available), the wrapper skips
    coroutine suspension entirely and returns the result inline.

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
    auto [ec, n] = co_await stream.read_some(buf);
    @endcode

    @see any_write_stream, any_stream, ReadStream
*/
class any_read_stream
{
    struct vtable;

    template<ReadStream S>
    struct vtable_for_impl;

    // ordered for cache line coherence
    void* stream_ = nullptr;
    vtable const* vt_ = nullptr;
    void* cached_awaitable_ = nullptr;
    void* storage_ = nullptr;
    bool awaitable_active_ = false;

public:
    /** Destructor.

        Destroys the owned stream (if any) and releases the cached
        awaitable storage.
    */
    ~any_read_stream();

    /** Default constructor.

        Constructs an empty wrapper. Operations on a default-constructed
        wrapper result in undefined behavior.
    */
    any_read_stream() = default;

    /** Non-copyable.

        The awaitable cache is per-instance and cannot be shared.
    */
    any_read_stream(any_read_stream const&) = delete;
    any_read_stream& operator=(any_read_stream const&) = delete;

    /** Move constructor.

        Transfers ownership of the wrapped stream (if owned) and
        cached awaitable storage from `other`. After the move, `other` is
        in a default-constructed state.

        @param other The wrapper to move from.
    */
    any_read_stream(any_read_stream&& other) noexcept
        : stream_(std::exchange(other.stream_, nullptr))
        , vt_(std::exchange(other.vt_, nullptr))
        , cached_awaitable_(std::exchange(other.cached_awaitable_, nullptr))
        , storage_(std::exchange(other.storage_, nullptr))
        , awaitable_active_(std::exchange(other.awaitable_active_, false))
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
    any_read_stream(S* s);

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

        @par Immediate Completion
        The operation completes immediately without suspending
        the calling coroutine when the underlying stream's
        awaitable reports immediate readiness via `await_ready`.

        @note This is a partial operation and may not process the
        entire buffer sequence. Use the composed @ref read algorithm
        for guaranteed complete transfer.

        @par Preconditions
        The wrapper must contain a valid stream (`has_value() == true`).
        The caller must not call this function again after a prior
        call returned an error (including EOF).
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
    // ordered by call frequency for cache line coherence
    void (*construct_awaitable)(
        void* stream,
        void* storage,
        std::span<mutable_buffer const> buffers);
    bool (*await_ready)(void*);
    coro (*await_suspend)(void*, coro, executor_ref, std::stop_token);
    io_result<std::size_t> (*await_resume)(void*);
    void (*destroy_awaitable)(void*) noexcept;
    std::size_t awaitable_size;
    std::size_t awaitable_align;
    void (*destroy)(void*) noexcept;
};

template<ReadStream S>
struct any_read_stream::vtable_for_impl
{
    using Awaitable = decltype(std::declval<S&>().read_some(
        std::span<mutable_buffer const>{}));

    static void
    do_destroy_impl(void* stream) noexcept
    {
        static_cast<S*>(stream)->~S();
    }

    static void
    construct_awaitable_impl(
        void* stream,
        void* storage,
        std::span<mutable_buffer const> buffers)
    {
        auto& s = *static_cast<S*>(stream);
        ::new(storage) Awaitable(s.read_some(buffers));
    }

    static constexpr vtable value = {
        &construct_awaitable_impl,
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
        },
        sizeof(Awaitable),
        alignof(Awaitable),
        &do_destroy_impl
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
    if(cached_awaitable_)
    {
        if(awaitable_active_)
            vt_->destroy_awaitable(cached_awaitable_);
        ::operator delete(cached_awaitable_);
    }
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
        if(cached_awaitable_)
        {
            if(awaitable_active_)
                vt_->destroy_awaitable(cached_awaitable_);
            ::operator delete(cached_awaitable_);
        }
        stream_ = std::exchange(other.stream_, nullptr);
        vt_ = std::exchange(other.vt_, nullptr);
        cached_awaitable_ = std::exchange(other.cached_awaitable_, nullptr);
        storage_ = std::exchange(other.storage_, nullptr);
        awaitable_active_ = std::exchange(other.awaitable_active_, false);
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

    // Preallocate the awaitable storage
    cached_awaitable_ = ::operator new(vt_->awaitable_size);

    g.committed = true;
}

template<ReadStream S>
any_read_stream::any_read_stream(S* s)
    : stream_(s)
    , vt_(&vtable_for_impl<S>::value)
{
    // Preallocate the awaitable storage
    cached_awaitable_ = ::operator new(vt_->awaitable_size);
}

//----------------------------------------------------------

template<MutableBufferSequence MB>
auto
any_read_stream::read_some(MB buffers)
{
    // VFALCO in theory, we could use if constexpr to detect a
    // span and then pass that through to read_some without the array
    struct awaitable
    {
        any_read_stream* self_;
        mutable_buffer_array<detail::max_iovec_> ba_;

        bool
        await_ready()
        {
            self_->vt_->construct_awaitable(
                self_->stream_,
                self_->cached_awaitable_,
                ba_.to_span());
            self_->awaitable_active_ = true;

            return self_->vt_->await_ready(
                self_->cached_awaitable_);
        }

        coro
        await_suspend(coro h, executor_ref ex, std::stop_token token)
        {
            return self_->vt_->await_suspend(
                self_->cached_awaitable_, h, ex, token);
        }

        io_result<std::size_t>
        await_resume()
        {
            struct guard {
                any_read_stream* self;
                ~guard() {
                    self->vt_->destroy_awaitable(self->cached_awaitable_);
                    self->awaitable_active_ = false;
                }
            } g{self_};
            return self_->vt_->await_resume(
                self_->cached_awaitable_);
        }
    };
    return awaitable{this,
        mutable_buffer_array<detail::max_iovec_>(buffers)};
}

} // namespace capy
} // namespace boost

#endif
