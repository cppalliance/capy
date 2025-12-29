//
// Copyright (c) 2025 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_EXECUTOR_HPP
#define BOOST_CAPY_EXECUTOR_HPP

#include <boost/capy/detail/config.hpp>
#include <boost/capy/detail/call_traits.hpp>
#include <boost/capy/async_result.hpp>
#include <boost/system/result.hpp>
#include <cstddef>
#include <exception>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>

namespace boost {
namespace capy {

/** A lightweight handle for submitting work to an execution context.

    This class provides a value-type interface for submitting
    work to be executed asynchronously. It supports two modes:

    @li **Reference mode**: Non-owning reference to an execution
        context. The caller must ensure the context outlives all
        executors that reference it. Created via the constructor.

    @li **Owning mode**: Shared ownership of a value-type executor.
        The executor is stored internally and its lifetime is
        managed automatically. Created via the `from()` factory.

    @par Thread Safety
    Distinct objects may be accessed concurrently. Shared objects
    require external synchronization.

    @par Implementing an Execution Context

    Both execution contexts (for reference mode) and value-type
    executors (for owning mode) must declare
    `friend struct executor::access` and provide three private
    member functions:

    @li `void* allocate(std::size_t size, std::size_t align)` —
        Allocate storage for a work item. May throw.

    @li `void deallocate(void* p, std::size_t size, std::size_t align)` —
        Free storage previously returned by allocate. Must not throw.

    @li `void submit(executor::work* w)` —
        Take ownership of the work item and arrange for execution.
        The context must eventually call `w->invoke()`, then
        `w->~work()`, then deallocate the storage.

    All three functions must be safe to call concurrently.

    @par Example (Reference Mode)
    @code
    class my_pool
    {
        friend struct executor::access;

        std::mutex mutex_;
        std::queue<executor::work*> queue_;

    public:
        void run_one()
        {
            executor::work* w = nullptr;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if(!queue_.empty())
                {
                    w = queue_.front();
                    queue_.pop();
                }
            }
            if(w)
            {
                w->invoke();
                std::size_t size = w->size;
                std::size_t align = w->align;
                w->~work();
                deallocate(w, size, align);
            }
        }

    private:
        void* allocate(std::size_t size, std::size_t)
        {
            return std::malloc(size);
        }

        void deallocate(void* p, std::size_t, std::size_t)
        {
            std::free(p);
        }

        void submit(executor::work* w)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            queue_.push(w);
        }
    };

    // Usage: reference mode
    my_pool pool;
    executor exec(pool);  // pool must outlive exec
    @endcode

    @par Example (Owning Mode)
    @code
    struct my_strand
    {
        friend struct executor::access;

        // ... internal state ...

    private:
        void* allocate(std::size_t size, std::size_t)
        {
            return std::malloc(size);
        }

        void deallocate(void* p, std::size_t, std::size_t)
        {
            std::free(p);
        }

        void submit(executor::work* w)
        {
            // ... queue and serialize work ...
        }
    };

    // Usage: owning mode
    executor exec = executor::from(my_strand{});  // executor owns the strand
    @endcode
*/
class executor
{
    struct ops;

    template<class T>
    struct ops_for;

    template<class Exec>
    struct holder;

    std::shared_ptr<const ops> ops_;
    void* obj_;

public:
    /** Abstract base for type-erased work.

        Implementations derive from this to wrap callable
        objects for submission through the executor.

        @par Lifecycle

        When work is submitted via an executor:
        @li Storage is allocated via the context's allocate()
        @li A work-derived object is constructed in place
        @li Ownership transfers to the context via submit()
        @li The context calls invoke() to execute the work
        @li The context destroys and deallocates the work

        @note Work objects must not be copied or moved after
        construction. They are always destroyed in place.

        @note Execution contexts are responsible for tracking
        the size and alignment of allocated work objects for
        deallocation. A common pattern is to prepend metadata
        to the allocation.
    */
    struct BOOST_SYMBOL_VISIBLE work
    {
        virtual ~work() = default;
        virtual void invoke() = 0;
    };

    class factory;

    /** Accessor for execution context private members.

        Execution contexts should declare this as a friend to
        allow the executor machinery to call their private
        allocate, deallocate, and submit members:

        @code
        class my_context
        {
            friend struct executor::access;
            // ...
        private:
            void* allocate(std::size_t, std::size_t);
            void deallocate(void*, std::size_t, std::size_t);
            void submit(executor::work*);
        };
        @endcode
    */
    struct access
    {
        template<class T>
        static void*
        allocate(T& ctx, std::size_t size, std::size_t align)
        {
            return ctx.allocate(size, align);
        }

        template<class T>
        static void
        deallocate(T& ctx, void* p, std::size_t size, std::size_t align)
        {
            ctx.deallocate(p, size, align);
        }

        template<class T>
        static void
        submit(T& ctx, work* w)
        {
            ctx.submit(w);
        }
    };

    /** Construct an executor referencing an execution context.

        Creates an executor in reference mode. The executor holds
        a non-owning reference to the context.

        The implementation type must provide:
        - `void* allocate(std::size_t size, std::size_t align)`
        - `void deallocate(void* p, std::size_t size, std::size_t align)`
        - `void submit(executor::work* w)`

        @param ctx The execution context to reference.
        The context must outlive this executor and all copies.

        @see from
    */
    template<
        class T,
        class = typename std::enable_if<
            !std::is_same<
                typename std::decay<T>::type,
                executor>::value>::type>
    executor(T& ctx) noexcept;

    /** Constructor

        Default-constructed executors are empty.
    */
    executor() noexcept
        : ops_()
        , obj_(nullptr)
    {
    }

    /** Create an executor with shared ownership of a value-type executor.

        Creates an executor in owning mode. The provided executor
        is moved into shared storage and its lifetime is managed
        automatically via reference counting.

        The executor type must provide:
        - `void* allocate(std::size_t size, std::size_t align)`
        - `void deallocate(void* p, std::size_t size, std::size_t align)`
        - `void submit(executor::work* w)`

        @param exec The executor to wrap (moved).

        @return An executor that shares ownership of the wrapped executor.

        @par Example
        @code
        // Wrap a value-type executor
        executor exec = executor::wrap(my_strand{});

        // Copies share ownership (reference counted)
        executor exec2 = exec;  // both reference the same strand
        @endcode
    */
    template<class Exec>
    static executor
    wrap(Exec exec);

    /** Return true if the executor references an execution context.
    */
    explicit
    operator bool() const noexcept
    {
        return ops_ != nullptr;
    }

    /** Submit work for execution (fire-and-forget).

        This overload uses the allocation-aware factory
        mechanism, allowing the implementation to control
        memory allocation strategy.

        @param f The callable to execute.
    */
    template<class F>
    void
    post(F&& f);

    /** Submit work and invoke a handler on completion.

        The work function is executed asynchronously. When it
        completes, the handler is invoked with the result or
        any exception that was thrown.

        The handler must be invocable with the signature:
        @code
        void handler( system::result<T, std::exception_ptr> );
        @endcode
        where `T` is the return type of `f`.

        @param f The work function to execute.

        @param handler The completion handler invoked with
        the result or exception.
    */
    template<class F, class Handler>
    auto
    async_post(F&& f, Handler&& handler) ->
        typename std::enable_if<! std::is_void<
            typename detail::call_traits<typename
                std::decay<F>::type>::return_type>::value>::type;

    /** Submit work and invoke a handler on completion.

        The work function is executed asynchronously. When it
        completes, the handler is invoked with success or any
        exception that was thrown.

        The handler must be invocable with the signature:
        @code
        void handler( system::result<void, std::exception_ptr> );
        @endcode

        @param f The work function to execute.

        @param handler The completion handler invoked with
        the result or exception.
    */
    template<class F, class Handler>
    auto
    async_post(F&& f, Handler&& handler) ->
        typename std::enable_if<std::is_void<typename
            detail::call_traits<typename std::decay<F>::type
                >::return_type>::value>::type;

#ifdef BOOST_CAPY_HAS_CORO
    /** Submit work and return an awaitable result.

        The work function is executed asynchronously. The
        returned async_result can be awaited in a coroutine
        to obtain the result.

        @param f The work function to execute.

        @return An awaitable that produces the result of the work.
    */
    template<class F>
    auto
    async_post(F&& f) ->
        async_result<std::invoke_result_t<std::decay_t<F>>>
        requires (!std::is_void_v<std::invoke_result_t<std::decay_t<F>>>);

    /** Submit work and return an awaitable result.

        The work function is executed asynchronously. The returned
        async_result can be awaited in a coroutine to wait
        for completion.

        @param f The work function to execute.

        @return An awaitable that completes when the work finishes.
    */
    template<class F>
    auto
    async_post(F&& f) ->
        async_result<void>
        requires std::is_void_v<std::invoke_result_t<std::decay_t<F>>>;
#endif
};

//-----------------------------------------------------------------------------

/** Static vtable for type-erased executor operations.
*/
struct executor::ops
{
    void* (*allocate)(void* obj, std::size_t size, std::size_t align);
    void (*deallocate)(void* obj, void* p, std::size_t size, std::size_t align);
    void (*submit)(void* obj, work* w);
};

/** Type-specific operation implementations.

    For each concrete type T, this provides static functions
    that cast the void* back to T* and forward via access.
*/
template<class T>
struct executor::ops_for
{
    static void*
    allocate(void* obj, std::size_t size, std::size_t align)
    {
        return access::allocate(*static_cast<T*>(obj), size, align);
    }

    static void
    deallocate(void* obj, void* p, std::size_t size, std::size_t align)
    {
        access::deallocate(*static_cast<T*>(obj), p, size, align);
    }

    static void
    submit(void* obj, work* w)
    {
        access::submit(*static_cast<T*>(obj), w);
    }

    static constexpr ops table = {
        &allocate,
        &deallocate,
        &submit
    };
};

template<class T>
constexpr executor::ops executor::ops_for<T>::table;

//-----------------------------------------------------------------------------

/** Holder for value-type executors in owning mode.

    Stores the executor by value and provides the vtable
    implementation that forwards to the held executor.
*/
template<class Exec>
struct executor::holder
{
    Exec exec;

    explicit
    holder(Exec e)
        : exec(std::move(e))
    {
    }

    static void*
    allocate(void* obj, std::size_t size, std::size_t align)
    {
        return access::allocate(
            static_cast<holder*>(obj)->exec, size, align);
    }

    static void
    deallocate(void* obj, void* p, std::size_t size, std::size_t align)
    {
        access::deallocate(
            static_cast<holder*>(obj)->exec, p, size, align);
    }

    static void
    submit(void* obj, work* w)
    {
        access::submit(
            static_cast<holder*>(obj)->exec, w);
    }

    static constexpr ops table = {
        &allocate,
        &deallocate,
        &submit
    };
};

template<class Exec>
constexpr executor::ops executor::holder<Exec>::table;

//-----------------------------------------------------------------------------

namespace detail {

// Null deleter for shared_ptr pointing to static storage
struct null_deleter
{
    void operator()(const void*) const noexcept {}
};

} // detail

template<class T, class>
executor::
executor(T& ctx) noexcept
    : ops_(
        &ops_for<typename std::decay<T>::type>::table,
        detail::null_deleter())
    , obj_(const_cast<void*>(static_cast<void const*>(std::addressof(ctx))))
{
}

template<class Exec>
executor
executor::
wrap(Exec exec)
{
    typedef typename std::decay<Exec>::type exec_type;
    typedef holder<exec_type> holder_type;

    std::shared_ptr<holder_type> h =
        std::make_shared<holder_type>(std::move(exec));

    executor ex;
    // Use aliasing constructor: share ownership with h,
    // but point to the static vtable
    ex.ops_ = std::shared_ptr<const ops>(h, &holder_type::table);
    ex.obj_ = h.get();
    return ex;
}

//-----------------------------------------------------------------------------

/** RAII factory for constructing and submitting work.

    This class manages the multi-phase process of:
    1. Allocating storage from the executor implementation
    2. Constructing work in-place via placement-new
    3. Submitting the work for execution

    If an exception occurs before commit(), the destructor
    will clean up any allocated resources.

    @par Exception Safety
    Strong guarantee. If any operation throws, all resources
    are properly released.
*/
class executor::factory
{
    ops const* ops_;
    void* obj_;
    void* storage_;
    std::size_t size_;
    std::size_t align_;
    bool committed_;

public:
    /** Construct a factory bound to an executor.

        @param exec The executor to submit work to.
    */
    explicit
    factory(executor& exec) noexcept
        : ops_(exec.ops_.get())
        , obj_(exec.obj_)
        , storage_(nullptr)
        , size_(0)
        , align_(0)
        , committed_(false)
    {
    }

    /** Destructor. Releases resources if not committed.
    */
    ~factory()
    {
        if(storage_ && !committed_)
            ops_->deallocate(obj_, storage_, size_, align_);
    }

    factory(factory const&) = delete;
    factory& operator=(factory const&) = delete;

    /** Allocate storage for work of given size and alignment.

        @param size The size in bytes required.
        @param align The alignment required.
        @return Pointer to uninitialized storage.
    */
    void*
    allocate(std::size_t size, std::size_t align)
    {
        storage_ = ops_->allocate(obj_, size, align);
        size_ = size;
        align_ = align;
        return storage_;
    }

    /** Submit constructed work for execution.

        After calling commit(), the factory releases ownership
        and the destructor becomes a no-op.

        @param w Pointer to the constructed work object
                 (must reside in the allocated storage).
    */
    void
    commit(work* w)
    {
        committed_ = true;
        ops_->submit(obj_, w);
    }
};

//-----------------------------------------------------------------------------

template<class F>
void
executor::
post(F&& f)
{
    struct callable : work
    {
        typename std::decay<F>::type f_;

        explicit
        callable(F&& f)
            : f_(std::forward<F>(f))
        {
        }

        void
        invoke() override
        {
            f_();
        }
    };

    factory fac(*this);
    void* p = fac.allocate(sizeof(callable), alignof(callable));
    callable* w = ::new(p) callable(std::forward<F>(f));
    fac.commit(w);
}

//-----------------------------------------------------------------------------

template<class F, class Handler>
auto
executor::
async_post(F&& f, Handler&& handler) ->
    typename std::enable_if<! std::is_void<typename
        detail::call_traits<typename std::decay<F>::type
            >::return_type>::value>::type
{
    using T = typename detail::call_traits<
        typename std::decay<F>::type>::return_type;
    using result_type = system::result<T, std::exception_ptr>;

    struct callable
    {
        typename std::decay<F>::type f;
        typename std::decay<Handler>::type handler;

        void operator()()
        {
            try
            {
                handler(result_type(f()));
            }
            catch(...)
            {
                handler(result_type(std::current_exception()));
            }
        }
    };

    post(callable{std::forward<F>(f), std::forward<Handler>(handler)});
}

template<class F, class Handler>
auto
executor::
async_post(F&& f, Handler&& handler) ->
    typename std::enable_if<std::is_void<typename
    detail::call_traits<typename std::decay<F>::type
        >::return_type>::value>::type
{
    using result_type = system::result<void, std::exception_ptr>;

    struct callable
    {
        typename std::decay<F>::type f;
        typename std::decay<Handler>::type handler;

        void operator()()
        {
            try
            {
                f();
                handler(result_type());
            }
            catch(...)
            {
                handler(result_type(std::current_exception()));
            }
        }
    };

    post(callable{std::forward<F>(f), std::forward<Handler>(handler)});
}

#ifdef BOOST_CAPY_HAS_CORO

template<class F>
auto
executor::
async_post(F&& f) ->
    async_result<std::invoke_result_t<std::decay_t<F>>>
    requires (!std::is_void_v<std::invoke_result_t<std::decay_t<F>>>)
{
    using T = std::invoke_result_t<std::decay_t<F>>;

    return make_async_result<T>(
        [exec = *this, f = std::forward<F>(f)](auto on_done) mutable
        {
            exec.post(
                [f = std::move(f),
                 on_done = std::move(on_done)]() mutable
                {
                    on_done(f());
                });
        });
}

template<class F>
auto
executor::
async_post(F&& f) ->
    async_result<void>
    requires std::is_void_v<std::invoke_result_t<std::decay_t<F>>>
{
    return make_async_result<void>(
        [exec = *this, f = std::forward<F>(f)](auto on_done) mutable
        {
            exec.post(
                [f = std::move(f),
                 on_done = std::move(on_done)]() mutable
                {
                    f();
                    on_done();
                });
        });
}

#endif

} // capy
} // boost

#endif
