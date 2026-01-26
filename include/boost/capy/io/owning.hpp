//
// Copyright (c) 2025 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_IO_OWNING_HPP
#define BOOST_CAPY_IO_OWNING_HPP

#include <boost/capy/detail/config.hpp>
#include <boost/capy/detail/holder.hpp>

#include <type_traits>
#include <utility>

namespace boost {
namespace capy {

/** Owning wrapper for reference-based type erasers.

    This class template creates an owning version of a reference-based
    type eraser. It inherits from the type eraser and owns the concrete
    object that is type-erased.

    The inheritance order ensures correct initialization:
    `holder<T>` is listed first so the owned object is constructed
    before the `Base` type eraser, which receives a reference to it.

    @par Move Semantics
    Move operations transfer both the owned object and the type eraser
    state. The `Base` class must provide:
    - A rebinding move constructor: `Base(Base&&, T&)`
    - A protected `rebind(T&)` method

    @par Non-Copyable
    This class is non-copyable because:
    - The `Base` type eraser is non-copyable (owns frame cache)
    - Copying would require rebinding to a new object location

    @par Example
    @code
    // Create an owning any_buffer_source
    owning<any_buffer_source, my_source> src(arg1, arg2);

    // src IS-A any_buffer_source
    any_buffer_source& ref = src;

    // Access the owned object
    my_source& s = src.get();
    @endcode

    @tparam Base The reference-based type eraser (e.g., any_buffer_source)
    @tparam T The concrete type to own

    @see any_buffer_source, any_read_source, any_read_stream,
         any_write_sink, any_write_stream, any_stream
*/
template<typename Base, typename T>
class owning
    : private detail::holder<T>
    , public Base
{
public:
    /** Construct with forwarded arguments.

        Constructs the owned object with the given arguments,
        then initializes the type eraser with a reference to it.

        @param args Arguments forwarded to T's constructor.
    */
    template<typename... Args>
    owning(Args&&... args)
        : detail::holder<T>(std::forward<Args>(args)...)
        , Base(this->obj_)
    {
    }

    /** Non-copyable. */
    owning(owning const&) = delete;
    owning& operator=(owning const&) = delete;

    /** Move constructor.

        Moves the owned object, then uses the rebinding move
        constructor of `Base` to update its internal pointer
        to the new object location.

        @param other The wrapper to move from.
    */
    owning(owning&& other)
        noexcept(std::is_nothrow_move_constructible_v<T>)
        : detail::holder<T>(std::move(other.obj_))
        , Base(std::move(other), this->obj_)
    {
    }

    /** Move assignment operator.

        Moves the owned object, then moves the `Base` state
        and rebinds to the new object location.

        @param other The wrapper to move from.
        @return Reference to this wrapper.
    */
    owning&
    operator=(owning&& other)
        noexcept(std::is_nothrow_move_assignable_v<T>)
    {
        if(this != &other)
        {
            static_cast<detail::holder<T>&>(*this) =
                std::move(static_cast<detail::holder<T>&>(other));
            static_cast<Base&>(*this) = std::move(static_cast<Base&>(other));
            Base::rebind(this->obj_);
        }
        return *this;
    }

    /** Access the owned object.

        @return Reference to the owned object.
    */
    T&
    get() noexcept
    {
        return this->obj_;
    }

    /** Access the owned object.

        @return Const reference to the owned object.
    */
    T const&
    get() const noexcept
    {
        return this->obj_;
    }
};

} // capy
} // boost

#endif
