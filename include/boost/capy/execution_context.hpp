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
#include <boost/capy/concept/executor.hpp>
#include <boost/capy/intrusive_queue.hpp>
#include <concepts>
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
    class service
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
        std::type_index t0_ = typeid(void);
        std::type_index t1_ = typeid(void);
    };

    //------------------------------------------------

    /** Abstract base class for completion handlers.

        Handlers are continuations that execute after an asynchronous
        operation completes. They can be queued for deferred invocation,
        allowing callbacks and coroutine resumptions to be posted to an
        executor.

        Handlers should execute quickly - typically just initiating
        another I/O operation or suspending on a foreign task. Heavy
        computation should be avoided in handlers to prevent blocking
        the event loop.

        Handlers may be heap-allocated or may be data members of an
        enclosing object. The allocation strategy is determined by the
        creator of the handler.

        @par Ownership Contract

        Callers must invoke exactly ONE of `operator()` or `destroy()`,
        never both:

        @li `operator()` - Invokes the handler. The handler is
            responsible for its own cleanup (typically `delete this`
            for heap-allocated handlers). The caller must not call
            `destroy()` after invoking this.

        @li `destroy()` - Destroys an uninvoked handler. This is
            called when a queued handler must be discarded without
            execution, such as during shutdown or exception cleanup.
            For heap-allocated handlers, this typically calls
            `delete this`.

        @par Exception Safety

        Implementations of `operator()` must perform cleanup before
        any operation that might throw. This ensures that if the handler
        throws, the exception propagates cleanly to the caller of
        `run()` without leaking resources. Typical pattern:

        @code
        void operator()() override
        {
            auto coro = coro_;
            delete this;    // cleanup FIRST
            coro.resume();  // then resume (may throw)
        }
        @endcode

        This "delete-before-invoke" pattern also enables memory
        recycling - the handler's memory can be reused immediately
        by subsequent allocations.

        @note Callers must never delete handlers directly with `delete`;
        use `operator()` for normal invocation or `destroy()` for cleanup.

        @note Heap-allocated handlers are typically allocated with
        custom allocators to minimize allocation overhead in
        high-frequency async operations.

        @note Some handlers (such as those owned by containers like
        `std::unique_ptr` or embedded as data members) are not meant to
        be destroyed and should implement both functions as no-ops
        (for `operator()`, invoke the continuation but don't delete).

        @see queue
    */
    class handler : public intrusive_queue<handler>::node
    {
    public:
        virtual void operator()() = 0;
        virtual void destroy() = 0;

        /** Returns the user-defined data pointer.

            Derived classes may set this to store auxiliary data
            such as a pointer to the most-derived object.

            @par Postconditions
            @li Initially returns `nullptr` for newly constructed handlers.
            @li Returns the current value of `data_` if modified by a derived class.

            @return The user-defined data pointer, or `nullptr` if not set.
        */
        void* data() const noexcept
        {
            return data_;
        }

    protected:
        ~handler() = default;

        void* data_ = nullptr;
    };

    //------------------------------------------------

    /** An intrusive FIFO queue of handlers.

        This queue stores handlers using an intrusive linked list,
        avoiding additional allocations for queue nodes. Handlers
        are popped in the order they were pushed (first-in, first-out).

        The destructor calls `destroy()` on any remaining handlers.

        @note This is not thread-safe. External synchronization is
        required for concurrent access.

        @see handler
    */
    class queue
    {
        intrusive_queue<handler> q_;

    public:
        /** Default constructor.

            Creates an empty queue.

            @post `empty() == true`
        */
        queue() = default;

        /** Move constructor.

            Takes ownership of all handlers from `other`,
            leaving `other` empty.

            @param other The queue to move from.

            @post `other.empty() == true`
        */
        queue(queue&& other) noexcept
            : q_(std::move(other.q_))
        {
        }

        queue(queue const&) = delete;
        queue& operator=(queue const&) = delete;
        queue& operator=(queue&&) = delete;

        /** Destructor.

            Calls `destroy()` on any remaining handlers in the queue.
        */
        ~queue()
        {
            while(auto* h = q_.pop())
                h->destroy();
        }

        /** Return true if the queue is empty.

            @return `true` if the queue contains no handlers.
        */
        bool
        empty() const noexcept
        {
            return q_.empty();
        }

        /** Add a handler to the back of the queue.

            @param h Pointer to the handler to add.

            @pre `h` is not null and not already in a queue.
        */
        void
        push(handler* h) noexcept
        {
            q_.push(h);
        }

        /** Splice all handlers from another queue to the back.

            All handlers from `other` are moved to the back of this
            queue. After this call, `other` is empty.

            @param other The queue to splice from.

            @post `other.empty() == true`
        */
        void
        push(queue& other) noexcept
        {
            q_.splice(other.q_);
        }

        /** Remove and return the front handler.

            @return Pointer to the front handler, or `nullptr`
                if the queue is empty.
        */
        handler*
        pop() noexcept
        {
            return q_.pop();
        }
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
    struct factory
    {
        std::type_index t0;
        std::type_index t1;

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
#ifdef _MSC_VER
# pragma warning(pop)
#endif
    service* head_ = nullptr;
    bool shutdown_ = false;
};

} // namespace capy
} // namespace boost

#endif
