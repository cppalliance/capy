# Combinator Gap Analysis: Coroutine-Native vs. Sender/Receiver

March 2026.

---

## Scope

This document compares the combinator capabilities of two models:

- **Coroutine-native** (`io::when_all`, `io::when_any`) as specified in `combinators.md`
- **Sender/receiver** (`std::execution::when_all`) as specified in P2300R10

The comparison assumes the sender model routes I/O results correctly: `!ec` goes to `set_value(T...)`, `ec` goes to `set_error(ec)`. This routing happens in the coroutine body - the translation layer described in P4093R0 (Falco, Gerbino). The coroutine absorbs the compound `io_result{ec, n}`, uses `n` on the success path, and propagates `ec` on the failure path. By the time the result crosses the sender boundary, it is clean: value or error, never both.

---

## 1. The Routing Decision

The three-channel model works for I/O when the compound result is decomposed before crossing the sender boundary:

```cpp
// Translation layer (P4093)
capy::task<std::error_code>
read_all(auto& stream, auto buf)
{
    auto [ec, n] = co_await capy::read(
        stream, buf);
    if (ec)
        co_return ec;
    // use n...
    co_return {};
}
```

The coroutine body is the routing decision. `!ec` produces a value. `ec` produces an error. The sender boundary sees one or the other, never both. The three channels work as designed.

---

## 2. when_all Comparison

Assuming correct routing (`!ec` -> `set_value`, `ec` -> `set_error`):

| # | Scenario | Coroutine-native | Sender | Gap? |
|---|----------|------------------|--------|------|
| 1 | All tasks succeed | `io_result<R1,...,Rn>` with `!ec` | `tuple<R1,...,Rn>` via `set_value` | Ergonomic only |
| 2 | One task returns `ec` | Inspects `ec`. Cancels siblings. | `set_error(tuple(ec, T...))`. Cancels siblings. | None |
| 3 | Multiple `ec` concurrently | First `ec` wins. | First `set_error` wins. | None |
| 4 | EOF, `n == 0` | Error. Cancel. | `set_error(tuple(eof, 0))`. Cancel. | None |
| 5 | Partial transfer (`ec`, `n > 0`) | Error. Cancel. Partial bytes stored. | `set_error(tuple(ec, n))`. Cancel. `n` preserved in error payload. | None |
| 6 | Zero-length buffer, success | Success. | `set_value`. | None |
| 7 | Zero-length buffer, `ec` | Error. Cancel. | `set_error(tuple(ec, 0))`. Cancel. | None |
| 8 | One task throws | Exception. Cancel. Rethrow. | `set_error(exception_ptr)`. Cancel. Rethrow. | None |
| 9 | Multiple throws | First exception wins. | First `set_error` wins. | None |
| 10 | Exception vs. `ec` | Exception wins. | Both are `set_error`. Distinguishable by type. | Minor (type dispatch needed) |
| 11 | Parent stop | ECANCELED, standard behavior. | `set_stopped`. Standard behavior. | None |
| 12 | All tasks fail | First `ec` wins. | First `set_error` wins. | None |
| 13 | Failure reporting | Outer `io_result`'s `ec`. | `set_error` channel. | Ergonomic only |
| 14 | Return type | `io_result<R1,...,Rn>`. One `ec`, flat destructuring. | `tuple<R1,...,Rn>`. No `ec` lifting. | Ergonomic only |

**No structural gaps.** The sender `when_all` can do everything `io::when_all` can do when the routing is correct. With `set_error(tuple(ec, T...))`, partial bytes are preserved in the error payload. The differences are ergonomic: the coroutine-native return type (`io_result<R1,...,Rn>`) is more convenient than `tuple<R1,...,Rn>`. Neither difference affects capability.

---

## 3. when_any Comparison

P2300 does not define `when_any`. A custom sender `when_any` can be written. Assuming correct routing:

| # | Scenario | Coroutine-native | Sender | Gap? |
|---|----------|------------------|--------|------|
| 1 | First task succeeds | Winner by `!ec`. Cancel siblings. | Winner by `set_value`. Cancel siblings. | None |
| 2 | One task returns `ec`, others pending | Does not win. Keep waiting. | `set_error` fires. Does not win. Keep waiting. | None |
| 3 | One succeeds, one failed | Successful task wins. | `set_value` wins over `set_error`. | None |
| 4 | All tasks fail | `error_code` at variant index 0. Unspecified which. | Last `set_error` propagated (or unspecified). | None |
| 5 | One throws, others pending | Does not win. Keep waiting. | `set_error(exception_ptr)` does not win. Keep waiting. | None |
| 6 | All throw | Rethrow first. | Rethrow first. | None |
| 7 | Parent stop | ECANCELED at variant index 0. | `set_stopped`. | None |
| 8 | EOF, `n == 0` | Does not win. | `set_error(eof)`. Does not win. | None |
| 9 | Immediate completion | Wins normally. | Wins normally. | None |
| 10 | Return type | `variant<error_code, R1,...,Rn>`. Index 0 is failure. | Implementation-defined. | Ergonomic only |

**No structural gaps.** A sender `when_any` that accepts `set_value` completions as winners and ignores `set_error` completions does exactly what `io::when_any` does. The channel model provides the hook: `set_value` means success, `set_error` means failure.

---

## 4. Where the Routing Happens

The key insight is that the compound result never needs to cross the sender boundary intact. The coroutine body decomposes it:

```
I/O primitive -> io_result{ec, n}
                      |
              coroutine body (translation layer)
                      |
            +--------------------+
            |                    |
       !ec: set_value(T...)   ec: set_error(ec)
```

This is P4093's contribution. The coroutine body is not overhead - it is the mechanism that makes the three-channel model work for I/O. Without it, the compound result must be routed whole, and the trilemma from P2430R0 applies. With it, the result is decomposed at the source, and the channels work as designed.

Both models require this coroutine body. In the coroutine-native model, the body feeds `io::when_all` directly. In the sender model, the body feeds `set_value`/`set_error`. The decomposition is identical. The downstream combinator sees the same classification.

---

## 5. What Differs

The gaps are ergonomic, not structural:

### Return type convenience

Coroutine-native `io::when_all` returns `io_result<R1, R2, ..., Rn>` - one `ec`, flat destructuring:

```cpp
auto [ec, n1, n2, n3] = co_await io::when_all(
    read(s1), read(s2), read(s3));
```

Sender `when_all` returns `tuple<R1, R2, R3>` where each `Ri` is `tuple<Ti...>` (the value pack from that child):

```cpp
auto [r1, r2, r3] = co_await when_all(
    read(s1), read(s2), read(s3));
// Each ri is tuple<Ti...> from that child's set_value
```

### Partial bytes on failure

With `set_error(tuple(ec, T...))` routing, partial bytes are preserved in the error payload. Both models retain the same information on the failure path.

### when_any return type

Coroutine-native returns `variant<error_code, R1, ..., Rn>` with error at index 0. A sender `when_any` would need to define its own return type convention.

### No standard when_any

P2300 does not define `when_any`. One must be written. The coroutine-native model ships it.

---

## 6. What the Coroutine Model Cannot Do

| Capability | Sender model | Coroutine model |
|------------|--------------|-----------------|
| Heterogeneous execution contexts per child | Yes (each sender carries its own scheduler) | Limited (children share the parent's executor) |
| Compile-time work graph composition | Yes (sender chains are types) | No (coroutines are runtime) |
| GPU dispatch | Yes (schedulers abstract hardware) | No (coroutines require a CPU stack) |
| Lazy evaluation without coroutine frame | Yes (senders are lazy values) | No (coroutine frame allocated at call) |

These are the sender model's strengths. They are orthogonal to I/O combinators.

---

## 7. Conclusion

There are no structural gaps between the two models for I/O combinators. The three-channel model works for I/O when the compound result is decomposed in the coroutine body before crossing the sender boundary. This decomposition is the translation layer described in P4093R0.

The differences are ergonomic. The coroutine-native model provides a more convenient return type (`io_result<R1,...,Rn>` with one `ec` and flat destructuring), preserves partial bytes on the failure path, and ships `when_any` out of the box. The sender model requires the user to write `when_any` and returns less convenient types, but can express the same semantics.

Both models require a coroutine body to decompose the compound result. The coroutine body is the bridge between I/O's compound results and any combinator model - whether channel-based or value-based. The question is not which model handles compound results better at the combinator level. Both handle them identically, because both rely on the same coroutine body to decompose them first.
