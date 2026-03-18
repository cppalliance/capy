# io::when_all / io::when_any - Behavior Specification

Team working document. March 2026.

---

## Design Basis

`io::when_all` and `io::when_any` are I/O-aware combinators. They know the result convention: `tuple<error_code, T...>`. Success means `!ec`. Failure means `ec`.

The primitive resolves ambiguity at the source: when `bytes_transferred == bytes_requested`, the primitive returns `({}, n)`. By the time the combinator sees the result, `!ec` is reliable. The combinator only inspects the error_code. It does not interpret `T...`.

The synchronous analogy governs the model:

```cpp
std::tuple result{ f1(), f2(), ..., fN() };
```

If any of `f1`...`fN` fails, you get the error - not a tuple of mixed results. The tuple only exists on the success path.

---

## io::when_all Behavior Table

| #  | Scenario                                  | Behavior                                                                 | Status      |
| -- | ----------------------------------------- | ------------------------------------------------------------------------ | ----------- |
| 1  | All tasks return `!ec`                    | Return tuple of all results. No cancellation.                            | Settled     |
| 2  | One task returns `ec`, others pending     | Cancel siblings. Propagate error.                                        | Settled     |
| 3  | Multiple tasks return `ec` concurrently   | Each triggers stop (idempotent). First ec wins.                          | Settled     |
| 4  | `ec == eof`, `n == 0`                     | Error. Cancel siblings.                                                  | Settled     |
| 5  | `ec != 0`, `n > 0` (partial transfer)    | Error. Cancel siblings. Values stored as-is (discarding would be the special case given the return type). Caller sees `ec` and knows it failed; partial values are available but not guaranteed meaningful. | Settled     |
| 6  | Zero-length buffer, `({}, 0)`             | Success. No cancellation.                                                | Settled     |
| 7  | Zero-length buffer, `(ec, 0)`             | Error (ec reflects stream state). Cancel siblings.                       | Settled     |
| 8  | One task throws                           | Capture exception. Cancel siblings. Rethrow after all complete.          | Settled     |
| 9  | Multiple tasks throw                      | First exception captured. Others discarded. Rethrow first.               | Settled     |
| 10 | One throws, another returns `ec` (either order) | Exception always wins. No value to return through an exception. Tuple is not accessible. | Settled     |
| 11 | Parent stop token fires                   | Not a special case. Children return ECANCELED, which is an error like any other. First ec wins, standard error behavior. | Settled     |
| 12 | All tasks fail                            | Propagate single error_code (first wins). Not a tuple of failures.       | Settled     |
| 13 | How does failure reach the caller?        | Via the outer `io_result`'s `ec`. See Return Type below.                 | Settled     |
| 14 | Return type                               | `io_result<R1, R2, ..., Rn>` where each `Ri` is `Ti` if child has single `T`, or `tuple<Ti...>` if child has multiple `T`s. See Return Type below. | Settled     |

---

## io::when_all Return Type

Peter Dimov's proposal: the return type lifts the error_code out of each child's `io_result` into a single outer `io_result`. Child value types are collected as parameters.

Given children returning `io_result<T1...>`, `io_result<T2...>`, ..., `io_result<Tn...>`:

```
io::when_all(child1, child2, ..., childN)
  -> io_result<R1, R2, ..., Rn>
```

Where each `Ri` is:
- `Ti` directly, if the child returns `io_result<Ti>` (single type - no wrapping)
- `tuple<Ti...>` if the child returns `io_result<Ti1, Ti2, ...>` (multiple types)
- `tuple<>` if the child returns `io_result<>` (no value types)

### Examples

Three reads, each returning `io_result<size_t>`:

```cpp
auto [ec, n1, n2, n3] = co_await io::when_all(
    stream.read_some(buf1),
    stream.read_some(buf2),
    stream.read_some(buf3));
// ec: single error_code (first failure wins)
// n1, n2, n3: size_t values (only meaningful when !ec)
```

Mixed result types:

```cpp
// task_a returns io_result<size_t>
// task_b returns io_result<size_t, flags>
// task_c returns io_result<message>
auto [ec, n, tf, msg] = co_await io::when_all(
    task_a, task_b, task_c);
// ec: single error_code
// n: size_t from task_a
// tf: tuple<size_t, flags> from task_b
// msg: message from task_c
```

Void results (`io_result<>`):

```cpp
// task_a returns io_result<size_t>
// task_b returns io_result<>  (just error_code, no values)
// task_c returns io_result<size_t>
auto [ec, n1, empty, n2] = co_await io::when_all(
    task_a, task_b, task_c);
// ec: single error_code
// n1: size_t from task_a
// empty: tuple<> from task_b
// n2: size_t from task_c
```

`io_result<>` contributes a `tuple<>` to the outer parameter list. Same rule as multiple types (`tuple<T...>`), applied to zero types.

### Why this works

- **Binary outcome.** Caller checks one `ec`. On success, destructures the values. On failure, handles one error. Matches the synchronous analogy.
- **No redundant error codes.** Child error codes are stripped. On the success path they were all zero. On the failure path only the first matters.
- **Natural destructuring.** Single-type children flatten into the parameter list. `auto [ec, n1, n2, n3]` just works.
- **Consistent with io_result convention.** The result is itself an `io_result`, so it composes with anything that already handles `io_result`.

---

## io::when_any Behavior Table

`io::when_any` selects a winner. A task wins by succeeding: `!ec`. Tasks that fail or throw do not win.

| #  | Scenario                                  | Behavior                                                                 | Status      |
| -- | ----------------------------------------- | ------------------------------------------------------------------------ | ----------- |
| 1  | First task to return `!ec`                | Wins. Cancel siblings. Return winner's result.                           | Settled     |
| 2  | One task returns `ec`, others pending     | Does not win. Keep waiting.                                              | Settled     |
| 3  | One succeeds, one already failed          | Successful task wins.                                                    | Settled     |
| 4  | All tasks return `ec` (all fail)          | No winner. Variant holds `error_code` at index 0. Which child's ec is unspecified (likely last, as that is the natural implementation result). | Settled     |
| 5  | One task throws, others pending           | Exception does not win. Keep waiting for a success.                      | Settled     |
| 6  | All tasks throw                           | No success possible. Rethrow first exception.                            | Settled     |
| 7  | Parent stop fires before any completion   | All children cancelled. Variant holds `error_code` at index 0 (ECANCELED). | Settled     |
| 8  | `ec == eof`, `n == 0`                     | Error. Does not win.                                                     | Settled     |
| 9  | Immediate completion (await_ready true)   | Wins normally. No special treatment.                                     | Settled     |
| 10 | Return type                               | `variant<error_code, R1, R2, ..., Rn>`. Index 0 is error_code (failure/no winner). Index 1..N are success results. See Return Type below. | Settled     |

---

## io::when_any Return Type

Option B (Peter Dimov), with `error_code` first:

```
io::when_any(child1, child2, ..., childN)
  -> variant<error_code, R1, R2, ..., Rn>
```

Where each `Ri` follows the same rules as `when_all`:
- `Ti` directly, if the child returns `io_result<Ti>` (single type)
- `tuple<Ti...>` if the child returns `io_result<Ti1, Ti2, ...>` (multiple types)
- `tuple<>` if the child returns `io_result<>` (no value types)

Index 0 (`error_code`) is the failure/no-winner case. Index 1..N identifies which child won.

### Examples

Three reads, each returning `io_result<size_t>`:

```cpp
auto result = co_await io::when_any(
    stream.read_some(buf1),
    stream.read_some(buf2),
    stream.read_some(buf3));
// result: variant<error_code, size_t, size_t, size_t>

if (result.index() == 0)
    // all failed: std::get<0>(result) is the error_code
else
    // result.index() - 1 is the winning child
```

Mixed result types:

```cpp
// task_a returns io_result<size_t>
// task_b returns io_result<message>
auto result = co_await io::when_any(task_a, task_b);
// result: variant<error_code, size_t, message>

if (result.index() == 1)
    // task_a won: std::get<1>(result) is size_t
else if (result.index() == 2)
    // task_b won: std::get<2>(result) is message
else
    // failure: std::get<0>(result) is error_code
```

### Why this works

- **Winner identification.** `result.index() - 1` maps directly to the child index. No separate index field needed.
- **Error at index 0.** Failure is always `index() == 0`. Simple boolean check: `if (result.index() == 0)`.
- **No redundant error codes.** Winners have `!ec` by definition. The child's error_code is stripped. Only the failure case carries an error_code.
- **Consistent with when_all.** Same `Ri` flattening rules. Same error_code stripping. Different container (variant vs io_result).

---

## Status

All rows settled. Both tables complete. No open questions remain.

---

## Contributors

- Peter Dimov: primitive normalization rule, binary success/failure model, "first ec wins" semantics, outer io_result return type proposal, corrections to rows 5/10/12
- Andrzej Krzemienski: error_code is status not error, three-bucket classification (success/failure/cancellation)
- Michael Vandeberg: predicate-based combinator design (superseded by concrete io:: approach), when_any winner selection, issues 204-206
- Vinnie Falco: ssl_stream_truncated counterexample, partial transfer preservation question
- Richard: concrete use cases (NNTP, HTTP HEAD mirroring)
- Mungo Gill: translation layer / D4056 error handling
