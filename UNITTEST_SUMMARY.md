# Unit-Test Status Summary

## Current Status

- Google Test is integrated via `FetchContent` and all tests are registered with `gtest_discover_tests()`.
- The repository now builds as four public libraries: `modern_io`, `modern_io_async`, `net_io`, and `net_io_async`.
- The main validation run on Linux/Clang currently completes with **101 of 101 tests passing**.

## Coverage Snapshot

### modern_io
- `test_concepts.cpp`: concepts and compile-time checks
- `test_file.cpp`: file I/O, EOF, seek, move semantics, large-file paths
- `test_data.cpp`: serialization round-trips, endianness, EOF, invalid formats
- `test_buffered.cpp`: buffering, flush behavior, partial reads, integration
- `test_iostream.cpp`: `std::istream`/`std::ostream` adapters and round-trips

### net_io
- `test_base.cpp`: socket and exception baseline
- `test_tcp_endpoint.cpp`: endpoint parsing, resolution, IPv4/IPv6, comparison
- `test_tcp_client.cpp`: connection and exchange
- `test_tcp_server.cpp`: echo server behavior
- `test_udp_transport.cpp`: connected UDP send/receive
- `udp_ping_pong_integration.cpp`: integration ping/pong path
- `test_concepts_udp.cpp`: synchronous UDP concept checks

### net_io_async
- `test_concepts_static.cpp`: static API/contract checks across sync and async boundaries
- `test_concepts_async.cpp`: async socket and adapter concepts
- `test_async_tcp.cpp`: async TCP accept/read/write roundtrip
- `test_async_tcp_multi.cpp`: multi-client async TCP accept path
- `test_async_udp.cpp`: async UDP roundtrip
- `test_async_udp_multi.cpp`: multi-client async UDP behavior
- `test_event_loop.cpp`: wake, registration, duplicate handling, stop semantics, thread-safety
- `test_io_task.cpp`: task return values, exception propagation, move semantics

### net_io_adapters
- `test_adapters.cpp`: shared stream adapter smoke coverage

### Packaging
- `modern_io_package_consumer_smoke`: validates install-tree package generation, config/version files, exported targets, and consumer linkability

## Notable Validation Changes

- Async tests are no longer placeholders; they are part of the main suite.
- File tests use per-test temporary directories, so parallel CTest runs do not race on shared paths.
- The low-level async task contract is unified on `net_io::Task`, and the runtime is covered by dedicated unit and integration tests.
- The exported CMake package now includes a version file and a dedicated package-consumer smoke test.

## Platform Policy

- Full validation target: Linux + Clang + Ninja.
- Sync libraries are the portability baseline for Windows.
- `net_io_async` is currently treated as Linux-first because the runtime backend depends on Linux readiness primitives.

## Recommended CI Baseline

```bash
cmake -B build -G Ninja -DCMAKE_CXX_COMPILER=clang++
cmake --build build
cd build && ctest --output-on-failure -j 4
```

## Follow-Up Areas

1. Add a non-Linux async backend before advertising `net_io_async` as cross-platform.
2. Expand the package smoke test from target resolution/linkability to installed module-import compilation once the toolchain path is stable.
3. Expand adapter coverage beyond the current smoke-level shared-stream test.
