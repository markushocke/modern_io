# Unit Test Status

**102 / 102 tests passing** — Linux, Clang, Ninja.

## Coverage by Module

### modern_io (5 test files)

| File | Covers |
|---|---|
| `test_concepts.cpp` | `InputStream`/`OutputStream` concept checks, mock implementations |
| `test_file.cpp` | File I/O, EOF, seek, move semantics, error paths |
| `test_data.cpp` | Serialization round-trips, endianness, EOF, invalid formats |
| `test_buffered.cpp` | Buffer mechanics, flush, partial reads, integration |
| `test_iostream.cpp` | `std::istream`/`std::ostream` adapter round-trips |

### net_io — sync (7 test files)

| File | Covers |
|---|---|
| `test_base.cpp` | `SocketException`, platform abstractions |
| `test_tcp_endpoint.cpp` | Parsing, hostname resolution, IPv4/IPv6, comparison |
| `test_tcp_client.cpp` | Connection and data exchange |
| `test_tcp_server.cpp` | Echo server behavior |
| `test_udp_transport.cpp` | Connected UDP send/receive |
| `test_udp_client_server_integration.cpp` | Client ↔ server integration path |
| `test_udp_send_recv_pairing.cpp` | Send/receive pairing validation |
| `udp_ping_pong_integration.cpp` | Ping/pong integration path |
| `test_concepts_udp.cpp` | Sync UDP concept checks |

### net_io — async (8 test files)

| File | Covers |
|---|---|
| `test_concepts_static.cpp` | Static API/contract checks across sync and async |
| `test_concepts_async.cpp` | Async socket and adapter concept validation |
| `test_async_tcp.cpp` | Async TCP accept/read/write roundtrip |
| `test_async_tcp_multi.cpp` | Multi-client async accept (stress-tested 25× in CI) |
| `test_async_udp.cpp` | Async UDP roundtrip |
| `test_async_udp_multi.cpp` | Multi-client async UDP |
| `test_event_loop.cpp` | Wake, registration, duplicate handling, stop, thread-safety |
| `test_io_task.cpp` | Return values, exception propagation, move semantics |

### net_io_adapters (1 test file)

| File | Covers |
|---|---|
| `test_adapters.cpp` | Shared stream adapter smoke |

### Packaging

`modern_io_package_consumer_smoke` validates install tree, config/version files, exported targets, and consumer linkability.

## CI Gates

The workflow runs four jobs on every push (see [TESTING.md](TESTING.md) for details):

- **linux-clang-full** — full `ctest` (102 tests)
- **linux-clang-async-focus** — async targets with timeouts + 25× multi-accept stress
- **linux-clang-asan-async** — ASAN + LeakSanitizer on async targets
- **windows-msvc-sync** — sync-only build and test

## Platform Policy

- Full validation: Linux + Clang + Ninja.
- Sync libraries are the portability baseline for Windows.
- `net_io_async` is Linux-first (epoll backend).

## Open Areas

1. Non-Linux async backend before advertising cross-platform async.
2. Expand package smoke test to cover installed module-import compilation.
3. Expand adapter coverage beyond current smoke level.
