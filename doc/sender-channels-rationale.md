# Design Rationale: Sender Channels and I/O Return Types

## Context

This document captures the design space and trade-offs around how
asynchronous I/O completion results interact with the three-channel model
of `std::execution` (P2300). It is intended for LEWG consumption as
background for decisions concerning `std::execution::task`, the SG4
networking mandate, and the relationship between senders and coroutines.

## Background

### The Three-Channel Model

P2300 routes asynchronous results through three channels:

- `set_value(rcvr, args...)` - the operation succeeded
- `set_error(rcvr, err)` - the operation failed
- `set_stopped(rcvr)` - the operation was cancelled

Sender algorithms such as `upon_error`, `let_error`, `upon_stopped`,
`when_all`, and `retry` dispatch on which channel fires. This is the
structural basis of sender composition.

### The I/O Completion Model

Every I/O completion delivers a tuple of values, typically
`(error_code, size_t)` or `(error_code, T)`. Both elements are always
present and always meaningful. A `read` that transfers 47 bytes before a
connection reset delivers `(connection_reset, 47)` - the 47 bytes are
valid data, and the error code explains what happened next.

The `error_code` in I/O is a status report, not a boolean. Non-trivial
algorithms must distinguish among many different conditions (EOF, partial
transfer, SSL truncation, cancellation with accumulated progress), some
of which are important for security, others for correctness.

This model has been stable across 25 years of Asio-family practice and
reflects how operating systems deliver I/O completions.

### The Terminology Problem

The type is named `error_code`, but it carries EOF, cancellation, and
partial success. The sender channel is named `set_error`, but it means
"this is a failure; discard everything else." The naming collision
between `error_code` (which is a status) and `set_error` (which is a
disposition) creates a persistent source of confusion in design
discussions.

A more precise vocabulary: I/O operations produce *status codes*. Some
statuses will eventually become errors at higher layers, but the
classification requires application context that the I/O layer does not
possess.

## The Core Observation: I/O as Complicated Success

I/O completions are *complicated success*. EOF with partial data,
cancellation with accumulated progress, a reset after bytes already
transferred - these are not failures. They are outcomes the caller must
inspect. The sender model offers three simple channels: one for a value,
one for an error, one for a stop signal.

At the I/O level, there are practically no strict errors - nothing that
unconditionally mandates killing all subsequent operations in the
pipeline. There are only different possible outcomes of interacting with
buffers and ports, and it is only the next operation in the pipeline that
can interpret the result as successful or not.

This leads to a structural observation: I/O uses only the value channel.
The error and stopped channels are unreachable for I/O senders. The
entire I/O stack lives in the success row:

```
Level:      I/O        I/O-level user algo   classification      program logic

success:   [read] -----> [ process ] ------> [classify] -------> [ process success ]
error:     (unused)                          (unused)     \----> [ process error   ]
cancel:    (unused)                          (unused)      \---> [ process cancel  ]
```

The error and stopped channels only become reachable after the
application classifies the I/O status - and that classification requires
context the I/O layer does not have.

## Positions on Channel Routing

The central question: given a coroutine or sender that produces
`(error_code, T)`, how should the result enter the sender pipeline?

### Position A: Everything Through set_value

Route the entire tuple `(error_code, T)` through `set_value`. Nothing is
lost; the downstream handler receives both values and classifies at
runtime.

```cpp
// The I/O sender always completes via set_value
set_value(std::move(rcvr), ec, n);
```

**Arguments for:**

1. No data is destroyed. The full tuple is preserved for the next stage.
2. Matches the nature of I/O: everything is status, and classification
   is the caller's responsibility.
3. This is how coroutine-native I/O already works: `auto [ec, n] =
   co_await stream.read_some(buf);` - the caller receives both values
   and branches.

**Arguments against:**

1. `upon_error`, `let_error`, and `upon_stopped` never fire for I/O
   senders. They are dead code in the pipeline.
2. Generic algorithms that dispatch on channels cannot distinguish I/O
   failure from I/O success. `when_all` cannot cancel siblings on I/O
   failure because the failure arrived as `set_value`. `retry` never
   retries because it only sees `set_error`.
3. The composability that justified having three channels is lost for the
   largest async domain. If I/O bypasses the channels, the generic
   error-handling machinery does not compose with I/O senders.

### Position B: Split on error_code (The Dimov Mapping)

Classify the error_code and route to different channels:

```cpp
if (!ec)
    set_value(std::move(rcvr), n);
else if (ec == error::canceled)
    set_stopped(std::move(rcvr));
else
    set_error(std::move(rcvr), ec);
```

This is the "default mapping" - a classification function that converts
I/O status into the three-channel model.

**Arguments for:**

1. Enables generic algorithms: `retry` fires on error, `when_all`
   cancels siblings on failure, `upon_error` reaches error handlers.
2. Provides a standard recipe that library authors can apply uniformly.
3. Custom mappings can be substituted for domain-specific classification
   (e.g., treating `ssl::stream_truncated` as success).

**Arguments against:**

1. **Data loss on error.** When `set_error(rcvr, ec)` fires, the
   partially transferred bytes are destroyed. A `read` that accumulated
   a partial HTTP body before a connection reset loses the body. In
   synchronous code, the equivalent `throw io_problem(ec, n)` preserves
   both values; the sender channel does not.
2. **Data loss on cancellation.** `set_stopped(rcvr)` carries zero
   values. Cancellation with accumulated progress (common in I/O) loses
   all accumulated data.
3. **Context-free classification.** The I/O layer does not know whether
   EOF is fatal or routine - that depends on the application. A
   classification function applied at the I/O layer forces a decision
   before the information needed to make it is available.
4. **The mapping itself is a tradeoff.** It works well for domains where
   errors are binary (worked or did not). I/O is the domain where errors
   carry data, and the mapping destroys that data.

### Position C: Exceptions as the Channel Switch

Within a `then` or coroutine, throw an exception to route from the value
channel to the error channel:

```cpp
int classify(std::pair<std::error_code, int> result) {
    if (result.first)
        throw std::system_error(result.first);
    return result.second;
}

auto pipeline = io_sender | then(classify) | upon_error(handle);
```

In `std::execution::task`, this is the only runtime mechanism for
switching from the value channel to the error channel.

**Arguments for:**

1. Uses C++'s native error-reporting mechanism.
2. Allows runtime classification with full context.
3. Works within the existing P2300 framework without new components.

**Arguments against:**

1. I/O is allergic to exceptions. Non-trivial I/O algorithms must
   distinguish among many different error conditions at runtime. Turning
   each into a thrown exception and catching it is a model mismatch:
   exception handling is designed for rare, unexpected failures, not for
   routine status inspection.
2. **Mandatory error-model translation.** The caller must convert
   between `error_code` and exceptions to make the channels function.
   This translation is friction, not composition.
3. **Data loss persists.** The thrown exception carries the error code
   but not the partial data. The bytes transferred before the error are
   still destroyed.
4. Every other major async framework propagates I/O errors through one
   mechanism. Requiring an adaptor to translate between error models is
   evidence of a model that does not serve I/O natively.

### Position D: let_value with Runtime Classification

Use `let_value` to receive the full tuple, classify at runtime, and
dispatch to different sender types:

```cpp
auto pipeline =
    do_read(sock, buf)
  | let_value([](std::pair<std::error_code, std::string> result) {
        auto [ec, body] = std::move(result);
        if (!ec || ec == error::eof)
            return just(std::move(body));
        else if (ec == error::canceled)
            return stopped();              // body destroyed
        else
            return just_error(ec);         // body destroyed
    })
  | retry(3)
  | when_all(other_work)
  | then(process);
```

**Arguments for:**

1. Solves the temporal inversion: the data is available when
   classification occurs.
2. The user writes the classification logic with full application
   context.
3. Preserves the ability to dispatch to all three channels.
4. Does not require exceptions.

**Arguments against:**

1. **Data loss persists.** `stopped()` destroys the body.
   `just_error(ec)` destroys the body. The synchronous equivalent keeps
   both values in scope on every path; the sender version does not.
2. **Error type pollution.** If `just_error(result)` sends the entire
   pair to preserve data, every downstream `upon_error` handler must
   know the error is really a tuple of partial results. A generic retry
   algorithm expects an error code, not a half-received HTTP body.
3. **Type-erasure requirements.** The different return types
   (`just(body)`, `just_error(ec)`, `stopped()`) must unify. Existing
   P2300 implementations lack the type-erased channel types needed to
   make this compile without additional machinery.
4. **Structural overhead.** In coroutine-native code, this
   classification is an `if` statement. In senders, it is an explicit
   structural adaptor that the framework demands. Turning control flow
   into data is the CPS transform, and the cost is visible.

### Position E: Do Not Route I/O Through Senders

I/O should not go through sender channels at all. Coroutine-native I/O
returns the tuple directly to the caller, who classifies with full
context using ordinary control flow:

```cpp
auto [ec, n] = co_await stream.read_some(buf);
if (!ec) { /* success */ }
else if (ec == error::eof) { /* end of stream */ }
else if (ec == error::canceled) { /* cancelled */ }
else { /* inspect ec */ }
```

When the coroutine has classified the result, it feeds clean values into
the sender pipeline at the application boundary:

```cpp
auto pipeline =
    my_io_coroutine()   // uses only set_value, returns clean result
  | translate_to_errors // user-written classification
  | regular_SR_logic;
```

**Arguments for:**

1. No data loss. The full tuple is in scope on every path.
2. Classification happens with full application context, using ordinary
   C++ control flow.
3. Each domain uses its own tools: coroutines for I/O, senders for
   compute and structured concurrency, meeting at the application
   boundary where the developer has context.
4. No mandatory error-model translation. No exceptions required for
   channel switching.
5. The cost of classification is paid once, at the boundary, where the
   coroutine's complicated success resolves into what a sender can
   consume properly.

**Arguments against:**

1. Requires accepting that P2300 is not the universal async model.
2. The SG4 poll (SF:5/F:5/N:1/A:0/SA:1) mandates that networking
   operations be senders. This position contradicts that mandate.
3. Two async models in the standard increase learning burden and
   ecosystem fragmentation.
4. Integration points between coroutine-native I/O and sender pipelines
   must be carefully designed.

## The Classification Problem

All positions except E face a structural timing problem. In senders, the
channel must be selected when the I/O operation completes. But the
classification of an I/O status into success, error, or cancellation
requires application context that is only available downstream.

```
Sender:   [IO completes] -> [channel chosen] -> ... -> [context available]
                                  ^                            ^
                             decision here              information here

Coroutine: [IO completes] -> [context available] -> [decision made]
                                  ^                        ^
                             information here         decision here
```

In senders, the decision precedes the information. In coroutines, the
information precedes the decision. This is not a defect in either model;
it is a consequence of how each model structures control flow. The
sender model's channels were designed for domains where the producer
knows whether it succeeded. I/O is the domain where the producer does
not know - only the consumer can classify.

`let_value` (Position D) partially addresses the temporal inversion by
deferring classification until the data is available. However, the
channel costs (data loss on error and stopped paths) remain because the
channels themselves are structurally narrower than the I/O completion
tuple.

## The Composability Consequence

If I/O results travel through `set_value` (the only lossless option),
then generic sender algorithms cannot see I/O failures:

```cpp
auto pipeline =
    do_read(sock, buf)
  | then([](std::pair<std::error_code, std::size_t> result) {
        auto [ec, n] = result;
        // must classify here...
    })
  | upon_error([](auto e) {
        // NEVER CALLED - I/O errors are in the pair above
    })
  | upon_stopped([] {
        // NEVER CALLED - cancellation is in the pair above
    });
```

- `retry` does not retry because it watches `set_error`, which never fires.
- `when_all` does not cancel siblings on I/O failure because the failure
  arrived as `set_value`.
- `upon_error` and `upon_stopped` are unreachable.

P2300 is predicated on structured concurrency and composition. If users
must resort to hand-rolled solutions at each step where their sender
pipeline interacts with I/O, and cannot use the native channels, then
the question becomes: what are senders buying for their cost in the I/O
domain?

In synchronous code, the equivalent glue preserves everything:

```cpp
auto [ec, n] = lib::sync_read(sock, buf);
if (!ec || ec == error::eof)
    return convert_to_request(n);
else if (ec == error::canceled)
    throw kill_transaction();       // n still in scope
else
    throw io_problem(ec, n);        // both values survive
```

The synchronous version keeps all data on every path. The sender version
must choose between signaling (routing to the correct channel) and
preserving (keeping the partial data). It cannot have both.

## The co_yield with_error Question

P3950R0 proposes `co_yield with_error(ec)` as a mechanism for coroutines
to signal errors to the sender pipeline. This exists to serve the error
channel.

If I/O sends everything through `set_value` (as the analysis above
suggests it must to avoid data loss), then the error channel that
`co_yield with_error` serves is one that I/O never triggers. A language
change is being proposed to regularize a mechanism for a channel that the
primary async domain does not use.

This does not mean `co_yield with_error` is without purpose - it may
serve non-I/O async domains. But its existence is evidence of friction
at the coroutine-sender boundary, and its utility for I/O is limited.

## The Boundary Model

The analysis suggests a natural architectural boundary:

1. **I/O side.** Coroutine-native I/O returns `(status_code, T)` tuples.
   Classification happens in ordinary C++ using `if` statements with
   full application context. The entire I/O stack lives in the value
   channel (or outside of sender channels entirely).

2. **Classification boundary.** The application writer, who has context
   to distinguish success from failure, maps the complicated success
   into the three-channel model.

3. **Sender side.** Clean values, errors, and cancellation signals flow
   through sender algorithms that dispatch on channels. `retry`,
   `when_all`, and `upon_error` function as designed.

```
 coroutine-native I/O          boundary          sender pipeline
[read] -> [accumulate] -> [classify] -> [set_value / set_error / set_stopped]
                               ^
                     application context here
```

This model minimizes cost: classification is paid once, at the boundary,
with full context. The I/O side avoids data loss. The sender side gets
clean channel signals.

## The Missing Building Block

P2300 does not currently offer a dedicated component for classifying I/O
status into three channels. `let_value` can serve this role but requires
the user to manually construct a type-erased or variant sender for the
three possible outcomes. A dedicated classification adaptor - one that
receives a tuple, applies a user-provided mapping function, and routes
to the appropriate channel - would reduce the friction described above.

Whether such an adaptor should exist in the standard, or whether its
absence is evidence that I/O should not be routed through sender channels
at all, is an open question.

## Positions on std::execution::task

### Position F: Ship task with C++26

**Arguments for:**

1. `task` completes the sender/receiver story by providing a coroutine
   bridge.
2. Users expect a standard task type alongside `std::execution`.
3. Delay creates uncertainty for the ecosystem.

**Arguments against:**

1. The three gaps documented above (channel routing, co_yield with_error,
   classification) are all at the coroutine boundary that `task` defines.
   ABI locks them in.
2. The task type's author filed sixteen open concerns against it.
3. Allocator propagation through the coroutine frame remains unsolved.
4. No production user is harmed by deferral - every networking library
   ships its own task type.

### Position G: Defer task to C++29

Ship `std::execution` (it earned its place). Do not ship `task` - the
structural gaps are at the coroutine boundary and ABI locks them in.
C++29 forwarding was unanimous. Deferring costs nothing and preserves
the ability to fix the boundary design with the benefit of implementation
experience.

## Positions on the SG4 Networking Mandate

### Position H: Networking Must Use the Sender Model

SG4 polled (SF:5/F:5/N:1/A:0/SA:1) that every future networking
operation in the C++ standard must be a sender.

**Arguments for:**

1. A single async model reduces ecosystem fragmentation.
2. Sender composition algorithms (`when_all`, `retry`, etc.) provide
   structured concurrency guarantees.
3. The committee has invested years in P2300; networking should build
   on it.

**Arguments against:**

1. The mandate produced zero networking facilities in three years.
2. If I/O uses only one of three channels, the committee is mandating a
   model where two-thirds of the error machinery is dead code for the
   primary async domain.
3. The mandate forecloses exploration of coroutine-native I/O designs
   that may be better suited to the domain.
4. The mandate was adopted when the only perceived alternatives were
   Asio and P2300. New information (coroutine-native I/O libraries with
   implementation experience) was not available at the time of the poll.

### Position I: Explore Coroutine-Native I/O Alongside Senders

WG21 should explore coroutine-native I/O designs alongside sender-based
designs. Each domain deserves a solution optimized for its use case.

**Arguments for:**

1. C++20 coroutines are a language feature that WG21 standardized. They
   have received no I/O infrastructure despite being well-suited to the
   domain.
2. Implementation experience with coroutine-native I/O libraries
   demonstrates an elegant, performant system that the committee cannot
   currently evaluate due to the SG4 mandate.
3. Opening alternatives does not undo anything - no networking facility
   has shipped. It explains why networking stalled: the foundation does
   not fit the domain.
4. A study group's purpose is to study. Exploring a promising direction,
   even if the outcome is to discard it, is a moral responsibility.

**Arguments against:**

1. Admitting that networking may not be built on P2300 could be
   perceived as invalidating years of committee work.
2. Two async models increase complexity for users and implementers.

## Areas of Agreement

1. **I/O completions are complicated success.** I/O does not produce
   simple pass/fail results. It produces status codes with partial data
   that the caller must inspect. Two of three sender channels are unused
   for I/O.

2. **The full tuple must be preserved.** At the I/O level, both the
   status code and the transferred data are always meaningful. Any
   design that discards data on error or cancellation paths is
   suboptimal for I/O.

3. **Classification requires application context.** The I/O layer
   cannot determine whether a given status is a success, an error, or a
   cancellation signal. Only the application layer has the context to
   classify.

4. **RAII-style error handling (exceptions) is a poor fit for I/O.**
   I/O algorithms must routinely inspect and branch on many different
   status codes. Exception handling is designed for rare, unexpected
   failures, not for routine status inspection.

5. **A classification boundary exists.** There is a layer where
   complicated success transitions to all-or-nothing. Both models
   (sender and coroutine) must cross this boundary.

6. **User control is paramount.** The standard should give users
   options, not make decisions for them. Preserving data and letting the
   user discard it is more conservative than discarding data and
   preventing the user from recovering it.

7. **`std::execution` has earned its place.** The sender/receiver model
   provides value for domains where errors are binary and compile-time
   work graphs are useful (GPU compute, parallel algorithms).

## Areas of Partial Agreement

1. **I/O does not need `set_error`.** There is broad agreement that at
   the I/O level, `set_error` has no use. The disagreement is over
   whether this observation indicts the three-channel model or merely
   reflects that I/O is a special case of a general model.

2. **`co_yield with_error` is problematic.** There is agreement that it
   is a hack bridging an impedance mismatch between coroutines and
   senders. The disagreement is whether the hack is tolerable or
   evidence of a deeper design flaw.

3. **Deferring task is prudent.** There is partial alignment on not
   shipping `task` prematurely. The disagreement is whether deferral
   should be unconditional or contingent on resolving specific open
   issues.

4. **Generic algorithms have a "partial success" problem.** `when_all`
   fundamentally cannot decide what to do with partial success in any
   framework. The classification must happen before the combinator,
   regardless of senders or coroutines. The disagreement is whether this
   inherent complexity is a property of higher-level combinators
   generally or a specific consequence of the three-channel model.

## Areas of Disagreement

1. **Whether I/O being a special case is a problem.** One view holds
   that I/O is the largest async domain, and a model where two-thirds
   of the machinery is dead code for that domain is evidence of poor
   fit. The other view holds that a universal model is expected to have
   special cases, and the fact that I/O does not utilize all channels
   does not mean the framework is wrong.

2. **Whether the composability loss matters in practice.** One view
   holds that `retry`, `when_all`, and other generic algorithms being
   unreachable for I/O senders undermines the core value proposition of
   structured concurrency. The other view holds that the user can always
   write a more complicated glue component, just as they would in
   sequential code.

3. **Whether a dedicated classification adaptor is sufficient.** One
   view holds that P2300 should offer a building block for classifying
   I/O status into three channels, and that this would resolve the
   friction. The other view holds that any classification adaptor within
   the sender model inherits the channel-width limitations (data loss on
   error and stopped paths) and therefore cannot fully resolve the
   mismatch.

4. **Whether the SG4 mandate should be revisited.** One view holds that
   the mandate should stand and that I/O designs must integrate with
   senders. The other view holds that the mandate was adopted without
   the information now available from coroutine-native I/O
   implementation experience, and that it should be opened to
   exploration of alternatives.

5. **Whether "one universal async model" is achievable.** One view
   holds that different async domains (I/O, GPU compute, parallel
   algorithms) have fundamentally different characteristics and deserve
   solutions optimized for their use cases. The other view holds that a
   single, universal async model is necessary for ecosystem coherence
   and that the costs of accommodation are acceptable.

## Summary of Alternatives

| #   | Approach                        | Data preserved? | Channels work? | Exceptions needed? | Classification timing     |
| --- | ------------------------------- | --------------- | -------------- | ------------------ | ------------------------- |
| A   | Everything through set_value    | Yes             | No             | No                 | Deferred (downstream)     |
| B   | Dimov Mapping (split on ec)     | No              | Yes            | No                 | Immediate (at I/O layer)  |
| C   | Exceptions for channel switch   | No              | Yes            | Yes                | Deferred (in then)        |
| D   | let_value with classification   | Partial         | Yes            | No                 | Deferred (in let_value)   |
| E   | Coroutine-native, no channels   | Yes             | N/A            | No                 | Deferred (caller decides) |

The design decision hinges on which cost the committee finds least
acceptable: the loss of generic algorithm composability (Position A),
the loss of partial data on error and cancellation (Positions B, C, D),
or the acceptance that I/O should not be routed through sender channels
(Position E).
