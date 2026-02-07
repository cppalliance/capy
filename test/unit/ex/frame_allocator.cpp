//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Test that header file is self-contained.
#include <boost/capy/ex/frame_allocator.hpp>

/*
Frame Allocator System
======================

This module implements coroutine frame allocation using std::pmr::memory_resource
as the polymorphic allocator interface. The system propagates a single allocator
down the entire coroutine call tree using thread-local storage (TLS).

Allocation Flow
---------------

1. User calls run_async(ex) or run_async(ex, allocator)
2. run_async creates a trampoline coroutine whose promise constructor:
   - For default: calls get_recycling_frame_allocator() to get recycling_memory_resource*
   - For memory_resource*: stores the pointer directly
   - For standard Allocator: wraps it in frame_memory_resource<Alloc> stored in promise
   - Sets current_frame_allocator() = &resource (or mr directly)
3. Trampoline resumes the user's task
4. task::promise_type (via io_awaitable_support) provides operator new/delete
   that call current_frame_allocator()->allocate/deallocate
5. task::initial_suspend captures TLS into promise via set_frame_allocator()
6. task::initial_suspend::await_resume restores TLS when body starts
7. When task awaits a child, transform_awaiter::await_resume restores TLS
8. Child tasks repeat steps 4-7, inheriting the same allocator

Key Components
--------------

current_frame_allocator()
    Thread-local std::pmr::memory_resource* set by trampoline before any task
    allocation. Tasks read this in operator new and capture it in initial_suspend.

frame_memory_resource<Alloc>
    Adapts a standard Allocator to memory_resource interface. Rebinds to std::byte.
    Stored by value in trampoline's promise, ensuring lifetime exceeds all tasks.

recycling_memory_resource
    Default allocator with thread-local and global pools for frame recycling.
    Accessed via get_recycling_memory_resource() which returns pointer to static.

io_awaitable_support
    CRTP mixin providing:
    - operator new/delete using current_frame_allocator()
    - alloc_ member to store captured allocator pointer
    - set_frame_allocator()/frame_allocator() accessors

trampoline
    Launcher coroutine that:
    - Is allocated BEFORE the user task (due to C++17 postfix evaluation order)
    - Stores the allocator (wrapper or pointer) in its promise
    - Sets TLS in promise constructor before task is created
    - Destroys task after completion, then self-destructs

Gotchas
-------

TLS must be set before task frame allocation:
    The trampoline's promise constructor sets TLS. This happens during
    make_trampoline() before the user's task is even created.

Trampoline uses default allocation:
    The trampoline coroutine itself is allocated with global new because
    TLS isn't set until its promise constructor runs. This is intentional.

TLS restoration on resume:
    After awaiting a child, the parent's TLS may have been changed by the child.
    transform_awaiter::await_resume restores parent's allocator from its promise.

memory_resource* lifetime:
    When passing memory_resource* directly, the user is responsible for ensuring
    it outlives all tasks. This matches std::pmr conventions.

Allocator value types are safe:
    Standard Allocator types are wrapped in frame_memory_resource and stored
    in the trampoline's promise, which is destroyed LAST (after all tasks).

Lifetime Guarantees
-------------------

Destruction order (LIFO):
1. Task completes, final_suspend returns to trampoline
2. Trampoline invokes handlers
3. Trampoline destroys task handle (task frame deallocated via TLS allocator)
4. Trampoline's final_suspend is suspend_never, so it self-destructs
5. Trampoline's promise (containing frame_memory_resource) destroyed last

This guarantees frame_memory_resource<Alloc> outlives all frames it allocated.
*/
