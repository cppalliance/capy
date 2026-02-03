|         | [`master`](https://github.com/cppalliance/capy/tree/master) | [`develop`](https://github.com/cppalliance/capy/tree/develop) |
|---------|-------------------------------------------------------------|---------------------------------------------------------------|
| Docs    | [![Documentation](https://img.shields.io/badge/docs-master-brightgreen.svg)](https://master.capy.cpp.al/) | [![Documentation](https://img.shields.io/badge/docs-develop-brightgreen.svg)](https://develop.capy.cpp.al/) |
| GitHub Actions | [![CI](https://github.com/cppalliance/capy/actions/workflows/ci.yml/badge.svg?branch=master)](https://github.com/cppalliance/capy/actions/workflows/ci.yml?query=branch%3Amaster) | [![CI](https://github.com/cppalliance/capy/actions/workflows/ci.yml/badge.svg?branch=develop)](https://github.com/cppalliance/capy/actions/workflows/ci.yml?query=branch%3Adevelop) |
| Drone   | [![Build Status](https://drone.cpp.al/api/badges/cppalliance/capy/status.svg?ref=refs/heads/master)](https://drone.cpp.al/cppalliance/capy/branches) | [![Build Status](https://drone.cpp.al/api/badges/cppalliance/capy/status.svg?ref=refs/heads/develop)](https://drone.cpp.al/cppalliance/capy/branches) |
| Codecov | [![codecov](https://codecov.io/gh/cppalliance/capy/branch/master/graph/badge.svg)](https://app.codecov.io/gh/cppalliance/capy/tree/master) | [![codecov](https://codecov.io/gh/cppalliance/capy/branch/develop/graph/badge.svg)](https://app.codecov.io/gh/cppalliance/capy/tree/develop) |

# Boost.Capy

This library provides facilities which use C++20 coroutines to perform I/O. It is not a networking library, yet it is the perfect foundation upon which networking libraries, or any libraries that perform I/O, may be built. It introduces concepts for representing buffers of data, and moving those buffers of data through processing pipelines driven entirely by C++20 coroutines. The design of the library is based on one simple observation from Peter Dimov:

> _An API designed from the ground up to use C++20 coroutines can achieve performance and ergonomics which cannot otherwise be obtained._

## Quick Start

Clone and build with CMake:

```bash
git clone https://github.com/cppalliance/capy.git
cd capy
cmake --preset standalone
cmake --build --preset standalone
```

The library is built to `out/standalone/`.

## Related Libraries

Capy is the foundation for [Corosio](https://github.com/cppalliance/corosio), a complete networking library built on the facilities that Capy offers. We intend to propose this library as the successor to the immensely popular Boost.Asio library.

- Documentation: https://master.corosio.cpp.al/corosio/index.html

Capy is also the foundation for [Http](https://github.com/cppalliance/http). The Http library uses Capy, yet does not use Corosio. That is because Http is "Sans/IO." It provides the algorithms and data structures which implement the HTTP protocol at a high level, while remaining agnostic to the particular network implementation. This is possible thanks to the powerful stream abstractions which Capy offers.

- Documentation: https://master.http.cpp.al/http/index.html

Capy is also used by [Beast2](https://github.com/cppalliance/beast2), which uses the Capy, Corosio, and Http libraries to implement high-level HTTP servers written in a C++ version of Express JS routers. This is the successor to Boost.Beast (note: Boost.Beast will continue to be maintained as its own separate library, Beast2 is new).

The Beast2 family of libraries includes:

- **Capy** — The foundation of I/O.
- **Corosio** — Coroutine-only portable networking. This is the successor to Boost.Asio.
- **Http** — Sans-I/O HTTP/1.1. This is a high-level library: servers, clients, Express JS middleware.
- **Websocket** — Sans-I/O Websocket. This is also a high-level library.
- **Beast2** — High-level HTTP and WebSocket servers. Express.js-style routing, multithreaded, idiomatic C++.
- **Burl** — High-level HTTP client. The features of curl, the ergonomics of coroutines, and the design sensibility of Python Requests.

Currently, the C++ Standard does not deliver facilities optimized for networking I/O. We believe that Capy should become a standard library component to fill this gap. Our first paper based on Capy introduces the _IoAwaitable_ family of concepts:

- Paper: https://github.com/cppalliance/wg21-papers/blob/master/source/d4003-io-awaitables.md

## The Beman Way

We are bringing Capy to Boost because this is what Boost was created for. The project exists to incubate high-quality libraries destined for standardization. Beman Dawes founded Boost on the principle that the best path to the standard is through proven practice: build it, ship it, let users depend on it, learn from real-world feedback, then propose standardization. Smart pointers, regular expressions, filesystem, threading primitives—all followed this path from Boost to the standard library.

Capy represents Boost returning to its role as a leader in C++ standardization efforts. The library addresses a real gap in C++26: there is no standard foundation for coroutine-based I/O. Rather than waiting for a committee to design something in the abstract, or adapting networking to a framework built for different requirements, Capy takes the proven approach. It exists. It works. It powers real networking code today. Now it needs the scrutiny and refinement that only the Boost review process can provide.

## The Problem Capy Solves

When an I/O operation completes, the operating system wakes up some thread, such as a completion port thread, an epoll reactor, or an io_uring worker. Without affinity tracking, your coroutine resumes on that arbitrary thread, forcing you to add synchronization everywhere or risk data races. This is the fundamental problem that coroutine-based networking must solve.

Capy's answer is the _IoAwaitable_ protocol. When you launch a coroutine with a designated executor, every child coroutine inherits that executor affinity automatically. Execution context flows forward through `co_await` chains, not backward through P3826 queries, to ensure every coroutine in the chain runs in the same context. When I/O completes on some OS thread, the _IoAwaitable_ protocol ensures your coroutine resumes on its designated executor. The data flow is explicit and testable. There are no thread-local globals, no implicit context, no surprises. Capy's `task` type uses the compiler to enforce invariants.

Cancellation follows the same forward-propagation model. Stop tokens flow forward from the launch of a coroutine chain alongside the execution context, to arrive at the platform API boundary, providing a uniform cancellation interface across all operations.

Frame allocation is where coroutine overhead traditionally hurts performance. Capy addresses this with thread-local recycling pools that achieve zero steady-state allocations after warmup. The coroutine launch site controls allocation policy, enabling per-deployment customization: bounded pools for real-time systems, per-tenant budgets for multi-tenant servers, or tracking allocators for debugging.

Buffer handling is essential for networking, and Asio's twenty five years of experience showed us how. Capy provides buffer sequence algorithms: think `std::ranges` but for buffers. These are the vocabulary types and operations that networking code needs: slicing, copying, concatenating, and iterating over discontiguous memory. One million scatter/gather buffers, if you will. The design is driven by real-world usage, not theoretical completeness.

Capy is opinionated on the things that matter for I/O:

- An executor model for coroutine affinity and completion dispatch
- Stop token integration: uniform cancellation, always available
- Allocator control over frame allocation with zero-overhead recycling
- Forward propagation of the full context through every `co_await`
- A `task` type that enforces the _IoAwaitable_ protocol at compile time
- Composition primitives for launching and coordinating coroutines
- A strand for safe concurrency without mutexes
- Buffer sequences: `std::ranges` for untyped bytes
- Type erasure by default: no combinatorial explosion of templates

## Proven Through Boost.Corosio

Capy is not speculation. It powers Boost.Corosio, a coroutine-first networking library that we are developing alongside Capy. Corosio provides real sockets, acceptors, TLS streams, timers, DNS resolution, and multiple implementations of SSL streams, all built on Capy's foundation. This is the successor library to the incredibly popular Boost.Asio. It demonstrates what networking could look like if designed for coroutines from the start rather than adapted from callback-based models.

The standardization strategy follows from this layering. Capy is the foundation piece that belongs in the standard: executor model, task types, buffer algorithms, cancellation integration. These are stable abstractions that networking libraries can build upon. Corosio, the networking piece, can remain outside the standard to mitigate risk: sockets and protocol implementations typically experience difficulty achieving consensus even after years of committee attention. Corosio can mature externally where it can evolve based on user feedback, while Capy provides the stable foundation that the standard library lacks.

## Requirements

- CMake 3.20 or later
- C++20 compiler (GCC 12+, Clang 17+, MSVC 14.34+)
- Ninja (recommended) or other CMake generator

## License

Distributed under the Boost Software License, Version 1.0.
(See accompanying file [LICENSE_1_0.txt](LICENSE_1_0.txt) or copy at
https://www.boost.org/LICENSE_1_0.txt)
