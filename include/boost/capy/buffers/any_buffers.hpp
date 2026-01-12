//
// Copyright (c) 2025 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_BUFFERS_ANY_BUFFERS_HPP
#define BOOST_CAPY_BUFFERS_ANY_BUFFERS_HPP

#include <boost/capy/buffers/detail/config.hpp>
#include <boost/capy/buffers/buffer.hpp>
#include <boost/core/null_deleter.hpp>
#include <boost/assert.hpp>
#include <cstddef>
#include <memory>
#include <new>
#include <type_traits>

namespace boost {
namespace capy {
namespace buffers {

/** A type-erased buffer sequence.

    This class template wraps any buffer sequence and
    exposes it through a uniform interface, hiding the
    concrete type. Iteration is performed via a type-erased
    bidirectional iterator.

    The implementation uses small buffer optimization (SBO)
    for iterators that are small, trivially aligned, and
    nothrow copy constructible. Larger iterators fall back
    to an index-based traversal strategy.

    @tparam IsConst If `true`, the sequence yields
    @ref const_buffer elements. If `false`, it yields
    @ref mutable_buffer elements.

    @see any_const_buffers, any_mutable_buffers
*/
template<bool IsConst>
class any_buffers
{
public:
    /** The buffer type returned when dereferencing iterators.

        This is @ref const_buffer when `IsConst` is `true`,
        otherwise @ref mutable_buffer.
    */
    using value_type = std::conditional_t<
        IsConst, const_buffer, mutable_buffer>;

    /** A bidirectional iterator over the buffer sequence.

        @see begin, end
    */
    class const_iterator;

    /** Destructor.
    */
    ~any_buffers() = default;

    /** Constructor.
        Default-constructed objects are empty with zero length.
    */
    BOOST_CAPY_DECL
    any_buffers() noexcept;

    /** Constructor.
    */
    any_buffers(
        any_buffers const& other) noexcept
    {
        other.sp_->copy(*this, other.sp_);
    }

    /** Assignment.
    */
    any_buffers&
    operator=(
        any_buffers const& other) noexcept
    {
        if(this == &other)
            return *this;
        other.sp_->copy(*this, other.sp_);
        return *this;
    }

    /** Constructor.

        The type-erased buffer sequence is constructed
        from the specified buffer sequence, which must satisfy
        `const_buffer_sequence`. If `IsConst` is `false`, must
        also satisfy `mutable_buffer_sequence`.

        @param buffers The buffer sequence to type-erase.
    */
    template<class BufferSequence>
        requires (!std::same_as<any_buffers, std::decay_t<BufferSequence>> &&
                  const_buffer_sequence<std::decay_t<BufferSequence>> &&
                  (IsConst || mutable_buffer_sequence<std::decay_t<BufferSequence>>))
    any_buffers(
        BufferSequence&& buffers)
    {
        using T = std::decay_t<BufferSequence>;
        construct(std::forward<BufferSequence>(buffers),
            std::bool_constant<(sizeof(impl<T>) <= sbo_size)>{});
    }

    /** Return an iterator to the beginning.

        @return An iterator pointing to the first buffer,
        or `end()` if the sequence is empty.
    */
    const_iterator begin() const noexcept;

    /** Return an iterator to the end.

        @return An iterator pointing one past the last buffer.
    */
    const_iterator end() const noexcept;

private:
    friend struct any_buffers_test;

    static constexpr std::size_t sbo_size = 6 * sizeof(void*);

    static constexpr std::size_t iter_sbo_size = 4 * sizeof(void*);

    struct BOOST_SYMBOL_VISIBLE
        any_impl
    {
        virtual ~any_impl() = default;
        virtual bool is_small_buffers() const noexcept = 0;
        virtual bool is_small_iter() const noexcept = 0;
        virtual void copy(any_buffers& dest,
            std::shared_ptr<any_impl const> const&) const = 0;
        virtual void it_copy(void*, void const*) const = 0;
        virtual void it_destroy(void*) const = 0;
        virtual void inc(void*) const = 0;
        virtual void dec(void*) const = 0;
        virtual auto deref(void const*) const -> value_type = 0;
        virtual bool equal(void const*, void const*) const = 0;
        virtual void begin(void*) const = 0;
        virtual void end(void*) const = 0;
    };

    template<class T, bool IsIterSmall = (sizeof(decltype(
        buffers::begin(std::declval<T const>()))) <= iter_sbo_size)>
    struct impl;

    // small buffer sequence
    template<class T>
    void construct(T&& t, std::true_type)
    {
        using U = std::decay_t<T>;
        sp_ = {
            ::new(&storage_) impl<U>(std::forward<T>(t)),
            null_deleter{} };
    }

    template<class T>
    void construct(T&& t, std::false_type)
    {
        using U = std::decay_t<T>;
        sp_ = std::make_shared<impl<U>>(std::forward<T>(t));
    }

    bool is_small_buffers() const noexcept
    {
        return sp_->is_small_buffers();
    }

    bool is_small_iter() const noexcept
    {
        return sp_->is_small_iter();
    }

    alignas(std::max_align_t)
        unsigned char mutable storage_[sbo_size] = {};
    std::shared_ptr<any_impl const> sp_;
};

//-----------------------------------------------

/** Alias for a type-erased const buffer sequence.

    Equivalent to `any_buffers<true>`.

    @see any_buffers, any_mutable_buffers
*/
using any_const_buffers = any_buffers<true>;

/** Alias for a type-erased mutable buffer sequence.

    Equivalent to `any_buffers<false>`.

    @see any_buffers, any_const_buffers
*/
using any_mutable_buffers = any_buffers<false>;

//-----------------------------------------------

// small iterator
template<bool IsConst>
template<class T, bool>
struct any_buffers<IsConst>::
    impl : any_impl
{
    using iter_t = decltype(buffers::begin(
        std::declval<T const&>()));

    template<class T_>
    explicit impl(T_&& t) noexcept
        : t_(std::forward<T_>(t))
    {
    }

    bool is_small_buffers() const noexcept override
    {
        return sizeof(*this) <= sbo_size;
    }

    bool is_small_iter() const noexcept override
    {
        return true;
    }

    void copy(any_buffers& dest, std::shared_ptr<
        any_buffers<IsConst>::any_impl const> const& sp) const override
    {
        copy(dest, sp, std::bool_constant<
            sizeof(*this) <= sbo_size>{});
    }

    void copy(
        any_buffers& dest, std::shared_ptr<
            any_buffers<IsConst>::any_impl const> const&,
        std::true_type) const // small buffers
    {
        dest.sp_ = std::shared_ptr<impl<T>>(
            ::new(&dest.storage_) impl<T>(t_),
            null_deleter{} );
    }

    void copy(
        any_buffers& dest, std::shared_ptr<
            any_buffers<IsConst>::any_impl const> const&,
        std::false_type) const
    {
        dest.sp_ = std::make_shared<impl<T>>(t_);
    }

    void it_copy(void* dest, void const* src) const override
    {
        ::new(dest) iter_t(*static_cast<iter_t const*>(src));
    }

    void it_destroy(void* p) const override
    {
        static_cast<iter_t*>(p)->~iter_t();
    }

    void inc(void* p) const override
    {
        ++(*static_cast<iter_t*>(p));
    }

    void dec(void* p) const  override
    {
        --(*static_cast<iter_t*>(p));
    }

    value_type deref(void const* p) const override
    {
        return *(*static_cast<iter_t const*>(p));
    }

    bool equal(void const* it0, void const* it1) const override
    {
        return  *static_cast<iter_t const*>(it0) ==
                *static_cast<iter_t const*>(it1);
    }

    void begin(void* p) const override
    {
        ::new(p) iter_t(buffers::begin(t_));
    }

    void end(void* p) const override
    {
        ::new(p) iter_t(buffers::end(t_));
    }

private:
    T t_;
};

template<bool IsConst>
template<class T>
struct any_buffers<IsConst>::
    impl<T, false> : any_impl
{
    struct iter_t
    {
        std::size_t i;
    };

    template<class T_>
    explicit impl(T_&& t) noexcept
        : t_(std::forward<T_>(t))
        , len_(length(t_))
    {
    }

    bool is_small_buffers() const noexcept override
    {
        return sizeof(*this) <= any_buffers<IsConst>::sbo_size;
    }

    bool is_small_iter() const noexcept override
    {
        return false;
    }

    void copy(
        any_buffers<IsConst>& dest, std::shared_ptr<
            any_buffers<IsConst>::any_impl const> const& sp) const override
    {
        copy(dest, sp, std::bool_constant<
            sizeof(*this) <= any_buffers<IsConst>::sbo_size>{});
    }

    void copy(
        any_buffers<IsConst>& dest, std::shared_ptr<
            any_buffers<IsConst>::any_impl const> const&,
        std::true_type) const // small buffers
    {
        dest.sp_ = std::shared_ptr<impl<T>>(
            ::new(&dest.storage_) impl<T>(t_),
            null_deleter{});
    }

    void copy(
        any_buffers<IsConst>& dest, std::shared_ptr<
            any_buffers<IsConst>::any_impl const> const&,
        std::false_type) const
    {
        dest.sp_ = std::make_shared<impl<T>>(t_);
    }

    void it_copy(void* dest, void const* src) const override
    {
        ::new(dest) iter_t(*static_cast<iter_t const*>(src));
    }

    void it_destroy(void* p) const override
    {
        static_cast<iter_t*>(p)->~iter_t();
    }

    void inc(void* p) const override
    {
        ++static_cast<iter_t*>(p)->i;
    }

    void dec(void* p) const  override
    {
        --static_cast<iter_t*>(p)->i;
    }

    typename any_buffers<IsConst>::value_type
    deref(void const* p) const override
    {
        auto const& it_ = *static_cast<iter_t const*>(p);
        auto it = buffers::begin(t_);
        for(auto i = it_.i; i; --i)
            ++it;
        return *it;
    }

    bool equal(void const* it0, void const* it1) const override
    {
        return  static_cast<iter_t const*>(it0)->i ==
                static_cast<iter_t const*>(it1)->i;
    }

    void begin(void* p) const override
    {
        ::new(p) iter_t{ 0 };
    }

    void end(void* p) const override
    {
        ::new(p) iter_t{ len_ };
    }

private:
    T t_;
    std::size_t len_;
};

//-----------------------------------------------

/** A bidirectional iterator for @ref any_buffers.

    This iterator provides type-erased access to the
    underlying buffer sequence elements. It models
    `BidirectionalIterator` and returns buffer objects
    by value.
*/
template<bool IsConst>
class any_buffers<IsConst>::
    const_iterator
{
public:
    /** The buffer type returned by dereferencing.
    */
    using value_type = typename
        any_buffers<IsConst>::value_type;

    /** The type returned by `operator*`.

        Buffers are returned by value.
    */
    using reference = value_type;

    /** Pointer type (void, not used).
    */
    using pointer = void;

    /** Signed integer type for iterator differences.
    */
    using difference_type = std::ptrdiff_t;

    /** Iterator category tag.
    */
    using iterator_category =
        std::bidirectional_iterator_tag;

    /** Iterator concept tag (C++20).
    */
    using iterator_concept = std::bidirectional_iterator_tag;

    /** Destructor.

        Destroys the type-erased iterator state.
    */
    ~const_iterator()
    {
        sp_->it_destroy(&storage_);
    }

    /** Default constructor.

        Constructs a singular iterator. A default-constructed
        iterator may only be assigned to or destroyed.
    */
    const_iterator() noexcept;

    /** Copy constructor.

        @param other The iterator to copy.
    */
    const_iterator(
        const_iterator const& other) noexcept
        : sp_(other.sp_)
    {
        sp_->it_copy(&storage_, &other.storage_);
    }

    /** Copy assignment.

        @param other The iterator to copy.
        @return `*this`
    */
    const_iterator& operator=(
        const_iterator const& other) noexcept
    {
        if(this == &other)
            return *this;
        sp_->it_destroy(&storage_);
        sp_ = other.sp_;
        sp_->it_copy(&storage_, &other.storage_);
        return *this;
    }

    /** Test for equality.

        @param other The iterator to compare.
        @return `true` if both iterators point to the
        same element of the same sequence.
    */
    bool
    operator==(
        const_iterator const& other) const noexcept
    {
        if(sp_ != other.sp_)
            return false;
        return sp_->equal(&storage_, &other.storage_);
    }

    /** Test for inequality.

        @param other The iterator to compare.
        @return `true` if the iterators point to
        different elements or different sequences.
    */
    bool
    operator!=(
        const_iterator const& other) const noexcept
    {
        return !(*this == other);
    }

    /** Dereference the iterator.

        @return The buffer at the current position.

        @pre The iterator is dereferenceable
        (not default-constructed or past-the-end).
    */
    reference
    operator*() const noexcept
    {
        return sp_->deref(&storage_);
    }

    /** Pre-increment.

        Advances the iterator to the next buffer.

        @return `*this`

        @pre The iterator is incrementable.
    */
    const_iterator&
    operator++() noexcept
    {
        sp_->inc(&storage_);
        return *this;
    }

    /** Post-increment.

        Advances the iterator to the next buffer.

        @return A copy of the iterator before incrementing.

        @pre The iterator is incrementable.
    */
    const_iterator
    operator++(int) noexcept
    {
        auto temp = *this;
        ++(*this);
        return temp;
    }

    /** Pre-decrement.

        Moves the iterator to the previous buffer.

        @return `*this`

        @pre The iterator is decrementable.
    */
    const_iterator&
    operator--() noexcept
    {
        sp_->dec(&storage_);
        return *this;
    }

    /** Post-decrement.

        Moves the iterator to the previous buffer.

        @return A copy of the iterator before decrementing.

        @pre The iterator is decrementable.
    */
    const_iterator
    operator--(int) noexcept
    {
        auto temp = *this;
        --(*this);
        return temp;
    }

private:
    friend class any_buffers;

    struct begin_tag {};
    struct end_tag {};

    const_iterator(begin_tag, std::shared_ptr<
        any_impl const> const& sp) noexcept
        : sp_(sp)
    {
        sp_->begin(&storage_);
    }

    const_iterator(end_tag, std::shared_ptr<
        any_impl const> const& sp) noexcept
        : sp_(sp)
    {
        sp_->end(&storage_);
    }

    alignas(std::max_align_t)
        unsigned char mutable storage_[iter_sbo_size] = {};
    std::shared_ptr<any_buffers<IsConst>::any_impl const> sp_;
};

//-----------------------------------------------

template<>
BOOST_CAPY_DECL
any_buffers<true>::
any_buffers() noexcept;

template<>
BOOST_CAPY_DECL
any_buffers<false>::
any_buffers() noexcept;

template<>
BOOST_CAPY_DECL
any_buffers<true>::
const_iterator::
const_iterator() noexcept;

template<>
BOOST_CAPY_DECL
any_buffers<false>::
const_iterator::
const_iterator() noexcept;

//-----------------------------------------------

template<bool IsConst>
auto
any_buffers<IsConst>::
begin() const noexcept ->
    const_iterator
{
    return const_iterator(typename
        const_iterator::begin_tag{}, sp_);
}

template<bool IsConst>
auto
any_buffers<IsConst>::
end() const noexcept ->
    const_iterator
{
    return const_iterator(typename
        const_iterator::end_tag{}, sp_);
}

} // buffers
} // capy
} // boost

#endif
