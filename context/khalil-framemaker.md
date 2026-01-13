# Frame-Appended Storage Pattern

A design for appending user-defined objects to coroutine frame allocations using TLS during the allocation window.

Credit: Design discussion with Khalil Estell (kammce)

---

## Problem Statement

### The Virtual Interface Challenge

When designing async interfaces using `virtual` functions, implementors should be able to choose between coroutine and synchronous implementations:

```cpp
struct IDataService {
    virtual task<std::string> fetch_data(int id) = 0;
};

// Sync implementation - returns cached data
struct CachedService : IDataService {
    task<std::string> fetch_data(int id) override {
        return task<std::string>(cache_.get(id));  // No coroutine!
    }
};

// Async implementation - uses coroutine
struct NetworkService : IDataService {
    task<std::string> fetch_data(int id) override {
        auto conn = co_await connect(server_);
        co_return co_await conn.request(id);
    }
};
```

### The Promise Storage Problem

If the return value is stored in the promise (`return_value()`), you **must** have a coroutine to provide a value—because without a coroutine, there's no promise.

**Solution**: Make the awaitable (task/future) the container of the value, not the promise.

### Avoiding std::variant

One approach uses `std::variant` to hold either value or coroutine handle:

```cpp
using future_state = std::variant<
    std::coroutine_handle<>,  // running
    T,                        // value (happy path)
    cancelled_state,          // cancelled
    std::exception_ptr        // exception
>;
```

But there's an alternative: **custom allocation with polymorphic state**.

---

## The Key Insight

Coroutine `operator new` can allocate **more** than the compiler-requested frame size. The extra space is yours.

Using TLS during "the window" (see [the-window.md](the-window.md)):

1. Set TLS before calling coroutine factory
2. `operator new` reads TLS, allocates extra, writes pointer back to TLS
3. Promise constructor reads TLS, stores the pointer
4. Coroutine accesses extra storage via promise

---

## Memory Layout

```
Coroutine path:
+-------------------+----------+-----------------------+
| coroutine frame   | padding  | user's T              |
| (compiler-sized)  | (align)  | (your state at end)   |
+-------------------+----------+-----------------------+
                               ^
                               promise.extra_ points here

Sync path (no coroutine):
+--------------------+
| immediate_state<T> |  (heap allocated separately)
+--------------------+
^
future.state_ points here
```

---

## The Mixin Design

### TLS Communication Structure

```cpp
namespace detail {

struct extra_storage_params {
    std::size_t size = 0;
    std::size_t alignment = alignof(std::max_align_t);
    void* storage = nullptr;  // Output: operator new writes here
};

inline extra_storage_params*& tls_extra_storage() noexcept {
    static thread_local extra_storage_params* p = nullptr;
    return p;
}

} // namespace detail
```

### RAII Guard for the Window

```cpp
template<typename T>
struct extra_storage_request {
    detail::extra_storage_params params_{sizeof(T), alignof(T), nullptr};
    detail::extra_storage_params* prev_ = nullptr;

    extra_storage_request() {
        prev_ = detail::tls_extra_storage();
        detail::tls_extra_storage() = &params_;
    }

    ~extra_storage_request() {
        detail::tls_extra_storage() = prev_;
    }

    void* storage() const noexcept { return params_.storage; }
};
```

### Promise Mixin

```cpp
struct extra_storage_allocating_base {
    void* extra_storage_ = nullptr;

    // Promise constructor reads from TLS (operator new already ran)
    extra_storage_allocating_base() {
        auto* params = detail::tls_extra_storage();
        if (params)
            extra_storage_ = params->storage;
    }

    // Typed access to the extra storage
    template<typename T>
    T& extra() noexcept {
        return *static_cast<T*>(extra_storage_);
    }

    template<typename T>
    T const& extra() const noexcept {
        return *static_cast<T const*>(extra_storage_);
    }

    static std::size_t align_up(std::size_t n, std::size_t align) noexcept {
        return (n + align - 1) & ~(align - 1);
    }

    static void* operator new(std::size_t frame_size) {
        auto* params = detail::tls_extra_storage();
        if (!params || params->size == 0) {
            return ::operator new(frame_size);
        }

        // Layout: [frame | padding | T]
        std::size_t t_offset = align_up(frame_size, params->alignment);
        std::size_t total = t_offset + params->size;

        void* raw = ::operator new(total);
        params->storage = static_cast<char*>(raw) + t_offset;
        return raw;
    }

    static void operator delete(void* ptr, std::size_t /*size*/) {
        // Note: For production, store metadata like frame_allocator does
        ::operator delete(ptr);
    }
};
```

---

## Accessing State from Inside the Coroutine

The promise stores `extra_storage_` but how does the coroutine body access it?

### The `await_transform` Pattern

Use a tag type and `await_transform` to yield the extra storage:

```cpp
// Tag type for requesting extra storage
template<typename T>
struct extra_t {};

template<typename T>
inline constexpr extra_t<T> extra{};

struct extra_storage_allocating_base {
    void* extra_storage_ = nullptr;
    
    // ... allocation code from above ...

    // Intercept extra<T> requests
    template<typename T>
    auto await_transform(extra_t<T>) {
        struct awaiter {
            T* ptr_;
            bool await_ready() const noexcept { return true; }  // Never suspends
            void await_suspend(std::coroutine_handle<>) const noexcept {}
            T& await_resume() const noexcept { return *ptr_; }
        };
        return awaiter{static_cast<T*>(extra_storage_)};
    }

    // Forward other awaitables normally
    template<typename A>
    decltype(auto) await_transform(A&& a) {
        return std::forward<A>(a);
    }
};
```

### Usage Inside Coroutine Body

```cpp
future<int> compute() {
    // Get typed reference to extra storage - never actually suspends
    auto& state = co_await extra<my_state_type>;
    
    state.counter++;
    co_return state.result;
}
```

The `await_ready() { return true; }` means these never actually suspend—they're just a mechanism to pass data from the promise into the coroutine body.

---

## Usage Pattern

```cpp
// Coroutine type using the mixin
template<typename T>
struct future {
    struct promise_type : extra_storage_allocating_base {
        auto& state() { return extra<future_state<T>>(); }
        // ... rest of promise
    };
};

// Factory that allocates state in the frame
template<typename T>
future<T> make_future_coro(/* args */) {
    extra_storage_request<future_state<T>> req;

    auto f = []() -> future<T> {
        // Access extra storage from inside the coroutine
        auto& state = co_await extra<future_state<T>>;
        // ... use state ...
        co_return /* ... */;
    }();

    // req.storage() now points to the uninitialized storage
    new (req.storage()) future_state<T>(/* initial state */);

    return f;
}
```

---

## Polymorphic State Pattern

Replace `std::variant` with abstract base:

```cpp
// Abstract base for state
template<typename T>
struct state_base {
    virtual ~state_base() = default;
    virtual bool ready() const = 0;
    virtual T& value() = 0;
    virtual std::exception_ptr exception() const = 0;
};

// State for coroutine path - lives at end of frame
template<typename T>
struct coroutine_state : state_base<T> {
    std::coroutine_handle<> h_;
    std::optional<T> value_;
    std::exception_ptr ep_;

    bool ready() const override { return h_.done(); }
    T& value() override { return *value_; }
    std::exception_ptr exception() const override { return ep_; }
};

// State for immediate value - no coroutine needed
template<typename T>
struct immediate_state : state_base<T> {
    T value_;

    explicit immediate_state(T v) : value_(std::move(v)) {}
    bool ready() const override { return true; }
    T& value() override { return value_; }
    std::exception_ptr exception() const override { return nullptr; }
};

// The future holds a pointer to state_base
template<typename T>
struct future {
    state_base<T>* state_;

    // Sync path: allocate immediate_state externally
    explicit future(T value)
        : state_(new immediate_state<T>(std::move(value)))
    {}

    // Coroutine path: state is at end of frame
    // (constructed via extra_storage_request)

    bool await_ready() const { return state_->ready(); }

    auto await_resume() {
        if (auto ep = state_->exception())
            std::rethrow_exception(ep);
        return std::move(state_->value());
    }
};
```

---

## Benefits

1. **Pay-per-use coroutines**: Sync implementations avoid coroutine frame allocation entirely

2. **Virtual interfaces work**: Same return type for both sync and async implementations

3. **No `std::variant`**: Polymorphism via virtual base, cleaner API, no visitor gymnastics

4. **Dynamic sizing**: The appended `T` can vary at runtime—the "variable-sized stack object" insight

5. **Composable**: Can combine with existing `frame_allocator` machinery for pooling

---

## The Variable-Sized Stack Object Insight

Regular functions have stack sizes determined at compile time:

```cpp
void buffer_stuffer(std::span<buffer> const& bufs) {
    thing x[bufs.size()];  // Doesn't work in C++
}
```

But coroutines can allocate variable amounts through `operator new`:

```cpp
task<void> buffer_stuffer(std::span<buffer> const& bufs) {
    // Extra storage request set BEFORE coroutine creation
    // can specify runtime-determined size
    co_return;
}
```

This enables runtime-sized "stack" allocations within the coroutine's lifetime.

---

## Safety Guarantees

From [the-window.md](the-window.md):

- TLS is only read in `operator new`
- TLS is set by the currently-running coroutine/caller
- Single-threaded: only one coroutine runs at a time per thread
- No dangling: the code that set TLS is still on the stack when `operator new` reads it

**Narrow, deterministic window. Clean.**
