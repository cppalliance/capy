# Strand

Each strand allocates a private serialization state via
`shared_ptr<strand_impl>`. Strands sharing an execution context borrow
mutexes from a 193-entry pool but never share their queues, `locked_`
flag, or invoker. This document is the design contract; see
`strand-rationale.md` for the motivation.

## Goals

- Strand isolation is absolute, not probabilistic. Two distinct strands
  never share a queue, a `locked_` flag, or a dispatcher executor.
- Public API of `strand<Ex>` is unchanged: same operations, same
  equality semantics, same `running_in_this_thread`.
- Construction cost: one `std::make_shared` per strand.
- Capy's existing performance optimizations are preserved: the
  `strand_queue` per-post wrapper recycler stays per-impl; the invoker
  coroutine frame cache moves to the service.

## Non-goals

- Per-strand mutex. The shared mutex pool stays. Revisit if benchmarks
  show contention from shared mutexes.
- Performance tuning of the mutex pool size or salt function.
- Lock-free hot path. The per-impl mutex is taken under
  `post`/`dispatch` for queue mutation and `locked_` flag check.
- Allocator plumbing for `allocate_shared`. Default-construct now; can
  add an allocator parameter later without changing this design.
- Changes to `strand_queue`. Its free-list stays; lifetime is bounded by
  the impl that owns it.

## Design

### Data structures

```cpp
struct strand_impl
    : intrusive_list<strand_impl>::node
{
    std::mutex* mutex_ = nullptr;          // borrowed from service pool
    strand_queue pending_;
    bool locked_ = false;
    std::atomic<std::thread::id> dispatch_thread_{};

    std::atomic<strand_service_impl*> service_{nullptr};

    ~strand_impl();
};

class strand_service_impl : public strand_service
{
    static constexpr std::size_t num_mutexes = 193;

    std::mutex mutex_;
    std::size_t salt_ = 0;
    std::shared_ptr<std::mutex> mutexes_[num_mutexes];
    intrusive_list<strand_impl> impl_list_;
    std::atomic<void*> invoker_frame_cache_{nullptr};
};

template<typename Ex>
class strand
{
    std::shared_ptr<detail::strand_impl> impl_;
    Ex ex_;
};
```

Key design choices:

- Each strand owns its `strand_impl` via `shared_ptr` (no pooling of
  impls).
- `strand` holds `shared_ptr<strand_impl>` rather than a raw pointer
  (size grows by one pointer).
- The invoker frame cache lives on the service, not the impl. The cache
  slot always points at a structure that lives for the execution
  context's lifetime, removing the lifetime hazard that would otherwise
  affect per-strand impls.
- `strand_impl` holds a borrowed `mutex_` pointer, an intrusive list
  base class (via `intrusive_list<strand_impl>::node`), and a
  back-pointer to the service.
- The service holds a 193-entry mutex pool, the head of the live-impl
  linked list, and the invoker frame cache slot.
- 193 is a prime large enough that hash collisions are rare in practice
  while keeping the static mutex array small.

### Public detail-header surface

```cpp
class BOOST_CAPY_DECL strand_service
    : public execution_context::service
{
public:
    virtual ~strand_service();

    // Returns shared_ptr instead of raw pointer.
    virtual std::shared_ptr<strand_impl>
    create_implementation() = 0;

    static bool
    running_in_this_thread(strand_impl& impl) noexcept;

    // Takes shared_ptr by const-ref so post_invoker can capture
    // lifetime on the unlocked-to-locked transition without paying an
    // atomic refcount on every post when the invoker is already running.
    static std::coroutine_handle<>
    dispatch(
        std::shared_ptr<strand_impl> const& impl,
        executor_ref ex,
        std::coroutine_handle<> h);

    static void
    post(
        std::shared_ptr<strand_impl> const& impl,
        executor_ref ex,
        std::coroutine_handle<> h);
};
```

The strand constructor calls `create_implementation()` and stores the
returned `shared_ptr`. Public-API surface of `strand<Ex>` does not
change. The `running_in_this_thread` query is non-mutating and does
not extend lifetime, so it stays as `strand_impl&`.

### Construction

```cpp
std::shared_ptr<strand_impl>
strand_service_impl::create_implementation()
{
    auto new_impl = std::make_shared<strand_impl>();

    std::lock_guard lock(mutex_);

    std::size_t s = salt_++;
    std::size_t idx = reinterpret_cast<std::size_t>(new_impl.get());
    idx += idx >> 3;
    idx ^= s + 0x9e3779b9 + (idx << 6) + (idx >> 2);
    idx %= num_mutexes;
    if(!mutexes_[idx])
        mutexes_[idx] = std::make_shared<std::mutex>();
    new_impl->mutex_ = mutexes_[idx].get();

    impl_list_.push_back(new_impl.get());
    new_impl->service_ = this;

    return new_impl;
}
```

The hash mixes the impl's address with a monotonic salt and the golden
ratio constant. The salt prevents deterministic collision sequences
when the allocator returns predictable addresses; the address bits
spread otherwise-correlated allocations. Mutex slots are allocated
lazily: a program that creates few strands never instantiates all 193
mutexes. The impl is appended to `impl_list_` via `push_back`; order
does not matter since shutdown drains the entire list.

### Dispatch / post

State machine is unchanged from the previous design. The key
differences:

- `enqueue`, `dispatch_pending`, `try_unlock` operate on a strand's own
  `pending_` and `locked_` (no cross-strand sharing).
- The mutex they take is `*impl.mutex_`, which may be shared with other
  impls that hashed to the same pool slot. Critical sections cover only
  brief queue push/pop and the `locked_` flag check.
- The static `post`/`dispatch` entry points take
  `shared_ptr<strand_impl> const&`. When the unlocked-to-locked
  transition wins, they copy the shared_ptr into `post_invoker`, which
  passes it as the coroutine parameter held in the coroutine frame.
  That keeps the impl alive for the duration of the dispatch cycle,
  even if the user drops their last strand handle. When the transition
  does not win (work is enqueued onto an already-running invoker), no
  shared_ptr copy is made. The existing invoker's frame already holds
  a reference. The hot path adds zero atomic refcount operations versus
  the previous raw-pointer code.

### Invoker frame allocation

```cpp
void* operator new(std::size_t n, strand_impl& impl)
{
    auto* svc = impl.service_;
    constexpr auto A = alignof(strand_service_impl*);
    std::size_t padded = (n + A - 1) & ~(A - 1);
    std::size_t total = padded + sizeof(strand_service_impl*);

    void* p = svc->invoker_frame_cache_.exchange(
        nullptr, std::memory_order_acquire);
    if(!p || p == kCacheClosed)
        p = ::operator new(total);

    *reinterpret_cast<strand_service_impl**>(
        static_cast<char*>(p) + padded) = svc;
    return p;
}

void operator delete(void* p, std::size_t n) noexcept
{
    constexpr auto A = alignof(strand_service_impl*);
    std::size_t padded = (n + A - 1) & ~(A - 1);
    auto* svc = *reinterpret_cast<strand_service_impl**>(
        static_cast<char*>(p) + padded);

    void* expected = nullptr;
    if(!svc->invoker_frame_cache_.compare_exchange_strong(
            expected, p, std::memory_order_release))
        ::operator delete(p);
}
```

The trailer holds a service pointer (lifetime: execution context),
not an impl pointer (lifetime: per-strand). The invoker's `make_invoker`
parameter is a shared_ptr stored in the coroutine frame; that one copy
keeps the impl alive past any user-side strand drop. `operator delete`
reads only the trailer (service-scoped), so impl may be dead at delete
time without consequence.

### Destruction

```cpp
strand_impl::~strand_impl()
{
    auto* svc = service_.load(std::memory_order_acquire);
    if(!svc) return;
    std::lock_guard lock(svc->mutex_);
    svc->impl_list_.remove(this);
}
```

`~strand_queue` (already implemented) destroys any pending wrappers
without resuming them. That covers the case where work was queued but
the inner executor never invoked the invoker before context teardown.

### Shutdown

```cpp
void strand_service_impl::shutdown() override
{
    std::lock_guard lock(mutex_);
    while(auto* p = impl_list_.pop_front())
    {
        std::lock_guard impl_lock(*p->mutex_);
        p->locked_ = true;
        p->service_.store(nullptr, std::memory_order_release);
    }

    void* fp = invoker_frame_cache_.exchange(
        kCacheClosed, std::memory_order_acq_rel);
    if(fp) ::operator delete(fp);
}
```

After shutdown, user-held strands still own their impls via
`shared_ptr`. When they drop, `~strand_impl` sees `service_ == nullptr`
and short-circuits without touching service state, which may have been
freed.

### Lifetime cases

1. **User drops strand, no work in flight.** Last `shared_ptr` drops;
   `~strand_impl` unlinks; impl freed. `~strand_queue` discards any
   wrappers (edge case only; `enqueue` posts the invoker on the
   unlocked-to-locked transition, so wrappers are normally drained
   before the strand becomes inactive).

2. **User drops strand, invoker still running.** The invoker promise
   holds the last `shared_ptr`; impl stays alive; invoker drains and
   exits at `final_suspend`. Frame deletion order: promise destructor
   (releases shared_ptr, runs `~strand_impl`), then `operator delete`
   (recycles frame to service cache; service is still alive). Safe.

3. **Service shutdown while user holds strand.** Shutdown unlinks the
   impl from the list, marks it locked, and nulls its `service_`
   back-pointer. When the user later drops the strand, `~strand_impl`
   sees `service_ == nullptr` and short-circuits without touching
   service state, which may have been freed.

4. **Service shutdown with invoker queued but never invoked.** The
   inner executor's destructor drops the queued continuation; the
   coroutine handle is never destroyed; the promise's `shared_ptr`
   never releases; impl and frame leak. Pre-existing behavior, not
   introduced by this design.

5. **Service shutdown with invoker mid-execution.** The invoker accesses
   the service only via the trailer in `operator delete` (cache-slot
   recycle). Shutdown sets the cache to `kCacheClosed`; concurrent
   invokers see the sentinel and call `::operator delete` instead. The
   service object itself must outlive any in-flight invoker. Capy's
   `execution_context` teardown is responsible for stopping the inner
   executor (which drains queued continuations) before destroying
   services. This matches the contract the previous implementation
   relied on.

### Move semantics

The documented contract is unchanged: "a moved-from strand is only safe
to destroy or reassign." The moved-from `shared_ptr` is nullptr; calls
on it dereference nullptr, which enforces the contract rather than
merely documenting it. The previous design left the moved-from strand
silently pointing at the same impl as the moved-to strand.
