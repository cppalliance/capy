//
// Copyright (c) 2025 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_IO_ANY_STREAM_HPP
#define BOOST_CAPY_IO_ANY_STREAM_HPP

#include <boost/capy/detail/config.hpp>
#include <boost/capy/concept/read_stream.hpp>
#include <boost/capy/concept/write_stream.hpp>
#include <boost/capy/io/any_read_stream.hpp>
#include <boost/capy/io/any_write_stream.hpp>

#include <concepts>

namespace boost {
namespace capy {

/** Type-erased wrapper for bidirectional streams.

    This class provides type erasure for any type satisfying both
    the @ref ReadStream and @ref WriteStream concepts, enabling
    runtime polymorphism for bidirectional I/O operations.

    Inherits from both @ref any_read_stream and @ref any_write_stream,
    providing `read_some` and `write_some` operations. Each base
    maintains its own cached awaitable storage, allowing concurrent
    read and write operations.

    The wrapper supports two construction modes:
    - **Owning**: Pass by value to transfer ownership. The wrapper
      allocates storage and owns the stream.
    - **Reference**: Pass a pointer to wrap without ownership. The
      pointed-to stream must outlive this wrapper.

    @par Implicit Conversion
    This class implicitly converts to `any_read_stream&` or
    `any_write_stream&`, allowing it to be passed to functions
    that accept only one capability. However, do not move through
    a base reference as this would leave the other base in an
    invalid state.

    @par Thread Safety
    Not thread-safe. Concurrent operations of the same type
    (two reads or two writes) are undefined behavior. One read
    and one write may be in flight simultaneously.

    @par Example
    @code
    // Owning - takes ownership of the stream
    any_stream stream(socket{ioc});

    // Reference - wraps without ownership
    socket sock(ioc);
    any_stream stream(&sock);

    // Use read_some from any_read_stream base
    mutable_buffer rbuf(rdata, rsize);
    auto [ec1, n1] = co_await stream.read_some(std::span(&rbuf, 1));

    // Use write_some from any_write_stream base
    const_buffer wbuf(wdata, wsize);
    auto [ec2, n2] = co_await stream.write_some(std::span(&wbuf, 1));

    // Pass to functions expecting one capability
    void reader(any_read_stream&);
    void writer(any_write_stream&);
    reader(stream);  // Implicit upcast
    writer(stream);  // Implicit upcast
    @endcode

    @see any_read_stream, any_write_stream, ReadStream, WriteStream
*/
class any_stream
    : public any_read_stream
    , public any_write_stream
{
    void* storage_ = nullptr;
    void* stream_ptr_ = nullptr;
    void (*destroy_)(void*) noexcept = nullptr;

public:
    /** Destructor.

        Destroys the owned stream (if any). Base class destructors
        handle their cached awaitable storage.
    */
    ~any_stream()
    {
        if(storage_)
        {
            destroy_(stream_ptr_);
            ::operator delete(storage_);
        }
    }

    /** Default constructor.

        Constructs an empty wrapper. Operations on a default-constructed
        wrapper result in undefined behavior.
    */
    any_stream() = default;

    /** Non-copyable.

        The awaitable caches are per-instance and cannot be shared.
    */
    any_stream(any_stream const&) = delete;
    any_stream& operator=(any_stream const&) = delete;

    /** Move constructor.

        Transfers ownership from both bases and the owned stream (if any).

        @param other The wrapper to move from.
    */
    any_stream(any_stream&& other) noexcept
        : any_read_stream(std::move(static_cast<any_read_stream&>(other)))
        , any_write_stream(std::move(static_cast<any_write_stream&>(other)))
        , storage_(std::exchange(other.storage_, nullptr))
        , stream_ptr_(std::exchange(other.stream_ptr_, nullptr))
        , destroy_(std::exchange(other.destroy_, nullptr))
    {
    }

    /** Move assignment operator.

        Destroys any owned stream and releases existing resources,
        then transfers ownership from `other`.

        @param other The wrapper to move from.
        @return Reference to this wrapper.
    */
    any_stream&
    operator=(any_stream&& other) noexcept
    {
        if(this != &other)
        {
            if(storage_)
            {
                destroy_(stream_ptr_);
                ::operator delete(storage_);
            }
            static_cast<any_read_stream&>(*this) =
                std::move(static_cast<any_read_stream&>(other));
            static_cast<any_write_stream&>(*this) =
                std::move(static_cast<any_write_stream&>(other));
            storage_ = std::exchange(other.storage_, nullptr);
            stream_ptr_ = std::exchange(other.stream_ptr_, nullptr);
            destroy_ = std::exchange(other.destroy_, nullptr);
        }
        return *this;
    }

    /** Construct by taking ownership of a bidirectional stream.

        Allocates storage and moves the stream into this wrapper.
        The wrapper owns the stream and will destroy it.

        @param s The stream to take ownership of. Must satisfy both
            ReadStream and WriteStream concepts.
    */
    template<class S>
        requires ReadStream<S> && WriteStream<S> &&
            (!std::same_as<std::decay_t<S>, any_stream>)
    any_stream(S s)
    {
        struct guard {
            any_stream* self;
            void* ptr = nullptr;
            bool committed = false;
            ~guard() {
                if(!committed && ptr) {
                    static_cast<S*>(ptr)->~S();
                    ::operator delete(self->storage_);
                    self->storage_ = nullptr;
                }
            }
        } g{this};

        storage_ = ::operator new(sizeof(S));
        S* ptr = ::new(storage_) S(std::move(s));
        g.ptr = ptr;
        stream_ptr_ = ptr;
        destroy_ = +[](void* p) noexcept { static_cast<S*>(p)->~S(); };

        // Initialize bases with pointer (reference semantics)
        static_cast<any_read_stream&>(*this) = any_read_stream(ptr);
        static_cast<any_write_stream&>(*this) = any_write_stream(ptr);

        g.committed = true;
    }

    /** Construct by wrapping a bidirectional stream without ownership.

        Wraps the given stream by pointer. The stream must remain
        valid for the lifetime of this wrapper.

        @param s Pointer to the stream to wrap. Must satisfy both
            ReadStream and WriteStream concepts.
    */
    template<class S>
        requires ReadStream<S> && WriteStream<S>
    any_stream(S* s)
        : any_read_stream(s)
        , any_write_stream(s)
    {
        // storage_ remains nullptr - no ownership
    }

    /** Check if the wrapper contains a valid stream.

        Both bases must be valid for the wrapper to be valid.

        @return `true` if wrapping a stream, `false` if default-constructed
            or moved-from.
    */
    bool
    has_value() const noexcept
    {
        return any_read_stream::has_value() &&
               any_write_stream::has_value();
    }

    /** Check if the wrapper contains a valid stream.

        Both bases must be valid for the wrapper to be valid.

        @return `true` if wrapping a stream, `false` if default-constructed
            or moved-from.
    */
    explicit
    operator bool() const noexcept
    {
        return has_value();
    }
};

} // namespace capy
} // namespace boost

#endif
