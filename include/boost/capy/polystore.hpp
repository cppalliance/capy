//
// Copyright (c) 2025 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_POLYSTORE_HPP
#define BOOST_CAPY_POLYSTORE_HPP

#include <boost/capy/detail/config.hpp>
#include <boost/capy/detail/call_traits.hpp>
#include <boost/capy/detail/except.hpp>
#include <boost/capy/detail/type_traits.hpp>
#include <boost/core/typeinfo.hpp>
#include <boost/core/detail/static_assert.hpp>
#include <cstring>
#include <memory>
#include <type_traits>
#include <unordered_map>
#include <vector>

#if ! defined( BOOST_NO_TYPEID )
#include <typeindex>
#endif

namespace boost {
namespace capy {

namespace detail {

#if defined( BOOST_NO_TYPEID )

struct typeindex
{
    typeindex(
        core::typeinfo const& ti) noexcept
        : n_(std::strlen(ti.name()))
        , ti_(&ti)
    { 
    }

    std::size_t hash_code() const noexcept
    {
        constexpr std::size_t offset_basis =
            (sizeof(std::size_t) == 8)
                ? 1469598103934665603ull
                : 2166136261u;
        constexpr std::size_t prime =
            (sizeof(std::size_t) == 8)
                ? 1099511628211ull
                : 16777619u;
        auto const s = ti_->name();
        std::size_t h = offset_basis;
        for(std::size_t i = 0; i < n_; ++i)
            h = (h ^ static_cast<unsigned char>(s[i])) * prime;
        return h;
    }

    bool operator==(typeindex const& other) const noexcept
    {
        return n_ == other.n_ && *ti_ == *other.ti_;
    }

private:
    std::size_t n_;
    core::typeinfo const* ti_;
};

} // detail
} // capy
} // boost
namespace std {
template<>
struct hash< boost::capy::detail::typeindex >
{
    std::size_t operator()(
        boost::capy::detail::typeindex const& t) const noexcept
    {
        return t.hash_code();
    }
};
} // std
namespace boost {
namespace capy {
namespace detail {

#else

using typeindex = std::type_index;

#endif

} // detail

/** A container of type-erased objects

    Objects are stored and retrieved by their type.
    Each type may be stored at most once. Types
    may specify a nested `key_type` to be used
    as the unique identifier instead of the type
    itself. In this case, a reference to the type
    must be convertible to a reference to the key type.

    @par Example
    @code
    struct A
    {
        int i = 1;
    };
    struct B
    {
        char c = '2';
    };
    struct C
    {
        double d;
    };
    struct D : C
    {
        using key_type = C;
        D()
        {
            d = 3.14;
        }
    };
    polystore ps;
    A& a = ps.emplace<A>();
    B& b = ps.insert(B{});
    C& c = ps.emplace<C>();
    assert(ps.get<A>().i == 1);
    assert(ps.get<B>().c == '2');
    assert(ps.get<C>().d == 3.14);
    invoke(ps, [](A& a){ a.i = 0; });
    invoke(ps, [](A const&, B& b){ b.c = 0; });
    assert(ps.get<A>().i == 0);
    assert(ps.get<B>().c == 0);
    @endcode
*/
class polystore
{
    template<class T, class = void>
    struct get_key : std::false_type
    {
    };

    template<class T>
    struct get_key<T, typename std::enable_if<
        ! std::is_same<T, typename T::key_type>::value>::type>
        : std::true_type
    {
        using type = typename T::key_type;
    };

public:
    /** Destructor

        All objects stored in the container are destroyed in
        the reverse order of construction.
    */
    BOOST_CAPY_DECL
    ~polystore();

    /** Constructor
        The moved-from container will be empty.
    */
    BOOST_CAPY_DECL
    polystore(polystore&& other) noexcept;

    /** Assignment operator
        The moved-from container will be empty.
        @return A reference to `*this`.
    */
    BOOST_CAPY_DECL
    polystore& operator=(polystore&& other) noexcept;

    /** Constructor
        The container is initially empty.
    */
    polystore() = default;

    /** Return a pointer to the object associated with type `T`, or `nullptr`

        If no object associated with `T` exists in the container,
        `nullptr` is returned.

        @par Thread Safety
        `const` member function calls are thread-safe.
        Calls to non-`const` member functions must not run concurrently
        with other member functions on the same object.

        @tparam T The type of object to find.
        @return A pointer to the associated object, or `nullptr` if none exists.
    */
    template<class T>
    T* find() const noexcept
    {
        return static_cast<T*>(find(BOOST_CORE_TYPEID(T)));
    }

    /** Assign the pointer for the object associated with `T`, or `nullptr`.
        
        If no object of type `T` is stored, @p t is set to `nullptr`.

        @par Thread Safety
        `const` member functions are thread-safe. Non-`const` functions
        must not run concurrently with any other member function on the
        same instance.

        @param t The pointer to assign.
        @return `true` if an object of type `T` is present, otherwise `false`.
    */
    template<class T>
    bool find(T*& t) const noexcept
    {
        t = find<T>();
        return t != nullptr;
    }

    /** Return a reference to the object associated with type T

        If no such object exists in the container, an exception is thrown.

        @par Exception Safety
        Strong guarantee.

        @par Thread Safety
        Calls to `const` member functions are thread-safe.  
        Calls to non-`const` member functions must not run concurrently
        with other member functions on the same object.

        @throws std::bad_typeid
        If no object associated with type `T` is present.
        @tparam T The type of object to retrieve.
        @return A reference to the associated object.
    */
    template<class T>
    T& get() const
    {
        if(auto t = find<T>())
            return *t;
        detail::throw_bad_typeid();
    }

    /** Construct and insert an anonymous object into the container

        A new object of type `T` is constructed in place using the provided
        arguments and inserted into the container without associating it
        with any key. A reference to the stored object is returned.

        @par Exception Safety
        Strong guarantee.

        @par Thread Safety
        Not thread-safe.

        @tparam T The type of object to construct and insert.
        @param args Arguments forwarded to the constructor of `T`.
        @return A reference to the inserted object.
    */
    template<class T, class... Args>
    T& emplace_anon(Args&&... args)
    {
        return *static_cast<T*>(insert_impl(
            make_any<T>(std::forward<Args>(args)...)));
    }

    /** Insert an anonymous object by moving or copying it into the container

        A new object of type `T` is inserted into the container without
        associating it with any key. The object is move-constructed or
        copy-constructed from the provided argument, and a reference to
        the stored object is returned.

        @par Exception Safety
        Strong guarantee.

        @par Thread Safety
        Not thread-safe.

        @tparam T The type of object to insert.
        @param t The object to insert.
        @return A reference to the inserted object.
    */
    template<class T>
    T& insert_anon(T&& t)
    {
        return emplace_anon<typename
            std::remove_cv<T>::type>(
                std::forward<T>(t));
    }

    /** Construct and insert an object with a nested key type

        A new object of type `T` is constructed in place using the provided
        arguments and inserted into the container. The type `T` must define
        a nested type `key_type`, which is used as the key for insertion.
        No additional key types may be specified. The type `T&` must be
        convertible to a reference to `key_type`.

        @par Constraints
        `T::key_type` must name a type.

        @par Exception Safety
        Strong guarantee.

        @par Thread Safety
        Not thread-safe.

        @throws std::invalid_argument On duplicate insertion.
        @tparam T The type of object to construct and insert.
        @param args Arguments forwarded to the constructor of `T`.
        @return A reference to the inserted object.
    */
    template<class T, class... Keys, class... Args>
    auto emplace(Args&&... args) ->
        typename std::enable_if<get_key<T>::value, T&>::type
    {
        // Can't have Keys with nested key_type
        BOOST_CORE_STATIC_ASSERT(sizeof...(Keys) == 0);
        // T& must be convertible to key_type&
        BOOST_CORE_STATIC_ASSERT(std::is_convertible<
            T&, typename get_key<T>::type&>::value);
        auto p = make_any<T>(std::forward<Args>(args)...);
        keyset<T, typename get_key<T>::type> ks(
            *static_cast<T*>(p->get()));
        return *static_cast<T*>(insert_impl(
            std::move(p), ks.kn, ks.N));
    }

    /** Construct and insert an object into the container

        A new object of type `T` is constructed in place using the provided
        arguments and inserted into the container. The type `T` must not
        already exist in the container, nor may any of the additional key
        types refer to an existing object. The type `T&` must be convertible
        to a reference to each specified key type.

        @par Constraints
        `T::key_type` must not name a type.

        @par Exception Safety
        Strong guarantee.

        @par Thread Safety
        Not thread-safe.

        @throws std::invalid_argument On duplicate insertion.
        @tparam T The type of object to construct and insert.
        @tparam Keys Optional key types associated with the object.
        @param args Arguments forwarded to the constructor of `T`.
        @return A reference to the inserted object.
    */
    template<class T, class... Keys, class... Args>
    auto emplace(Args&&... args) ->
        typename std::enable_if<! get_key<T>::value, T&>::type
    {
        // T& must be convertible to each of Keys&
        BOOST_CORE_STATIC_ASSERT(all_true<std::is_convertible<
            T&, Keys&>::value...>::value);
        auto p = make_any<T>(std::forward<Args>(args)...);
        keyset<T, Keys...> ks(*static_cast<T*>(p->get()));
        return *static_cast<T*>(insert_impl(
            std::move(p), ks.kn, ks.N));
    }

    /** Return an existing object, creating it if necessary

        If an object of the exact type `T` already exists in the container,
        a reference to that object is returned. Otherwise, a new object is
        constructed in place using the provided arguments, and a reference
        to the newly created object is returned. The type `T` must not
        already exist in the container, nor may any of the additional key
        types refer to an existing object. The type `T` must be convertible
        to a reference to each additional key type.

        @par Exception Safety
        Strong guarantee.

        @par Thread Safety
        Not thread-safe.

        @throws std::invalid_argument On duplicate insertion.
        @tparam T The type of object to return or create.
        @tparam Keys Optional key types associated with the object.
        @param args Arguments forwarded to the constructor of `T`.
        @return A reference to the existing or newly created object.
    */
    template<class T, class... Keys, class... Args>
    auto try_emplace(Args&&... args) ->
        typename std::enable_if<get_key<T>::value, T&>::type
    {
        // Can't have Keys with nested key_type
        BOOST_CORE_STATIC_ASSERT(sizeof...(Keys) == 0);
        // T& must be convertible to key_type&
        BOOST_CORE_STATIC_ASSERT(std::is_convertible<
            T&, typename get_key<T>::type&>::value);
        if(auto t = find<T>())
            return *t;
        auto p = make_any<T>(std::forward<Args>(args)...);
        keyset<T, typename get_key<T>::type> ks(
            *static_cast<T*>(p->get()));
        return *static_cast<T*>(insert_impl(
            std::move(p), ks.kn, ks.N));
    }

    /** Return an existing object, creating it if necessary

        If an object of the exact type `T` already exists in the container,
        a reference to that object is returned. Otherwise, a new object is
        constructed in place using the provided arguments, and a reference
        to the newly created object is returned. The type `T` must not
        already exist in the container, nor may any of the additional key
        types refer to an existing object. The type `T` must be convertible
        to a reference to each additional key type.

        @par Exception Safety
        Strong guarantee.

        @par Thread Safety
        `const` member function calls are thread-safe.
        Calls to non-`const` member functions must not run concurrently
        with other member functions on the same object.

        @throws std::invalid_argument On duplicate insertion.
        @tparam T The type of object to return or create.
        @tparam Keys Optional key types associated with the object.
        @param args Arguments forwarded to the constructor of `T`.
        @return A reference to the existing or newly created object.
    */
    template<class T, class... Keys, class... Args>
    auto try_emplace(Args&&... args) ->
        typename std::enable_if<! get_key<T>::value, T&>::type
    {
        // T& must be convertible to each of Keys&
        BOOST_CORE_STATIC_ASSERT(all_true<std::is_convertible<
            T&, Keys&>::value...>::value);
        if(auto t = find<T>())
            return *t;
        auto p = make_any<T>(std::forward<Args>(args)...);
        keyset<T, Keys...> ks(*static_cast<T*>(p->get()));
        return *static_cast<T*>(insert_impl(
            std::move(p), ks.kn, ks.N));
    }

    /** Insert an object by moving or copying it into the container

        If an object of the same type `T` already exists in the container,
        or if any of the additional key types would refer to an existing
        object, an exception is thrown. Otherwise, the object is inserted
        by move or copy construction, and a reference to the stored object
        is returned. The type `T` must be convertible to a reference to each
        additional key type.

        @par Exception Safety
        Strong guarantee.

        @par Thread Safety
        Not thread-safe.

        @throws std::invalid_argument On duplicate insertion.
        @tparam T The type of object to insert.
        @tparam Keys Optional key types associated with the object.
        @param t The object to insert.
        @return A reference to the inserted object.
    */
    template<class T, class... Keys>
    T& insert(T&& t)
    {
        return emplace<typename
            std::remove_cv<T>::type, Keys...>(
                std::forward<T>(t));
    }

    /** Return an existing object or create a new one

        If an object of the exact type `T` already exists in the container,
        a reference to that object is returned. Otherwise, a new object of
        type `T` is default-constructed in the container, and a reference
        to the newly created object is returned. This function ignores
        nested key types and cannot be used to specify additional keys.

        @par Constraints
        `T` must be default-constructible.

        @par Exception Safety
        Strong guarantee.

        @par Thread Safety
        Not thread-safe.

        @tparam T The type of object to retrieve or create.
        @return A reference to the stored object.
    */
    template<class T>
    T& use()
    {
        // T must be default constructible
        BOOST_CORE_STATIC_ASSERT(
            std::is_default_constructible<T>::value);
        if(auto t = find<T>())
            return *t;
        return emplace<T>();
    }

protected:
    struct any;
    class elements;

    /** Remove and destroy all objects in the container.

        All stored objects are destroyed in the reverse order
        of construction. The container is left empty.
    */
    BOOST_CAPY_DECL
    void
    clear() noexcept;

    /** Return a range of all stored elements
        @par Thread Safety
        `const` member function calls are thread-safe.
        Calls to non-`const` member functions must not run concurrently
        with other member functions on the same object.
        @return An object representing the range of stored elements.
    */
    BOOST_CAPY_DECL
    elements
    get_elements() noexcept;

private:
    template<bool...> struct bool_pack {};
    template<bool... Bs>
    struct all_true : std::is_same<bool_pack<
        true, Bs...>, bool_pack<Bs..., true>> {};

    template<class T, class = void>
    struct has_start : std::false_type {};

    template<class T>
    struct has_start<T, typename std::enable_if<
        std::is_same<decltype(std::declval<T>().start()),
            void>::value>::type> : std::true_type {};

    template<class T, class = void>
    struct has_stop : std::false_type {};

    template<class T>
    struct has_stop<T, typename std::enable_if<
        std::is_same<decltype(std::declval<T>().stop()),
            void>::value>::type> : std::true_type {};

    struct key
    {
        detail::typeindex ti =
            detail::typeindex(BOOST_CORE_TYPEID(void));
        void* p = nullptr;

        key() = default;
        key(detail::typeindex const& ti_,
            void* p_) noexcept : ti(ti_) , p(p_) {}
    };

    template<class T, class... Key>
    struct keyset;

    template<class T>
    struct keyset<T>
    {
        static constexpr std::size_t N = 1;
        key kn[1];

        explicit keyset(T& t) noexcept
            : kn{ key(detail::typeindex(BOOST_CORE_TYPEID(T)), &t) }
        {
        }
    };

    template<class T, class... Keys>
    struct keyset
    {
        static constexpr std::size_t N = 1 + sizeof...(Keys);
        key kn[N + 1];

        explicit keyset(T& t) noexcept
            : kn{
                key(detail::typeindex(BOOST_CORE_TYPEID(T)),
                    std::addressof(t)),
                key(detail::typeindex(BOOST_CORE_TYPEID(Keys)),
                    &static_cast<Keys&>(t))..., }
        {
        }
    };

    template<class T> struct any_impl;

    using any_ptr = std::unique_ptr<any>;

    template<class T, class... Args>
    auto
    make_any(Args&&... args) ->
        std::unique_ptr<any_impl<T>>
    {
        return std::unique_ptr<any_impl<T>>(new
            any_impl<T>(std::forward<Args>(args)...));
    }

    void destroy() noexcept;
    BOOST_CAPY_DECL any& get(std::size_t i);
    BOOST_CAPY_DECL void* find(
        core::typeinfo const& ti) const noexcept;
    BOOST_CAPY_DECL void* insert_impl(any_ptr,
        key const* = nullptr, std::size_t = 0);

    std::vector<any_ptr> v_;
    std::unordered_map<
        detail::typeindex, void*> m_;
};

//------------------------------------------------

struct BOOST_CAPY_DECL
    polystore::any
{
    virtual ~any() = default;
    virtual void start() = 0;
    virtual void stop() = 0;
private:
    friend class polystore;
    virtual void* get() noexcept = 0;
};

//------------------------------------------------

class polystore::elements
{
public:
    std::size_t size() const noexcept
    {
        return n_;
    }

    any& operator[](
        std::size_t i) noexcept
    {
        return ps_.get(i);
    }

private:
    friend class polystore;

    elements(
        std::size_t n,
        polystore& ps)
        : n_(n)
        , ps_(ps)
    {
    }

    std::size_t n_;
    polystore& ps_;
};

//------------------------------------------------

template<class T>
struct polystore::any_impl : polystore::any
{
    T t;

    template<class... Args>
    explicit any_impl(Args&&... args)
        : t(std::forward<Args>(args)...)
    {
    }
    void* get() noexcept override { return std::addressof(t); }
    void start() override { do_start(has_start<T>{}); }
    void stop() override { do_stop(has_stop<T>{}); }
    void do_start(std::true_type) { t.start(); }
    void do_start(std::false_type) {}
    void do_stop(std::true_type) { t.stop(); }
    void do_stop(std::false_type) {}
};

//------------------------------------------------

namespace detail {

template<class T> struct arg;
template<class T> struct arg<T const&> : arg<T&> {};
template<class T> struct arg<T const*> : arg<T*> {};
template<class T> struct arg<T&>
{
    T& operator()(polystore& ps) const
    {
        return ps.get<T>();
    }
};
template<class T> struct arg<T*>
{
    T* operator()(polystore& ps) const
    {
        return ps.find<T>();
    }
};

template<class F, class... Args>
auto
invoke(polystore& ps, F&& f,
    detail::type_list<Args...> const&) ->
        typename detail::call_traits<typename
            std::decay<F>::type>::return_type
{
    return std::forward<F>(f)(arg<Args>()(ps)...);
}

} // detail

/** Invoke a callable, injecting stored objects as arguments
    The callable is invoked with zero or more arguments.
    For each argument type, if an object of that type
    (or key type) is stored in the container, a reference
    to that object is passed to the callable.
    @par Example
    @code
    struct A { int i = 1; };
    polystore ps;
    ps.emplace<A>();
    ps.invoke([](A& a){ assert(a.i == 1; });
    @endcode
    @param f The callable to invoke.
    @return The result of the invocation.
    @throws std::bad_typeid if any reference argument
        types are not found in the container.
*/
template<class F>
auto
invoke(polystore& ps, F&& f) ->
    typename detail::call_traits<
        typename std::decay<F>::type>::return_type
{
    return detail::invoke(ps, std::forward<F>(f),
        typename detail::call_traits< typename
            std::decay<F>::type>::arg_types{});
}

} // capy
} // boost

#endif
