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
    maintains its own cached coroutine frame, allowing concurrent
    read and write operations.

    The wrapper has reference semantics - it wraps an existing
    stream without taking ownership. The wrapped stream must
    outlive this wrapper.

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
    socket sock(ioc);
    any_stream stream(sock);

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
public:
    /** Default constructor.

        Constructs an empty wrapper. Operations on a default-constructed
        wrapper result in undefined behavior.
    */
    any_stream() = default;

    /** Non-copyable.

        The frame caches are per-instance and cannot be shared.
    */
    any_stream(any_stream const&) = delete;
    any_stream& operator=(any_stream const&) = delete;

    /** Move constructor.

        Transfers ownership from both bases.

        @param other The wrapper to move from.
    */
    any_stream(any_stream&& other) noexcept = default;

    /** Rebinding move constructor.

        Transfers the cached frames and vtables from `other`, but binds
        to a new stream object. Used by owning wrappers when the owned
        object moves to a new location.

        @param other The wrapper to move state from.
        @param new_stream The new stream to bind to. Must be the same
            type as the original stream.
    */
    template<class S>
        requires ReadStream<S> && WriteStream<S>
    any_stream(any_stream&& other, S& new_stream) noexcept
        : any_read_stream(std::move(static_cast<any_read_stream&>(other)), new_stream)
        , any_write_stream(std::move(static_cast<any_write_stream&>(other)), new_stream)
    {
    }

    /** Move assignment operator.

        Releases existing resources and transfers ownership from both bases.

        @param other The wrapper to move from.
        @return Reference to this wrapper.
    */
    any_stream& operator=(any_stream&& other) noexcept = default;

    /** Construct from a bidirectional stream.

        Wraps the given stream for both read and write operations.
        Preallocates coroutine frames for both read and write paths.
        The stream must remain valid for the lifetime of this wrapper.

        @param s The stream to wrap. Must satisfy both ReadStream
            and WriteStream concepts.
    */
    template<class S>
        requires ReadStream<S> && WriteStream<S> &&
            (!std::same_as<std::decay_t<S>, any_stream>)
    any_stream(S& s) noexcept
        : any_read_stream(s)
        , any_write_stream(s)
    {
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

protected:
    /** Rebind to a new stream after move.

        Updates the internal pointers in both bases to reference a new
        stream object. Used by owning wrappers after move assignment
        when the owned object has moved to a new location.

        @param new_stream The new stream to bind to. Must be the same
            type as the original stream.

        @note Terminates if called with a stream of different type
            than the original.
    */
    template<class S>
        requires ReadStream<S> && WriteStream<S>
    void
    rebind(S& new_stream) noexcept
    {
        any_read_stream::rebind(new_stream);
        any_write_stream::rebind(new_stream);
    }
};

} // namespace capy
} // namespace boost

#endif
