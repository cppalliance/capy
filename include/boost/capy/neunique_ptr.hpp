//
// Copyright (c) 2025 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_NEUNIQUE_PTR_HPP
#define BOOST_CAPY_NEUNIQUE_PTR_HPP

#include <boost/capy/detail/config.hpp>
#include <cstddef>
#include <functional>
#include <memory>
#include <type_traits>
#include <utility>

namespace boost {
namespace capy {

namespace detail {

//----------------------------------------------------------

/** Storage wrapper applying empty base optimization.

    When `T` is empty and non-final, inherits from it to
    apply EBO. Otherwise stores as a member.
*/
#if __cplusplus >= 201402L
template<
    class T,
    bool = std::is_empty<T>::value && !std::is_final<T>::value>
#else
template<
    class T,
    bool = std::is_empty<T>::value>
#endif
struct ebo_storage
{
    T value_;

    ebo_storage() = default;

    explicit ebo_storage(T const& t)
        : value_(t)
    {
    }

    explicit ebo_storage(T&& t)
        : value_(std::move(t))
    {
    }

    T& get() noexcept { return value_; }
    T const& get() const noexcept { return value_; }
};

template<class T>
struct ebo_storage<T, true> : T
{
    ebo_storage() = default;

    explicit ebo_storage(T const& t)
        : T(t)
    {
    }

    explicit ebo_storage(T&& t)
        : T(std::move(t))
    {
    }

    T& get() noexcept { return *this; }
    T const& get() const noexcept { return *this; }
};

//----------------------------------------------------------

/** RAII scope guard for rollback on exception.
*/
template<class F>
class scope_guard
{
    F f_;
    bool active_ = true;

public:
    explicit scope_guard(F f) noexcept
        : f_(std::move(f))
    {
    }

    ~scope_guard()
    {
        if(active_)
            f_();
    }

    void release() noexcept
    {
        active_ = false;
    }

    scope_guard(scope_guard&& other) noexcept
        : f_(std::move(other.f_))
        , active_(other.active_)
    {
        other.active_ = false;
    }

    scope_guard(scope_guard const&) = delete;
    scope_guard& operator=(scope_guard const&) = delete;
    scope_guard& operator=(scope_guard&&) = delete;
};

template<class F>
scope_guard<F>
make_scope_guard(F f) noexcept
{
    return scope_guard<F>(std::move(f));
}

//----------------------------------------------------------

/** Base class for type-erased control blocks.

    The control block stores the deleter and allocator,
    enabling type erasure and incomplete type support.
*/
struct control_block_base
{
    virtual void destroy_and_deallocate() noexcept = 0;

protected:
    ~control_block_base() = default;
};

//----------------------------------------------------------

/** Control block storing pointer, deleter, and allocator.

    Used when constructing from a raw pointer with a
    custom deleter and/or allocator.
*/
template<class T, class D, class A>
struct control_block_pda final
    : control_block_base
    , private ebo_storage<
        typename std::allocator_traits<A>::
            template rebind_alloc<control_block_pda<T, D, A>>>
{
    using alloc_type = typename std::allocator_traits<A>::
        template rebind_alloc<control_block_pda>;
    using alloc_traits = std::allocator_traits<alloc_type>;
    using alloc_storage = ebo_storage<alloc_type>;

    T* ptr;
    D deleter;

    alloc_type& get_alloc() noexcept
    {
        return alloc_storage::get();
    }

    control_block_pda(T* p, D d, A const& a)
        : alloc_storage(alloc_type(a))
        , ptr(p)
        , deleter(std::move(d))
    {
    }

    void destroy_and_deallocate() noexcept override
    {
        T* p = ptr;
        D del = std::move(deleter);
        alloc_type a = std::move(get_alloc());
        this->~control_block_pda();
        alloc_traits::deallocate(a, this, 1);
        if(p)
            del(p);
    }
};

//----------------------------------------------------------

/** Control block with embedded object storage.

    Used by allocate_neunique to store the object
    inline with the control block.
*/
template<class T, class A>
struct control_block_embedded final
    : control_block_base
    , private ebo_storage<
        typename std::allocator_traits<A>::
            template rebind_alloc<control_block_embedded<T, A>>>
{
    using alloc_type = typename std::allocator_traits<A>::
        template rebind_alloc<control_block_embedded>;
    using alloc_traits = std::allocator_traits<alloc_type>;
    using alloc_storage = ebo_storage<alloc_type>;

    alignas(T) unsigned char storage[sizeof(T)];

    alloc_type& get_alloc() noexcept
    {
        return alloc_storage::get();
    }

    T* get() noexcept
    {
        return reinterpret_cast<T*>(storage);
    }

    template<class... Args>
    explicit control_block_embedded(A const& a, Args&&... args)
        : alloc_storage(alloc_type(a))
    {
        ::new(static_cast<void*>(storage)) T(
            std::forward<Args>(args)...);
    }

    void destroy_and_deallocate() noexcept override
    {
        get()->~T();
        alloc_type a = std::move(get_alloc());
        this->~control_block_embedded();
        alloc_traits::deallocate(a, this, 1);
    }
};

//----------------------------------------------------------

/** Control block for arrays with embedded storage.

    Used by allocate_neunique for array types.
*/
template<class T, class A>
struct control_block_array final
    : control_block_base
    , private ebo_storage<
        typename std::allocator_traits<A>::
            template rebind_alloc<control_block_array<T, A>>>
{
    using alloc_type = typename std::allocator_traits<A>::
        template rebind_alloc<control_block_array>;
    using alloc_traits = std::allocator_traits<alloc_type>;
    using alloc_storage = ebo_storage<alloc_type>;

    std::size_t size;
    // Flexible array member follows

    alloc_type& get_alloc() noexcept
    {
        return alloc_storage::get();
    }

    T* get() noexcept
    {
        return reinterpret_cast<T*>(
            reinterpret_cast<unsigned char*>(this) +
            sizeof(control_block_array));
    }

    static std::size_t storage_size(std::size_t n) noexcept
    {
        return sizeof(control_block_array) + sizeof(T) * n;
    }

    void destroy_and_deallocate() noexcept override
    {
        T* p = get();
        for(std::size_t i = size; i > 0; --i)
            p[i - 1].~T();
        alloc_type a = std::move(get_alloc());
        std::size_t sz = size;
        this->~control_block_array();
        std::size_t units = (storage_size(sz) +
            sizeof(control_block_array) - 1) /
            sizeof(control_block_array);
        alloc_traits::deallocate(a,
            reinterpret_cast<control_block_array*>(this), units);
    }
};

} // detail

//----------------------------------------------------------

template<class T>
class neunique_ptr;

template<class T, class A, class... Args>
typename std::enable_if<
    !std::is_array<T>::value,
    neunique_ptr<T>>::type
allocate_neunique(A const& a, Args&&... args);

template<class T, class A>
typename std::enable_if<
    std::is_array<T>::value && std::extent<T>::value == 0,
    neunique_ptr<T>>::type
allocate_neunique(A const& a, std::size_t n);

//----------------------------------------------------------

/** A smart pointer with unique ownership, type-erased deleter,
    and allocator support.

    This class provides unique ownership semantics similar to
    `std::unique_ptr`, combined with features from `std::shared_ptr`:

    @li Type-erased deleters - pointers with different deleters
        share the same static type
    @li Allocator support - custom allocators for the control block
    @li Incomplete types - the destructor is captured at construction
    @li Aliasing - the stored pointer can differ from the owned pointer

    The implementation uses a control block to store the deleter
    and allocator, similar to `std::shared_ptr` but without
    reference counting.

    @par Control Block Elision

    When constructed from a raw pointer without a custom deleter
    or allocator, no control block is allocated. The pointer is
    deleted directly using `delete`. This optimization requires
    the type to be complete at destruction time.

    @par Size

    `sizeof(neunique_ptr<T>)` is two pointers (16 bytes on 64-bit).

    @par Thread Safety

    Distinct `neunique_ptr` objects may be accessed concurrently.
    A single `neunique_ptr` object may not be accessed concurrently
    from multiple threads.

    @tparam T The element type. May be incomplete at declaration.
        For arrays, use `neunique_ptr<T[]>`.

    @see make_neunique, allocate_neunique
*/
template<class T>
class neunique_ptr
{
    template<class U> friend class neunique_ptr;

    template<class U, class A, class... Args>
    friend typename std::enable_if<
        !std::is_array<U>::value,
        neunique_ptr<U>>::type
    allocate_neunique(A const& a, Args&&... args);

    using control_block = detail::control_block_base;

    T* ptr_ = nullptr;
    control_block* cb_ = nullptr;

    template<class D, class A>
    void init_pda(T* p, D d, A const& a)
    {
        using cb_type = detail::control_block_pda<T, D, A>;
        using alloc_type = typename cb_type::alloc_type;
        using alloc_traits = std::allocator_traits<alloc_type>;

        alloc_type alloc(a);
        cb_type* cb = alloc_traits::allocate(alloc, 1);
        auto guard = detail::make_scope_guard(
            [&]{ alloc_traits::deallocate(alloc, cb, 1); });
        ::new(static_cast<void*>(cb)) cb_type(p, std::move(d), a);
        guard.release();
        ptr_ = p;
        cb_ = cb;
    }

public:
    /** The pointer type.
    */
    using pointer = T*;

    /** The element type.
    */
    using element_type = T;

    //------------------------------------------------------
    //
    // Constructors
    //
    //------------------------------------------------------

    /** Construct an empty pointer.

        @post `get() == nullptr`
    */
    constexpr neunique_ptr() noexcept = default;

    /** Construct an empty pointer from nullptr.

        @post `get() == nullptr`
    */
    constexpr neunique_ptr(std::nullptr_t) noexcept {}

    /** Construct from a raw pointer.

        Takes ownership of `p` using `delete`. No control
        block is allocated. The type must be complete at
        destruction time.

        @param p Pointer to take ownership of, or nullptr.

        @post `get() == p`
    */
    explicit neunique_ptr(pointer p) noexcept
        : ptr_(p)
    {
    }

    /** Construct from a raw pointer with custom deleter.

        Takes ownership of `p` using the specified deleter
        and the default allocator.

        @param p Pointer to take ownership of, or nullptr.
        @param d Deleter callable as `d(p)`.

        @throws std::bad_alloc if control block allocation fails.
            If an exception is thrown, `d(p)` is called.

        @post `get() == p`
    */
    template<class D>
    neunique_ptr(pointer p, D d)
        : neunique_ptr(p, std::move(d), std::allocator<T>{})
    {
    }

    /** Construct from a raw pointer with custom deleter and allocator.

        Takes ownership of `p` using the specified deleter.
        The allocator is used to allocate the control block.

        @param p Pointer to take ownership of, or nullptr.
        @param d Deleter callable as `d(p)`.
        @param a Allocator for control block allocation.

        @throws Any exception thrown by control block allocation.
            If an exception is thrown, `d(p)` is called.

        @post `get() == p`
    */
    template<class D, class A>
    neunique_ptr(pointer p, D d, A const& a)
    {
        if(!p)
            return;
        auto guard = detail::make_scope_guard(
            [&]{ d(p); });
        init_pda(p, std::move(d), a);
        guard.release();
    }

    /** Aliasing constructor.

        Constructs a `neunique_ptr` that stores `p` but shares
        ownership with `other`. After construction, `get() == p`
        and `other` is empty.

        This allows a `neunique_ptr` to point to a subobject
        of the owned object.

        @note If `other` has no control block (was constructed
            from a raw pointer without a deleter), the behavior
            is undefined unless `p` equals `other.get()`.

        @param other Pointer to transfer ownership from.
        @param p Pointer to store (typically to a subobject
            of the object owned by `other`).

        @post `get() == p`
        @post `other.get() == nullptr`
    */
    template<class U>
    neunique_ptr(neunique_ptr<U>&& other, pointer p) noexcept
        : ptr_(p)
        , cb_(other.cb_)
    {
        other.ptr_ = nullptr;
        other.cb_ = nullptr;
    }

    /** Move constructor.

        Takes ownership from `other`. After construction,
        `other` is empty.

        @param other Pointer to move from.

        @post `other.get() == nullptr`
    */
    neunique_ptr(neunique_ptr&& other) noexcept
        : ptr_(other.ptr_)
        , cb_(other.cb_)
    {
        other.ptr_ = nullptr;
        other.cb_ = nullptr;
    }

    /** Converting move constructor.

        Takes ownership from `other`. After construction,
        `other` is empty. Participates in overload resolution
        only if `U*` is convertible to `T*`.

        @param other Pointer to move from.

        @post `other.get() == nullptr`
    */
    template<class U, class = typename std::enable_if<
        std::is_convertible<U*, T*>::value>::type>
    neunique_ptr(neunique_ptr<U>&& other) noexcept
        : ptr_(other.ptr_)
        , cb_(other.cb_)
    {
        other.ptr_ = nullptr;
        other.cb_ = nullptr;
    }

    neunique_ptr(neunique_ptr const&) = delete;
    neunique_ptr& operator=(neunique_ptr const&) = delete;

    //------------------------------------------------------
    //
    // Destructor
    //
    //------------------------------------------------------

    /** Destructor.

        Destroys the owned object using the stored deleter
        and deallocates the control block using the stored
        allocator. If no control block exists, uses `delete`.
    */
    ~neunique_ptr()
    {
        if(cb_)
            cb_->destroy_and_deallocate();
        else if(ptr_)
            delete ptr_;
    }

    //------------------------------------------------------
    //
    // Assignment
    //
    //------------------------------------------------------

    /** Move assignment.

        Releases the currently owned object and takes
        ownership from `other`.

        @param other Pointer to move from.

        @return `*this`

        @post `other.get() == nullptr`
    */
    neunique_ptr& operator=(neunique_ptr&& other) noexcept
    {
        if(this != &other)
        {
            if(cb_)
                cb_->destroy_and_deallocate();
            else if(ptr_)
                delete ptr_;
            ptr_ = other.ptr_;
            cb_ = other.cb_;
            other.ptr_ = nullptr;
            other.cb_ = nullptr;
        }
        return *this;
    }

    /** Converting move assignment.

        Releases the currently owned object and takes
        ownership from `other`. Participates in overload
        resolution only if `U*` is convertible to `T*`.

        @param other Pointer to move from.

        @return `*this`

        @post `other.get() == nullptr`
    */
    template<class U, class = typename std::enable_if<
        std::is_convertible<U*, T*>::value>::type>
    neunique_ptr& operator=(neunique_ptr<U>&& other) noexcept
    {
        if(cb_)
            cb_->destroy_and_deallocate();
        else if(ptr_)
            delete ptr_;
        ptr_ = other.ptr_;
        cb_ = other.cb_;
        other.ptr_ = nullptr;
        other.cb_ = nullptr;
        return *this;
    }

    /** Assign nullptr.

        Releases the currently owned object.

        @return `*this`

        @post `get() == nullptr`
    */
    neunique_ptr& operator=(std::nullptr_t) noexcept
    {
        reset();
        return *this;
    }

    //------------------------------------------------------
    //
    // Modifiers
    //
    //------------------------------------------------------

    /** Replace the owned object.

        Releases the currently owned object and takes
        ownership of `p` using `delete`. No control block
        is allocated.

        @param p Pointer to take ownership of, or nullptr.

        @post `get() == p`
    */
    void reset(pointer p = nullptr) noexcept
    {
        if(cb_)
            cb_->destroy_and_deallocate();
        else if(ptr_)
            delete ptr_;
        ptr_ = p;
        cb_ = nullptr;
    }

    /** Replace the owned object with custom deleter.

        Releases the currently owned object and takes
        ownership of `p` using the specified deleter.

        @param p Pointer to take ownership of, or nullptr.
        @param d Deleter callable as `d(p)`.

        @throws Any exception thrown by control block allocation.

        @post `get() == p`
    */
    template<class D>
    void reset(pointer p, D d)
    {
        neunique_ptr(p, std::move(d)).swap(*this);
    }

    /** Replace the owned object with custom deleter and allocator.

        Releases the currently owned object and takes
        ownership of `p` using the specified deleter and
        allocator.

        @param p Pointer to take ownership of, or nullptr.
        @param d Deleter callable as `d(p)`.
        @param a Allocator for control block allocation.

        @throws Any exception thrown by control block allocation.

        @post `get() == p`
    */
    template<class D, class A>
    void reset(pointer p, D d, A const& a)
    {
        neunique_ptr(p, std::move(d), a).swap(*this);
    }

    /** Swap with another pointer.

        @param other Pointer to swap with.
    */
    void swap(neunique_ptr& other) noexcept
    {
        std::swap(ptr_, other.ptr_);
        std::swap(cb_, other.cb_);
    }

    //------------------------------------------------------
    //
    // Observers
    //
    //------------------------------------------------------

    /** Return the stored pointer.

        @return The stored pointer, or nullptr if empty.
    */
    pointer get() const noexcept
    {
        return ptr_;
    }

    /** Check if non-empty.

        @return `true` if `get() != nullptr`.
    */
    explicit operator bool() const noexcept
    {
        return ptr_ != nullptr;
    }

    /** Dereference the pointer.

        @pre `get() != nullptr`

        @return Reference to the pointed-to object.
    */
    typename std::add_lvalue_reference<T>::type
    operator*() const noexcept
    {
        return *ptr_;
    }

    /** Member access.

        @pre `get() != nullptr`

        @return The stored pointer.
    */
    pointer operator->() const noexcept
    {
        return ptr_;
    }
};

//----------------------------------------------------------

/** A smart pointer with unique ownership for arrays.

    Array specialization of @ref neunique_ptr. Provides
    `operator[]` instead of `operator*` and `operator->`.

    @tparam T The element type (without `[]`).

    @see neunique_ptr, make_neunique, allocate_neunique
*/
template<class T>
class neunique_ptr<T[]>
{
    template<class U> friend class neunique_ptr;

    template<class U, class A>
    friend typename std::enable_if<
        std::is_array<U>::value && std::extent<U>::value == 0,
        neunique_ptr<U>>::type
    allocate_neunique(A const& a, std::size_t n);

    using control_block = detail::control_block_base;

    T* ptr_ = nullptr;
    control_block* cb_ = nullptr;

    template<class D, class A>
    void init_pda(T* p, D d, A const& a)
    {
        using cb_type = detail::control_block_pda<T, D, A>;
        using alloc_type = typename cb_type::alloc_type;
        using alloc_traits = std::allocator_traits<alloc_type>;

        alloc_type alloc(a);
        cb_type* cb = alloc_traits::allocate(alloc, 1);
        auto guard = detail::make_scope_guard(
            [&]{ alloc_traits::deallocate(alloc, cb, 1); });
        ::new(static_cast<void*>(cb)) cb_type(p, std::move(d), a);
        guard.release();
        ptr_ = p;
        cb_ = cb;
    }

public:
    /** The pointer type.
    */
    using pointer = T*;

    /** The element type.
    */
    using element_type = T;

    //------------------------------------------------------
    //
    // Constructors
    //
    //------------------------------------------------------

    /** Construct an empty pointer.

        @post `get() == nullptr`
    */
    constexpr neunique_ptr() noexcept = default;

    /** Construct an empty pointer from nullptr.

        @post `get() == nullptr`
    */
    constexpr neunique_ptr(std::nullptr_t) noexcept {}

    /** Construct from a raw pointer.

        Takes ownership of `p` using `delete[]`. No control
        block is allocated. The type must be complete at
        destruction time.

        @param p Pointer to take ownership of, or nullptr.

        @post `get() == p`
    */
    template<class U, class = typename std::enable_if<
        std::is_same<U, T*>::value ||
        std::is_same<U, std::nullptr_t>::value>::type>
    explicit neunique_ptr(U p) noexcept
        : ptr_(p)
    {
    }

    /** Construct from a raw pointer with custom deleter.

        Takes ownership of `p` using the specified deleter
        and the default allocator.

        @param p Pointer to take ownership of, or nullptr.
        @param d Deleter callable as `d(p)`.

        @throws std::bad_alloc if control block allocation fails.
            If an exception is thrown, `d(p)` is called.

        @post `get() == p`
    */
    template<class D>
    neunique_ptr(pointer p, D d)
        : neunique_ptr(p, std::move(d), std::allocator<T>{})
    {
    }

    /** Construct from a raw pointer with custom deleter and allocator.

        Takes ownership of `p` using the specified deleter.
        The allocator is used to allocate the control block.

        @param p Pointer to take ownership of, or nullptr.
        @param d Deleter callable as `d(p)`.
        @param a Allocator for control block allocation.

        @throws Any exception thrown by control block allocation.
            If an exception is thrown, `d(p)` is called.

        @post `get() == p`
    */
    template<class D, class A>
    neunique_ptr(pointer p, D d, A const& a)
    {
        if(!p)
            return;
        auto guard = detail::make_scope_guard(
            [&]{ d(p); });
        init_pda(p, std::move(d), a);
        guard.release();
    }

    /** Aliasing constructor.

        Constructs a `neunique_ptr` that stores `p` but shares
        ownership with `other`. After construction, `get() == p`
        and `other` is empty.

        @note If `other` has no control block (was constructed
            from a raw pointer without a deleter), the behavior
            is undefined unless `p` equals `other.get()`.

        @param other Pointer to transfer ownership from.
        @param p Pointer to store.

        @post `get() == p`
        @post `other.get() == nullptr`
    */
    template<class U>
    neunique_ptr(neunique_ptr<U>&& other, pointer p) noexcept
        : ptr_(p)
        , cb_(other.cb_)
    {
        other.ptr_ = nullptr;
        other.cb_ = nullptr;
    }

    /** Move constructor.

        Takes ownership from `other`. After construction,
        `other` is empty.

        @param other Pointer to move from.

        @post `other.get() == nullptr`
    */
    neunique_ptr(neunique_ptr&& other) noexcept
        : ptr_(other.ptr_)
        , cb_(other.cb_)
    {
        other.ptr_ = nullptr;
        other.cb_ = nullptr;
    }

    neunique_ptr(neunique_ptr const&) = delete;
    neunique_ptr& operator=(neunique_ptr const&) = delete;

    //------------------------------------------------------
    //
    // Destructor
    //
    //------------------------------------------------------

    /** Destructor.

        Destroys the owned array using the stored deleter
        and deallocates the control block. If no control
        block exists, uses `delete[]`.
    */
    ~neunique_ptr()
    {
        if(cb_)
            cb_->destroy_and_deallocate();
        else if(ptr_)
            delete[] ptr_;
    }

    //------------------------------------------------------
    //
    // Assignment
    //
    //------------------------------------------------------

    /** Move assignment.

        Releases the currently owned array and takes
        ownership from `other`.

        @param other Pointer to move from.

        @return `*this`

        @post `other.get() == nullptr`
    */
    neunique_ptr& operator=(neunique_ptr&& other) noexcept
    {
        if(this != &other)
        {
            if(cb_)
                cb_->destroy_and_deallocate();
            else if(ptr_)
                delete[] ptr_;
            ptr_ = other.ptr_;
            cb_ = other.cb_;
            other.ptr_ = nullptr;
            other.cb_ = nullptr;
        }
        return *this;
    }

    /** Assign nullptr.

        Releases the currently owned array.

        @return `*this`

        @post `get() == nullptr`
    */
    neunique_ptr& operator=(std::nullptr_t) noexcept
    {
        reset();
        return *this;
    }

    //------------------------------------------------------
    //
    // Modifiers
    //
    //------------------------------------------------------

    /** Replace the owned array.

        Releases the currently owned array and takes
        ownership of `p` using `delete[]`. No control
        block is allocated.

        @param p Pointer to take ownership of, or nullptr.

        @post `get() == p`
    */
    template<class U, class = typename std::enable_if<
        std::is_same<U, T*>::value ||
        std::is_same<U, std::nullptr_t>::value>::type>
    void reset(U p = nullptr) noexcept
    {
        if(cb_)
            cb_->destroy_and_deallocate();
        else if(ptr_)
            delete[] ptr_;
        ptr_ = p;
        cb_ = nullptr;
    }

    /** Replace the owned array with custom deleter.

        Releases the currently owned array and takes
        ownership of `p` using the specified deleter.

        @param p Pointer to take ownership of, or nullptr.
        @param d Deleter callable as `d(p)`.

        @throws Any exception thrown by control block allocation.

        @post `get() == p`
    */
    template<class D>
    void reset(pointer p, D d)
    {
        neunique_ptr(p, std::move(d)).swap(*this);
    }

    /** Replace the owned array with custom deleter and allocator.

        Releases the currently owned array and takes
        ownership of `p` using the specified deleter.

        @param p Pointer to take ownership of, or nullptr.
        @param d Deleter callable as `d(p)`.
        @param a Allocator for control block allocation.

        @throws Any exception thrown by control block allocation.

        @post `get() == p`
    */
    template<class D, class A>
    void reset(pointer p, D d, A const& a)
    {
        neunique_ptr(p, std::move(d), a).swap(*this);
    }

    /** Swap with another pointer.

        @param other Pointer to swap with.
    */
    void swap(neunique_ptr& other) noexcept
    {
        std::swap(ptr_, other.ptr_);
        std::swap(cb_, other.cb_);
    }

    //------------------------------------------------------
    //
    // Observers
    //
    //------------------------------------------------------

    /** Return the stored pointer.

        @return The stored pointer, or nullptr if empty.
    */
    pointer get() const noexcept
    {
        return ptr_;
    }

    /** Check if non-empty.

        @return `true` if `get() != nullptr`.
    */
    explicit operator bool() const noexcept
    {
        return ptr_ != nullptr;
    }

    /** Access array element.

        @param i Index of element to access.

        @pre `get() != nullptr`
        @pre `i` is within bounds.

        @return Reference to element at index `i`.
    */
    T& operator[](std::size_t i) const noexcept
    {
        return ptr_[i];
    }
};

//----------------------------------------------------------
//
// Free functions
//
//----------------------------------------------------------

/** Create a neunique_ptr for a single object.

    Allocates and constructs an object of type `T` using
    `new`. No control block is allocated.

    @param args Arguments forwarded to `T`'s constructor.

    @return A `neunique_ptr<T>` owning the new object.

    @throws std::bad_alloc if allocation fails.
    @throws Any exception thrown by `T`'s constructor.
*/
template<class T, class... Args>
typename std::enable_if<
    !std::is_array<T>::value,
    neunique_ptr<T>>::type
make_neunique(Args&&... args)
{
    return neunique_ptr<T>(new T(std::forward<Args>(args)...));
}

/** Create a neunique_ptr for an array.

    Allocates an array of `n` value-initialized elements
    using `new[]`. No control block is allocated.

    @param n Number of elements.

    @return A `neunique_ptr<T[]>` owning the new array.

    @throws std::bad_alloc if allocation fails.
*/
template<class T>
typename std::enable_if<
    std::is_array<T>::value && std::extent<T>::value == 0,
    neunique_ptr<T>>::type
make_neunique(std::size_t n)
{
    using U = typename std::remove_extent<T>::type;
    return neunique_ptr<T>(new U[n]());
}

template<class T, class... Args>
typename std::enable_if<
    std::extent<T>::value != 0>::type
make_neunique(Args&&...) = delete;

//----------------------------------------------------------

/** Create a neunique_ptr using a custom allocator.

    Allocates and constructs an object of type `T` using
    the specified allocator. The object and control block
    are allocated together for efficiency.

    @param a Allocator to use.
    @param args Arguments forwarded to `T`'s constructor.

    @return A `neunique_ptr<T>` owning the new object.

    @throws Any exception thrown by allocation or construction.
*/
template<class T, class A, class... Args>
typename std::enable_if<
    !std::is_array<T>::value,
    neunique_ptr<T>>::type
allocate_neunique(A const& a, Args&&... args)
{
    using cb_type = detail::control_block_embedded<T, A>;
    using alloc_type = typename cb_type::alloc_type;
    using alloc_traits = std::allocator_traits<alloc_type>;

    alloc_type alloc(a);
    cb_type* cb = alloc_traits::allocate(alloc, 1);
    auto guard = detail::make_scope_guard(
        [&]{ alloc_traits::deallocate(alloc, cb, 1); });
    ::new(static_cast<void*>(cb)) cb_type(
        a, std::forward<Args>(args)...);
    guard.release();

    neunique_ptr<T> result;
    result.ptr_ = cb->get();
    result.cb_ = cb;
    return result;
}

/** Create a neunique_ptr for an array using a custom allocator.

    Allocates an array of `n` value-initialized elements
    using the specified allocator.

    @param a Allocator to use.
    @param n Number of elements.

    @return A `neunique_ptr<T[]>` owning the new array.

    @throws Any exception thrown by allocation or construction.
*/
template<class T, class A>
typename std::enable_if<
    std::is_array<T>::value && std::extent<T>::value == 0,
    neunique_ptr<T>>::type
allocate_neunique(A const& a, std::size_t n)
{
    using U = typename std::remove_extent<T>::type;
    using cb_type = detail::control_block_array<U, A>;
    using alloc_type = typename cb_type::alloc_type;
    using alloc_traits = std::allocator_traits<alloc_type>;

    alloc_type alloc(a);
    std::size_t units = (cb_type::storage_size(n) +
        sizeof(cb_type) - 1) / sizeof(cb_type);
    cb_type* cb = alloc_traits::allocate(alloc, units);

    auto guard = detail::make_scope_guard(
        [&]{ alloc_traits::deallocate(alloc, cb, units); });

    ::new(static_cast<void*>(cb)) cb_type();
    cb->get_alloc() = alloc;
    cb->size = 0;

    U* arr = cb->get();
    for(std::size_t i = 0; i < n; ++i)
    {
        ::new(static_cast<void*>(arr + i)) U();
        ++cb->size;
    }
    guard.release();

    neunique_ptr<T> result;
    result.ptr_ = arr;
    result.cb_ = cb;
    return result;
}

//----------------------------------------------------------

/** Swap two pointers.

    @param a First pointer.
    @param b Second pointer.
*/
template<class T>
void swap(neunique_ptr<T>& a, neunique_ptr<T>& b) noexcept
{
    a.swap(b);
}

//----------------------------------------------------------
//
// Comparison operators
//
//----------------------------------------------------------

/** Compare for equality.

    @return `true` if both store the same pointer.
*/
template<class T, class U>
bool operator==(
    neunique_ptr<T> const& a,
    neunique_ptr<U> const& b) noexcept
{
    return a.get() == b.get();
}

/** Compare with nullptr.

    @return `true` if `a` is empty.
*/
template<class T>
bool operator==(
    neunique_ptr<T> const& a,
    std::nullptr_t) noexcept
{
    return !a;
}

/** Compare with nullptr.

    @return `true` if `a` is empty.
*/
template<class T>
bool operator==(
    std::nullptr_t,
    neunique_ptr<T> const& a) noexcept
{
    return !a;
}

/** Compare for inequality.

    @return `true` if pointers differ.
*/
template<class T, class U>
bool operator!=(
    neunique_ptr<T> const& a,
    neunique_ptr<U> const& b) noexcept
{
    return a.get() != b.get();
}

/** Compare with nullptr.

    @return `true` if `a` is non-empty.
*/
template<class T>
bool operator!=(
    neunique_ptr<T> const& a,
    std::nullptr_t) noexcept
{
    return static_cast<bool>(a);
}

/** Compare with nullptr.

    @return `true` if `a` is non-empty.
*/
template<class T>
bool operator!=(
    std::nullptr_t,
    neunique_ptr<T> const& a) noexcept
{
    return static_cast<bool>(a);
}

/** Less-than comparison.

    @return `true` if `a.get() < b.get()`.
*/
template<class T, class U>
bool operator<(
    neunique_ptr<T> const& a,
    neunique_ptr<U> const& b) noexcept
{
    using V = typename std::common_type<
        typename neunique_ptr<T>::pointer,
        typename neunique_ptr<U>::pointer>::type;
    return std::less<V>()(a.get(), b.get());
}

/** Less-than comparison with nullptr.

    @return `true` if `a.get() < nullptr`.
*/
template<class T>
bool operator<(
    neunique_ptr<T> const& a,
    std::nullptr_t) noexcept
{
    return std::less<typename neunique_ptr<T>::pointer>()(
        a.get(), nullptr);
}

/** Less-than comparison with nullptr.

    @return `true` if `nullptr < a.get()`.
*/
template<class T>
bool operator<(
    std::nullptr_t,
    neunique_ptr<T> const& a) noexcept
{
    return std::less<typename neunique_ptr<T>::pointer>()(
        nullptr, a.get());
}

/** Less-than-or-equal comparison.

    @return `true` if `a.get() <= b.get()`.
*/
template<class T, class U>
bool operator<=(
    neunique_ptr<T> const& a,
    neunique_ptr<U> const& b) noexcept
{
    return !(b < a);
}

/** Less-than-or-equal comparison with nullptr.

    @return `true` if `a.get() <= nullptr`.
*/
template<class T>
bool operator<=(
    neunique_ptr<T> const& a,
    std::nullptr_t) noexcept
{
    return !(nullptr < a);
}

/** Less-than-or-equal comparison with nullptr.

    @return `true` if `nullptr <= a.get()`.
*/
template<class T>
bool operator<=(
    std::nullptr_t,
    neunique_ptr<T> const& a) noexcept
{
    return !(a < nullptr);
}

/** Greater-than comparison.

    @return `true` if `a.get() > b.get()`.
*/
template<class T, class U>
bool operator>(
    neunique_ptr<T> const& a,
    neunique_ptr<U> const& b) noexcept
{
    return b < a;
}

/** Greater-than comparison with nullptr.

    @return `true` if `a.get() > nullptr`.
*/
template<class T>
bool operator>(
    neunique_ptr<T> const& a,
    std::nullptr_t) noexcept
{
    return nullptr < a;
}

/** Greater-than comparison with nullptr.

    @return `true` if `nullptr > a.get()`.
*/
template<class T>
bool operator>(
    std::nullptr_t,
    neunique_ptr<T> const& a) noexcept
{
    return a < nullptr;
}

/** Greater-than-or-equal comparison.

    @return `true` if `a.get() >= b.get()`.
*/
template<class T, class U>
bool operator>=(
    neunique_ptr<T> const& a,
    neunique_ptr<U> const& b) noexcept
{
    return !(a < b);
}

/** Greater-than-or-equal comparison with nullptr.

    @return `true` if `a.get() >= nullptr`.
*/
template<class T>
bool operator>=(
    neunique_ptr<T> const& a,
    std::nullptr_t) noexcept
{
    return !(a < nullptr);
}

/** Greater-than-or-equal comparison with nullptr.

    @return `true` if `nullptr >= a.get()`.
*/
template<class T>
bool operator>=(
    std::nullptr_t,
    neunique_ptr<T> const& a) noexcept
{
    return !(nullptr < a);
}

} // capy
} // boost

//----------------------------------------------------------
//
// Hash support
//
//----------------------------------------------------------

namespace std {

/** Hash support for neunique_ptr.

    Allows `neunique_ptr` to be used as a key in
    unordered containers.
*/
template<class T>
struct hash<::boost::capy::neunique_ptr<T>>
{
    /** Return hash value for a neunique_ptr.

        @param p Pointer to hash.

        @return Hash of the stored pointer.
    */
    std::size_t operator()(
        ::boost::capy::neunique_ptr<T> const& p) const noexcept
    {
        return std::hash<typename boost::capy::neunique_ptr<T>::pointer>()(p.get());
    }
};

} // std

#endif
