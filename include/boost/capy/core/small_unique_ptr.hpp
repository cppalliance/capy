//
// Copyright (c) 2025 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_SMALL_UNIQUE_PTR_HPP
#define BOOST_CAPY_SMALL_UNIQUE_PTR_HPP

#include <boost/capy/detail/config.hpp>
#include <cstddef>
#include <type_traits>
#include <utility>
#include <new>

namespace boost {
namespace capy {

/** A smart pointer with small buffer optimization.

    This class provides unique ownership semantics similar
    to `std::unique_ptr`, but with an embedded buffer for
    small object optimization. Objects that fit within the
    buffer are constructed in-place, avoiding heap allocation.
    Larger objects fall back to heap allocation.

    The SBO path requires the managed type to be nothrow
    move constructible. Types that do not meet this requirement
    or exceed the buffer size are heap-allocated.

    @tparam T The base type of the managed object. Pointers
    to derived types may be stored if convertible to `T*`.

    @tparam N The size of the internal buffer in bytes.
    Defaults to `sizeof(T)`.

    @par Example
    @code
    struct Base { virtual ~Base() = default; };
    struct Derived : Base { int x; };

    // Uses SBO if Derived fits in buffer
    auto p = make_small_unique<Base, 32, Derived>(42);
    @endcode

    @see make_small_unique
*/
template<class T, std::size_t N = sizeof(T)>
class small_unique_ptr
{
    static_assert(N >= sizeof(T), "Buffer too small for base type");

    template<class U>
    struct fits_in_buffer
        : std::integral_constant<
            bool,
            (sizeof(U) <= N) &&
            (alignof(U) <= alignof(std::max_align_t))>
    {
    };

    alignas(std::max_align_t) unsigned char buffer_[N];
    T* ptr_;
    void (*destroy_)(small_unique_ptr*);
    void (*relocate_)(small_unique_ptr*, small_unique_ptr*);

    template<class U>
    static void destroy_small(small_unique_ptr* self)
    {
        static_cast<U*>(self->ptr_)->~U();
    }

    template<class U>
    static void destroy_large(small_unique_ptr* self)
    {
        delete static_cast<U*>(self->ptr_);
    }

    template<class U>
    static void relocate_small(
        small_unique_ptr* src,
        small_unique_ptr* dst)
    {
        static_assert(
            std::is_nothrow_move_constructible<U>::value,
            "U must be nothrow move constructible for SBO");
        U* p = static_cast<U*>(src->ptr_);
        dst->ptr_ = ::new(static_cast<void*>(&dst->buffer_)) U(std::move(*p));
        dst->destroy_ = src->destroy_;
        dst->relocate_ = src->relocate_;
        p->~U();
        src->ptr_ = nullptr;
        src->destroy_ = nullptr;
        src->relocate_ = nullptr;
    }

    template<class U>
    static void relocate_large(
        small_unique_ptr* src,
        small_unique_ptr* dst)
    {
        dst->ptr_ = src->ptr_;
        dst->destroy_ = src->destroy_;
        dst->relocate_ = src->relocate_;
        src->ptr_ = nullptr;
        src->destroy_ = nullptr;
        src->relocate_ = nullptr;
    }

    void clear() noexcept
    {
        ptr_ = nullptr;
        destroy_ = nullptr;
        relocate_ = nullptr;
    }

public:
    /** The type of the managed object.
    */
    typedef T element_type;

    /** The pointer type.
    */
    typedef T* pointer;

    /** Default constructor.

        Constructs an empty smart pointer that owns nothing.

        @post `get() == nullptr`
    */
    small_unique_ptr() noexcept
        : ptr_(nullptr)
        , destroy_(nullptr)
        , relocate_(nullptr)
    {
    }

    /** Construct from nullptr.

        Constructs an empty smart pointer that owns nothing.

        @post `get() == nullptr`
    */
    small_unique_ptr(std::nullptr_t) noexcept
        : small_unique_ptr()
    {
    }

    /** Construct from a raw pointer.

        Takes ownership of a heap-allocated object. The
        pointer must have been allocated with `new` and
        will be deleted when this smart pointer is destroyed.

        @note This constructor always uses the heap path.
        Use @ref emplace or @ref make_small_unique to
        benefit from small buffer optimization.

        @param p A pointer to a heap-allocated object,
        or `nullptr`.

        @tparam U The dynamic type. Must be convertible to `T*`.

        @post `get() == p`
    */
    template<class U
        ,class = typename std::enable_if<
            std::is_convertible<U*, T*>::value>::type>
    explicit
    small_unique_ptr(
        U* p) noexcept
        : ptr_(p)
        , destroy_(p ? &destroy_large<U> : nullptr)
        , relocate_(p ? &relocate_large<U> : nullptr)
    {
    }

    /** Copy constructor (deleted).

        small_unique_ptr is move-only.
    */
    small_unique_ptr(small_unique_ptr const&) = delete;

    /** Copy assignment (deleted).

        small_unique_ptr is move-only.
    */
    small_unique_ptr& operator=(small_unique_ptr const&) = delete;

    /** Move constructor.

        Transfers ownership from `other` to `*this`.
        For SBO objects, the managed object is move-constructed
        into this instance's buffer. For heap objects, the
        pointer is transferred.

        @param other The source smart pointer. Will be empty
        after this call.

        @post `other.get() == nullptr`
    */
    small_unique_ptr(small_unique_ptr&& other) noexcept
        : ptr_(nullptr)
        , destroy_(nullptr)
        , relocate_(nullptr)
    {
        if(other.ptr_)
            other.relocate_(&other, this);
    }

    /** Move assignment.

        Destroys any currently managed object, then transfers
        ownership from `other` to `*this`.

        @param other The source smart pointer. Will be empty
        after this call.

        @return `*this`

        @post `other.get() == nullptr`
    */
    small_unique_ptr& operator=(small_unique_ptr&& other) noexcept
    {
        if(this != &other)
        {
            reset();
            if(other.ptr_)
                other.relocate_(&other, this);
        }
        return *this;
    }

    /** Assign nullptr.

        Destroys any currently managed object and resets
        to empty state.

        @return `*this`

        @post `get() == nullptr`
    */
    small_unique_ptr& operator=(std::nullptr_t) noexcept
    {
        reset();
        return *this;
    }

    /** Destructor.

        Destroys the managed object if one exists. SBO
        objects are destroyed in-place; heap objects
        are deleted.
    */
    ~small_unique_ptr() noexcept
    {
        reset();
    }

    /** Destroy the managed object.

        If a managed object exists, it is destroyed (either
        in-place for SBO or deleted for heap). After this
        call, the smart pointer is empty.

        @post `get() == nullptr`
    */
    void reset() noexcept
    {
        if(ptr_)
        {
            destroy_(this);
            clear();
        }
    }

    /** Release ownership.

        Returns the managed pointer and relinquishes
        ownership without destroying the object. The
        caller is responsible for deleting the returned
        pointer.

        @warning For SBO objects, the returned pointer
        points to memory inside this object's buffer,
        which becomes invalid when this object is
        destroyed or reused.

        @return The previously managed pointer, or
        `nullptr` if empty.

        @post `get() == nullptr`
    */
    pointer release() noexcept
    {
        pointer p = ptr_;
        clear();
        return p;
    }

    /** Get the managed pointer.

        @return The stored pointer, or `nullptr` if empty.
    */
    pointer get() const noexcept
    {
        return ptr_;
    }

    /** Check if non-empty.

        @return `true` if this manages an object,
        `false` otherwise.
    */
    explicit operator bool() const noexcept
    {
        return ptr_ != nullptr;
    }

    /** Dereference the managed object.

        @return A reference to the managed object.

        @pre `get() != nullptr`
    */
    typename std::add_lvalue_reference<T>::type
    operator*() const noexcept
    {
        return *ptr_;
    }

    /** Member access.

        @return The stored pointer.

        @pre `get() != nullptr`
    */
    pointer operator->() const noexcept
    {
        return ptr_;
    }

    /** Swap with another small_unique_ptr.

        Exchanges the managed objects between `*this`
        and `other`.

        @param other The smart pointer to swap with.
    */
    void swap(small_unique_ptr& other) noexcept
    {
        small_unique_ptr tmp(std::move(other));
        other = std::move(*this);
        *this = std::move(tmp);
    }

    /** Construct a managed object in-place.

        Creates an object of type `U` with the given
        arguments. If `U` fits in the buffer and is
        nothrow move constructible, SBO is used;
        otherwise the object is heap-allocated.

        @param args Constructor arguments for `U`.

        @tparam U The concrete type to construct.
        Must be convertible to `T*`.

        @tparam Args Constructor argument types.

        @return A small_unique_ptr managing the new object.

        @throws Any exception thrown by `U`'s constructor.
        For heap allocation, may also throw `std::bad_alloc`.
    */
    // SBO path
    template<class U, class... Args>
    static
    typename std::enable_if<
        fits_in_buffer<U>::value &&
        std::is_convertible<U*, T*>::value,
        small_unique_ptr>::type
    emplace(Args&&... args)
    {
        static_assert(
            std::is_nothrow_move_constructible<U>::value,
            "U must be nothrow move constructible for SBO");
        small_unique_ptr p;
        p.ptr_ = ::new(static_cast<void*>(&p.buffer_)) U(
            std::forward<Args>(args)...);
        p.destroy_ = &destroy_small<U>;
        p.relocate_ = &relocate_small<U>;
        return p;
    }

    /** Construct a managed object in-place (heap path).

        Creates an object of type `U` on the heap when
        `U` does not fit in the buffer or does not meet
        SBO requirements.

        @param args Constructor arguments for `U`.

        @tparam U The concrete type to construct.
        Must be convertible to `T*`.

        @tparam Args Constructor argument types.

        @return A small_unique_ptr managing the new object.

        @throws Any exception thrown by `U`'s constructor,
        or `std::bad_alloc` on allocation failure.
    */
    // Heap path
    template<class U, class... Args>
    static
    typename std::enable_if<
        !fits_in_buffer<U>::value &&
        std::is_convertible<U*, T*>::value,
        small_unique_ptr>::type
    emplace(Args&&... args)
    {
        small_unique_ptr p;
        p.ptr_ = new U(std::forward<Args>(args)...);
        p.destroy_ = &destroy_large<U>;
        p.relocate_ = &relocate_large<U>;
        return p;
    }
};

/** Swap two small_unique_ptr objects.

    Exchanges the managed objects between `lhs` and `rhs`.

    @param lhs The first smart pointer.
    @param rhs The second smart pointer.

    @see small_unique_ptr::swap
*/
template<class T, std::size_t N>
void swap(
    small_unique_ptr<T, N>& lhs,
    small_unique_ptr<T, N>& rhs) noexcept
{
    lhs.swap(rhs);
}

/** Create a small_unique_ptr with in-place construction.

    Constructs an object of type `U` managed by a
    small_unique_ptr. Uses small buffer optimization
    if `U` fits within `N` bytes and is nothrow move
    constructible; otherwise heap-allocates.

    @param args Constructor arguments forwarded to `U`.

    @tparam T The base type for the smart pointer.

    @tparam N The buffer size in bytes.

    @tparam U The concrete type to construct.
    Must be convertible to `T*`.

    @tparam Args Constructor argument types.

    @return A small_unique_ptr<T, N> managing the new object.

    @throws Any exception thrown by `U`'s constructor.
    For heap allocation, may also throw `std::bad_alloc`.

    @par Example
    @code
    struct Base { virtual ~Base() = default; };
    struct Derived : Base { int value; Derived(int v) : value(v) {} };

    auto p = make_small_unique<Base, 64, Derived>(42);
    @endcode

    @see small_unique_ptr, small_unique_ptr::emplace
*/
template<class T, std::size_t N, class U, class... Args>
typename std::enable_if<
    std::is_convertible<U*, T*>::value,
    small_unique_ptr<T, N>>::type
make_small_unique(Args&&... args)
{
    return small_unique_ptr<T, N>::template emplace<U>(
        std::forward<Args>(args)...);
}

} // capy
} // boost

#endif
