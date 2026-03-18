# Testing

## Build & Run

```bash
cmake -B build -G Ninja -DCMAKE_CXX_COMPILER=clang++
cmake --build build
cd build && ctest --output-on-failure -j4
```

Requires CMake 3.28+, Ninja, and a C++23 compiler with module support (Clang 16+ recommended).  
Google Test is fetched automatically via `FetchContent`.

### Useful Invocations

```bash
# Specific test suite
./tests/modern_io/test_modern_io_file --gtest_filter="FileIOTest.*"

# Single test case
./tests/net_io/test_net_io_async_tcp_multi --gtest_filter="AsyncTcpMultiTest.AcceptMultipleClients"

# Package smoke test only
ctest --output-on-failure -R modern_io_package_consumer_smoke
```

---

## Test Structure

```
tests/
├── test_net_helpers.hpp              # Shared sync test helpers
├── test_net_async_helpers.hpp        # Async coroutine test helpers
├── modern_io/
│   ├── test_concepts.cpp             # Concept validation
│   ├── test_file.cpp                 # File I/O, EOF, seek, move semantics
│   ├── test_data.cpp                 # Serialization round-trips, endianness
│   ├── test_buffered.cpp             # Buffer mechanics, flush, partial reads
│   └── test_iostream.cpp             # std::stream adapter round-trips
├── net_io/
│   ├── test_base.cpp                 # SocketException, platform abstractions
│   ├── test_tcp_endpoint.cpp         # Parsing, resolution, IPv4/IPv6
│   ├── test_tcp_client.cpp           # Connection and exchange
│   ├── test_tcp_server.cpp           # Echo server behavior
│   ├── test_udp_transport.cpp        # Connected UDP send/receive
│   ├── test_udp_client_server_integration.cpp
│   ├── test_udp_send_recv_pairing.cpp
│   ├── udp_ping_pong_integration.cpp
│   ├── test_concepts_udp.cpp         # Sync UDP concept checks
│   ├── test_concepts_static.cpp      # Static API/contract checks
│   ├── test_concepts_async.cpp       # Async socket & adapter concepts
│   ├── test_async_tcp.cpp            # Async TCP roundtrip
│   ├── test_async_tcp_multi.cpp      # Multi-client async accept
│   ├── test_async_udp.cpp            # Async UDP roundtrip
│   ├── test_async_udp_multi.cpp      # Multi-client async UDP
│   ├── test_event_loop.cpp           # Wake, registration, thread-safety
│   └── test_io_task.cpp              # Return values, exceptions, move
├── net_io_adapters/
│   └── test_adapters.cpp             # Shared stream adapter smoke
└── package_smoke/
    ├── CMakeLists.txt
    ├── RunPackageSmoke.cmake.in
    └── consumer/                     # Validates install tree & linkability
```

Current baseline: **102 tests passing** (Linux/Clang).

---

## Adding a Test

```cpp
// tests/net_io/test_myfeature.cpp
import net_io;
#include <gtest/gtest.h>

TEST(MyFeatureTest, BasicBehavior) {
    EXPECT_EQ(1 + 1, 2);
}
```

```cmake
# tests/net_io/CMakeLists.txt
add_executable(test_myfeature test_myfeature.cpp)
target_link_libraries(test_myfeature PRIVATE net_io GTest::gtest_main)
target_compile_features(test_myfeature PRIVATE cxx_std_23)
gtest_discover_tests(test_myfeature)
```

---

## CI Pipeline

The GitHub Actions workflow (`.github/workflows/cmake-multi-platform.yml`) runs four jobs on every push/PR to `main`:

| Job | What it does |
|---|---|
| **linux-clang-full** | Full build, full `ctest` (102 tests) |
| **linux-clang-async-focus** | Builds only async targets, runs them individually with `timeout`, plus 25× repeated multi-accept stress loop |
| **linux-clang-asan-async** | ASAN + LeakSanitizer build of async targets (`detect_leaks=1`, `abort_on_error=1`) |
| **windows-msvc-sync** | Sync-only build (`modern_io`, `net_io`, `net_io_adapters` + their tests) |

`net_io_async` is Linux-only (epoll). The Windows job validates the sync baseline until another backend exists.

---

## ASAN / Leak Detection

The CI ASAN job covers async targets automatically.  For local runs:

```bash
cmake -B build_asan -G Ninja \
  -DCMAKE_CXX_COMPILER=clang++ \
  -DCMAKE_CXX_FLAGS="-fsanitize=address -fno-omit-frame-pointer" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address"
cmake --build build_asan
ASAN_OPTIONS=detect_leaks=1 ./build_asan/tests/net_io/test_net_io_event_loop
```

Valgrind works as well:
```bash
valgrind --leak-check=full ./build/tests/net_io/test_net_io_event_loop
```

---

## Troubleshooting

| Symptom | Fix |
|---|---|
| `CMake 3.28 or higher is required` | Update CMake |
| `unknown type name 'import'` | Use Clang 16+ with C++23 modules |
| Async tests fail to build on Windows/macOS | Expected — `net_io_async` requires Linux (epoll) |
| Google Test download fails | Check network; or install gtest system-wide |
