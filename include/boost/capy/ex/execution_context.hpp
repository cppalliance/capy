//
// Copyright (c) 2025 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_EXECUTION_CONTEXT_HPP
#define BOOST_CAPY_EXECUTION_CONTEXT_HPP

#include <boost/capy/detail/config.hpp>
#include <boost/capy/detail/frame_memory_resource.hpp>
#include <boost/capy/concept/executor.hpp>
#include <concepts>
#include <memory>
#include <memory_resource>
#include <mutex>
#include <tuple>
#include <type_traits>
#include <typeindex>
#include <utility>

namespace boost {
namespace capy {

/** Base class for I/O object containers providing service management.

    An execution context represents a place where function objects are
    executed. It provides a service registry where polymorphic services
    can be stored and retrieved by type. Each service type may be stored
    at most once. Services may specify a nested `key_type` to enable
    lookup by a base class type.

    Derived classes such as `io_context` extend this to provide
    execution facilities like event loops and thread pools. Derived
    class destructors must call `shutdown()` and `destroy()` to ensure
    proper service cleanup before member destruction.

    @par Service Lifecycle
    Services are created on first use via `use_service()` or explicitly
    via `make_service()`. During destruction, `shutdown()` is called on
    each service in reverse order of creation, then `destroy()` deletes
    them. Both functions are idempotent.

    @par Thread Safety
    Service registration and lookup functions are thread-safe.
    The `shutdown()` and `destroy()` functions are not thread-safe
    and must only be called during destruction.

    @par Example
    @code
    struct file_service : execution_context::service
    {
    protected:
        void shutdown() override {}
    };

    struct posix_file_service : file_service
    {
        using key_type = file_service;

        explicit posix_file_service(execution_context&) {}
    };

    class io_context : public execution_context
    {
    public:
        ~io_context()
        {
            shutdown();
            destroy();
        }
    };

    io_context ctx;
    ctx.make_service<posix_file_service>();
    ctx.find_service<file_service>();       // returns posix_file_service*
    ctx.find_service<posix_file_service>(); // also works
    @endcode

    @see service, is_execution_context
*/
class BOOST_CAPY_DECL
    execution_context
{
    template<class T, class = void>
    struct get_key : std::false_type
    {};

    template<class T>
    struct get_key<T, std::void_t<typename T::key_type>> : std::true_type
    {
        using type = typename T::key_type;
    };

public:
    //------------------------------------------------

    /** Abstract base class for services owned by an execution context.

        Services provide extensible functionality to an execution context.
        Each service type can be registered at most once. Services are
        created via `use_service()` or `make_service()` and are owned by
        the execution context for their lifetime.

        Derived classes must implement the pure virtual `shutdown()` member
        function, which is called when the owning execution context is
        being destroyed. The `shutdown()` function should release resources
        and cancel outstanding operations without blocking.

        @par Deriving from service
        @li Implement `shutdown()` to perform cleanup.
        @li Accept `execution_context&` as the first constructor parameter.
        @li Optionally define `key_type` to enable base-class lookup.

        @par Example
        @code
        struct my_service : execution_context::service
        {
            explicit my_service(execution_context&) {}

        protected:
            void shutdown() override
            {
                // Cancel pending operations, release resources
            }
        };
        @endcode

        @see execution_context
    */
    class BOOST_CAPY_DECL
        service
    {
    public:
        virtual ~service() = default;

    protected:
        service() = default;

        /** Called when the owning execution context shuts down.

            Implementations should release resources and cancel any
            outstanding asynchronous operations. This function must
            not block and must not throw exceptions. Services are
            shut down in reverse order of creation.

            @par Exception Safety
            No-throw guarantee.
        */
        virtual void shutdown() = 0;

    private:
        friend class execution_context;

        service* next_ = nullptr;
#ifdef _MSC_VER
# pragma warning(push)
# pragma warning(disable: 4251)
#endif
        std::type_index t0_ = typeid(void);
        std::type_index t1_ = typeid(void);
#ifdef _MSC_VER
# pragma warning(pop)
#endif
    };

    //------------------------------------------------

    execution_context(execution_context const&) = delete;

    execution_context& operator=(execution_context const&) = delete;

    /** Destructor.

        Calls `shutdown()` then `destroy()` to clean up all services.

        @par Effects
        All services are shut down and deleted in reverse order
        of creation.

        @par Exception Safety
        No-throw guarantee.
    */
    ~execution_context();

    /** Default constructor.

        @par Exception Safety
        Strong guarantee.
    */
    execution_context();

    /** Return true if a service of type T exists.

        @par Thread Safety
        Thread-safe.

        @tparam T The type of service to check.

        @return `true` if the service exists.
    */
    template<class T>
    bool has_service() const noexcept
    {
        return find_service<T>() != nullptr;
    }

    /** Return a pointer to the service of type T, or nullptr.

        @par Thread Safety
        Thread-safe.

        @tparam T The type of service to find.

        @return A pointer to the service, or `nullptr` if not present.
    */
    template<class T>
    T* find_service() const noexcept
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return static_cast<T*>(find_impl(typeid(T)));
    }

    /** Return a reference to the service of type T, creating it if needed.

        If no service of type T exists, one is created by calling
        `T(execution_context&)`. If T has a nested `key_type`, the
        service is also indexed under that type.

        @par Constraints
        @li `T` must derive from `service`.
        @li `T` must be constructible from `execution_context&`.

        @par Exception Safety
        Strong guarantee. If service creation throws, the container
        is unchanged.

        @par Thread Safety
        Thread-safe.

        @tparam T The type of service to retrieve or create.

        @return A reference to the service.
    */
    template<class T>
    T& use_service()
    {
        static_assert(std::is_base_of<service, T>::value,
            "T must derive from service");
        static_assert(std::is_constructible<T, execution_context&>::value,
            "T must be constructible from execution_context&");

        struct impl : factory
        {
            impl()
                : factory(
                    typeid(T),
                    get_key<T>::value
                        ? typeid(typename get_key<T>::type)
                        : typeid(T))
            {
            }

            service* create(execution_context& ctx) override
            {
                return new T(ctx);
            }
        };

        impl f;
        return static_cast<T&>(use_service_impl(f));
    }

    /** Construct and add a service.

        A new service of type T is constructed using the provided
        arguments and added to the container. If T has a nested
        `key_type`, the service is also indexed under that type.

        @par Constraints
        @li `T` must derive from `service`.
        @li `T` must be constructible from `execution_context&, Args...`.
        @li If `T::key_type` exists, `T&` must be convertible to `key_type&`.

        @par Exception Safety
        Strong guarantee. If service creation throws, the container
        is unchanged.

        @par Thread Safety
        Thread-safe.

        @throws std::invalid_argument if a service of the same type
            or `key_type` already exists.

        @tparam T The type of service to create.

        @param args Arguments forwarded to the constructor of T.

        @return A reference to the created service.
    */
    template<class T, class... Args>
    T& make_service(Args&&... args)
    {
        static_assert(std::is_base_of<service, T>::value,
            "T must derive from service");
        if constexpr(get_key<T>::value)
        {
            static_assert(
                std::is_convertible<T&, typename get_key<T>::type&>::value,
                "T& must be convertible to key_type&");
        }

        struct impl : factory
        {
            std::tuple<Args&&...> args_;

            explicit impl(Args&&... a)
                : factory(
                    typeid(T),
                    get_key<T>::value
                        ? typeid(typename get_key<T>::type)
                        : typeid(T))
                , args_(std::forward<Args>(a)...)
            {
            }

            service* create(execution_context& ctx) override
            {
                return std::apply([&ctx](auto&&... a) {
                    return new T(ctx, std::forward<decltype(a)>(a)...);
                }, std::move(args_));
            }
        };

        impl f(std::forward<Args>(args)...);
        return static_cast<T&>(make_service_impl(f));
    }

    //------------------------------------------------

    /** Return the memory resource used for coroutine frame allocation.

        The returned pointer is valid for the lifetime of this context.
        By default, this returns a pointer to the recycling memory
        resource which pools frame allocations for reuse.

        @return Pointer to the frame allocator.

        @see set_frame_allocator
    */
    std::pmr::memory_resource*
    get_frame_allocator() const noexcept
    {
        return frame_alloc_;
    }

    /** Set the memory resource used for coroutine frame allocation.

        The caller is responsible for ensuring the memory resource
        remains valid for the lifetime of all coroutines launched
        using this context's executor.

        @par Thread Safety
        Not thread-safe. Must not be called while any thread may
        be referencing this execution context or its executor.

        @param mr Pointer to the memory resource.

        @see get_frame_allocator
    */
    void
    set_frame_allocator(std::pmr::memory_resource* mr) noexcept
    {
        owned_.reset();
        frame_alloc_ = mr;
    }

    /** Set the frame allocator from a standard Allocator.

        The allocator is wrapped in an internal memory resource
        adapter owned by this context. The wrapper remains valid
        for the lifetime of this context or until a subsequent
        call to set_frame_allocator.

        @par Thread Safety
        Not thread-safe. Must not be called while any thread may
        be referencing this execution context or its executor.

        @tparam Allocator The allocator type satisfying the
            standard Allocator requirements.

        @param a The allocator to use.

        @see get_frame_allocator
    */
    template<class Allocator>
        requires (!std::is_pointer_v<Allocator>)
    void
    set_frame_allocator(Allocator const& a)
    {
        static_assert(
            requires { typename std::allocator_traits<Allocator>::value_type; },
            "Allocator must satisfy allocator requirements");
        static_assert(
            std::is_copy_constructible_v<Allocator>,
            "Allocator must be copy constructible");

        auto p = std::make_shared<
            detail::frame_memory_resource<Allocator>>(a);
        frame_alloc_ = p.get();
        owned_ = std::move(p);
    }

protected:
    /** Shut down all services.

        Calls `shutdown()` on each service in reverse order of creation.
        After this call, services remain allocated but are in a stopped
        state. Derived classes should call this in their destructor
        before any members are destroyed. This function is idempotent;
        subsequent calls have no effect.

        @par Effects
        Each service's `shutdown()` member function is invoked once.

        @par Postconditions
        @li All services are in a stopped state.

        @par Exception Safety
        No-throw guarantee.

        @par Thread Safety
        Not thread-safe. Must not be called concurrently with other
        operations on this execution_context.
    */
    void shutdown() noexcept;

    /** Destroy all services.

        Deletes all services in reverse order of creation. Derived
        classes should call this as the final step of destruction.
        This function is idempotent; subsequent calls have no effect.

        @par Preconditions
        @li `shutdown()` has been called.

        @par Effects
        All services are deleted and removed from the container.

        @par Postconditions
        @li The service container is empty.

        @par Exception Safety
        No-throw guarantee.

        @par Thread Safety
        Not thread-safe. Must not be called concurrently with other
        operations on this execution_context.
    */
    void destroy() noexcept;

private:
    struct BOOST_CAPY_DECL
        factory
    {
#ifdef _MSC_VER
# pragma warning(push)
# pragma warning(disable: 4251)
#endif
        std::type_index t0;
        std::type_index t1;
#ifdef _MSC_VER
# pragma warning(pop)
#endif

        factory(std::type_index t0_, std::type_index t1_)
            : t0(t0_), t1(t1_)
        {
        }

        virtual service* create(execution_context&) = 0;

    protected:
        ~factory() = default;
    };

    service* find_impl(std::type_index ti) const noexcept;
    service& use_service_impl(factory& f);
    service& make_service_impl(factory& f);

#ifdef _MSC_VER
# pragma warning(push)
# pragma warning(disable: 4251)
#endif
    mutable std::mutex mutex_;
    std::shared_ptr<void> owned_;
#ifdef _MSC_VER
# pragma warning(pop)
#endif
    std::pmr::memory_resource* frame_alloc_ = nullptr;
    service* head_ = nullptr;
    bool shutdown_ = false;
};

} // namespace capy
} // namespace boost

#endif
