# Boost.Capy Examples

This directory contains example programs demonstrating Boost.Capy usage.

## Examples

### hello-task/

Minimal Capy program: a task that prints a message.

### producer-consumer/

Two tasks communicating via an async event.

### buffer-composition/

Composing buffer sequences without allocation for scatter/gather I/O.

### mock-stream-testing/

Unit testing protocol code with mock streams and error injection.

### type-erased-echo/

Echo server demonstrating the compilation firewall pattern.

### timeout-cancellation/

Using stop tokens to implement operation timeouts.

### parallel-fetch/

Running multiple operations concurrently with `when_all`.

### echo-server-corosio/

A complete echo server using Corosio for real network I/O. Requires Corosio.

### stream-pipeline/

Data transformation through a pipeline of sources and sinks.

### any-sender-size/

Measures the `exec::any_sender` operation state and the heap allocation its
`connect` performs, against the concrete operation state for the same
pipeline. Requires stdexec (`BOOST_CAPY_BUILD_P2300_EXAMPLES=ON`).

## Building

### CMake

```bash
cmake -B build -DBOOST_CAPY_BUILD_EXAMPLES=ON
cmake --build build
```

### B2 (BJam)

```bash
b2 example
```
