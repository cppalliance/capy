# Strand: Why Per-Strand Implementation

A strand has two reasonable internal designs. The simpler one pools
serialization state across strands; the correct one allocates state
per-strand. Capy uses the per-strand design. This document explains why
the simpler design is wrong and what the per-strand design costs.

## The previous design

Capy's original strand service held a fixed array of `strand_impl`
objects, 211 slots, allocated inline in the service and never freed
individually. When a user constructed a new strand, the service
incremented a counter and returned a pointer to `impls_[counter % 211]`.

```cpp
strand_impl impls_[211];
std::size_t salt_;

strand_impl* get_implementation()
{
    std::lock_guard lock(mutex_);
    return &impls_[salt_++ % 211];
}
```

This is pure round-robin: the 1st strand gets slot 0, the 212th strand
gets slot 0 again. Two strands that map to the same slot share the same
`strand_impl` object.

Each `strand_impl` holds:

- a mutex (`mutex_`)
- a pending operation queue (`pending_`)
- a locked flag (`locked_`)
- the executor identity used by whichever invoker is currently
  dispatching

Two strands that share a slot share all of this.

## What sharing actually shares

Sharing a mutex is not inherently a problem. Two strands that hold the
same mutex contend on push and pop operations, which are brief. They
still proceed independently afterward.

Sharing a queue and a locked flag is a different matter. Those are the
state machine that determines which work runs, in what order, and
through which executor. When two logically independent strands share
this state, the following become possible:

**Cross-strand blocking.** Strand A is mid-dispatch, so `locked_` is
true. Strand B posts a new operation. B's post sees `locked_` already
set and adds its work to the shared queue without posting a new
invoker. B's work now waits behind A's entire dispatch cycle, even
though A and B are supposed to be independent.

**Wrong executor dispatch.** The invoker that won the unlocked-to-locked
transition captures the executor of the strand that triggered it. Call
this strand A. If strand B later enqueues work into the shared state,
that work runs through A's executor, not B's. For strands that wrap
the same underlying thread pool, this is invisible. For strands that
wrap different executor layers (a metrics wrapper, a type-erased
`any_executor`, a test shim), operations execute through the wrong
executor, violating the invariants the user associated with B's
executor.

**False equality.** `operator==` on two distinct strands returns true
when they map to the same slot, because equality is defined as pointer
identity of the impl.

## Why per-strand is the right choice

The correctness argument is simple: strand isolation is part of the
contract. The word "strand" implies a serialization domain that is
independent of all other strands. A user who writes code against two
strands is justified in expecting that progress on one does not depend
on progress on the other, and that work posted to one runs through
that strand's executor, not a neighbor's.

The pooled design cannot provide this guarantee for more than 211
strands from the same context.

One possible response is randomization: instead of pure round-robin,
use a hash of the strand's address mixed with a salt counter. This
spreads collisions across time so that (0, 211), (1, 212) are no longer
the deterministic collision pairs. It does not remove collisions. With
1000 strands from one context, roughly five collision pairs exist
somewhere in the set. The bug surface is narrower and harder to trigger
reproducibly, but the class of bug is identical.

Randomization fixed a performance symptom (deterministic starvation)
without fixing the correctness problem (shared state between independent
strands). Treating these as the same fix is a category error.

The per-strand design removes the impl pool entirely. Each strand
allocates its own `strand_impl` via `make_shared`. Two strands never
share a queue, a locked flag, or an invoker. Isolation is unconditional.

The mutex pool stays. 193 mutexes for any number of strands is a real
saving over allocating a mutex per strand. Unlike the impl pool, mutex
sharing has no semantic consequence: the critical sections guarded by
the mutex cover only push/pop and the locked flag check. Two strands
that briefly contend on a shared mutex wait for each other's push/pop
then proceed independently. No state crosses the boundary.

The key insight is that isolation and contention are not the same
problem. The impl pool conflated them. Removing the impl pool eliminates
the isolation problem; keeping the mutex pool manages the contention
cost without reintroducing the isolation problem.

## What the per-strand design costs

**One allocation per strand.** `make_shared<strand_impl>` allocates
roughly 80-96 bytes on typical allocators with per-thread arenas
(glibc, jemalloc, tcmalloc). For any strand that posts at least one
operation, this is negligible against the work being dispatched.

**One pointer of additional size per strand handle.** The strand object
holds a `shared_ptr<strand_impl>` rather than a raw pointer. A
`shared_ptr` is two pointers wide; a raw pointer is one. Strand objects
grow by one pointer (typically 8 bytes).

**Two atomic refcount operations per invoker creation/destruction.** The
invoker coroutine frame holds a copy of the `shared_ptr`, so the
reference count increments when the invoker starts and decrements when
it finishes. These are not on the hot post path; they happen at the
unlocked-to-locked transition (once per dispatch batch), not on every
enqueue.

The mutex pool bounds memory growth at 193 mutexes regardless of how
many strands exist. A program that creates 10,000 strands does not get
10,000 mutexes; it gets at most 193.

## Tradeoffs we did not take

**Per-strand mutex.** Allocating a mutex per strand would eliminate the
mutex pool entirely and remove all cross-strand contention. The cost is
roughly 40 extra bytes per strand. The benefit is marginal: the
critical sections that use the pool mutex are brief, and contention
between unrelated strands is unlikely in practice. This option remains
open if benchmarks show real contention under specific workloads.

The chosen design (per-strand impl, shared mutex pool) matches the
strategy used by current executor-aware strand implementations in the
C++ library space, which provides confidence that the tradeoffs are
well understood.
