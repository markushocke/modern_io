# Test Setup Guide

## Prerequisites

To build and run tests, you need:

- **CMake 3.28+** (for C++20 module support)
- **Ninja** (recommended for module-aware builds)
- **C++23 compiler** with modules support:
  - Clang 16+ (recommended)
  - GCC 14+ (experimental)
  - MSVC 19.34+ (Visual Studio 2022 17.4+)
- **Google Test** (automatically fetched by CMake)

`net_io_async` is currently validated on Linux. The synchronous libraries and tests remain the portable baseline.

## Building with Tests

```bash
# Configure the project
cmake -B build -G Ninja -DCMAKE_CXX_COMPILER=clang++

# Build everything including tests
cmake --build build

# Run all tests
cd build
ctest --output-on-failure -j 4

# Or run specific tests
./tests/modern_io/test_modern_io_file
./tests/modern_io/test_modern_io_data
./tests/net_io/test_net_io_base
```

## Test Organization

```
tests/
├── CMakeLists.txt
├── test_net_helpers.hpp            # Shared sync helpers
├── test_net_async_helpers.hpp      # Async coroutine helpers
├── package_smoke/
│   ├── CMakeLists.txt
│   ├── RunPackageSmoke.cmake.in
│   └── consumer/
│       ├── CMakeLists.txt
│       └── main.cpp
├── modern_io/
│   ├── CMakeLists.txt
│   ├── test_concepts.cpp       # Concept validation tests
│   ├── test_file.cpp           # File I/O tests
│   ├── test_data.cpp           # Data serialization tests
│   ├── test_buffered.cpp       # Buffered I/O tests
│   └── test_iostream.cpp       # iostream adapter tests
├── net_io/
│   ├── CMakeLists.txt
│   ├── test_base.cpp           # Base functionality tests
│   ├── test_tcp_endpoint.cpp   # TCP endpoint tests
│   ├── test_tcp_client.cpp     # TCP client tests
│   ├── test_tcp_server.cpp     # TCP server tests
│   ├── test_udp_transport.cpp  # UDP transport tests
│   ├── udp_ping_pong_integration.cpp
│   ├── test_concepts_static.cpp
│   ├── test_concepts_async.cpp
│   ├── test_async_tcp.cpp
│   ├── test_async_tcp_multi.cpp
│   ├── test_async_udp.cpp
│   ├── test_async_udp_multi.cpp
│   ├── test_event_loop.cpp
│   └── test_io_task.cpp
└── net_io_adapters/
    ├── CMakeLists.txt
    └── test_adapters.cpp       # Adapter tests
```

## Running Tests in Detail

### Run All Tests
```bash
cd build
ctest --output-on-failure
```

### Run Tests with Verbose Output
```bash
cd build
ctest -V
```

### Run Specific Test Suite
```bash
cd build
./tests/modern_io/test_modern_io_file --gtest_filter="FileIOTest.*"
```

### Run Package Smoke Test
```bash
cd build
ctest --output-on-failure -R modern_io_package_consumer_smoke
```

### Run Single Test Case
```bash
cd build
./tests/modern_io/test_modern_io_data --gtest_filter="DataStreamTest.Int32BigEndianRoundTrip"
```

## Test Coverage

### modern_io Module
- ✅ **Concepts**: Static assertions, mock implementations
- ✅ **File I/O**: Read/write, EOF, seeking, error handling
- ✅ **Data Streams**: All data types, endianness, exceptions
- ✅ **Buffered I/O**: Buffer mechanics, flush behavior
- ✅ **iostream Adapters**: std::stream integration

### net_io Module
- ✅ **Base**: SocketException, platform abstractions
- ✅ **TCP Endpoint**: Construction, resolution, IPv4/IPv6
- ✅ **TCP Client**: Connect and exchange tests
- ✅ **TCP Server**: Echo server tests
- ✅ **UDP Transport**: Send/receive integration tests
- ✅ **Async Concepts**: Compile-time checks for async sockets and adapters
- ✅ **Async Runtime**: TCP/UDP roundtrip and multi-client tests
- ✅ **Event Loop**: Wakeup, registration, duplicate handling, thread-safety
- ✅ **Task Layer**: Return values, exception propagation, move semantics

### net_io_adapters Module
- ✅ **Adapters**: Shared stream adapter tests implemented

### Packaging
- ✅ **Package Smoke**: install tree, config file, version file, exported targets, and consumer linkability are validated via `modern_io_package_consumer_smoke`

Current baseline in the main build: **101 tests passing**.

## Adding New Tests

### 1. Create Test File

Create a new `.cpp` file in the appropriate test directory:

```cpp
// test_myfeature.cpp
import modern_io;
import modern_io.myfeature;
#include <gtest/gtest.h>

using namespace modern_io;

TEST(MyFeatureTest, BasicFunctionality) {
    // Your test code here
    EXPECT_EQ(1 + 1, 2);
}
```

### 2. Register in CMakeLists.txt

Add to the appropriate `tests/*/CMakeLists.txt`:

```cmake
add_executable(test_myfeature test_myfeature.cpp)
target_link_libraries(test_myfeature PRIVATE modern_io GTest::gtest_main)
target_compile_features(test_myfeature PRIVATE cxx_std_23)
gtest_discover_tests(test_myfeature)
```

### 3. Build and Run

```bash
cmake --build build
cd build
ctest -R test_myfeature
```

## Exception Testing

The framework includes comprehensive exception types. Example test:

```cpp
TEST(FileIOTest, OpenNonexistentFile) {
    EXPECT_THROW({
        FileInputStream in("nonexistent.txt");
    }, FileIOException);
    
    try {
        FileInputStream in("nonexistent.txt");
        FAIL() << "Expected FileIOException";
    } catch (const FileIOException& e) {
        EXPECT_EQ(e.filepath(), "nonexistent.txt");
        EXPECT_NE(e.error_code(), 0);
    }
}
```

## Continuous Integration

For CI/CD pipelines:

```yaml
# Example GitHub Actions workflow
- name: Build and Test
  run: |
        cmake -B build -G Ninja -DCMAKE_CXX_COMPILER=clang++
    cmake --build build
        cd build && ctest --output-on-failure -j 4
```

Recommended CI policy:
- Run full build and full test suite on Linux with Clang.
- Validate sync targets on Windows as build-only until `net_io_async` gains a non-Linux backend.

## Troubleshooting

### CMake Version Too Old
```
Error: CMake 3.28 or higher is required
```
**Solution**: Update CMake or build from source.

### Compiler Doesn't Support Modules
```
Error: C++20 modules are not supported
```
**Solution**: Use Clang 16+ or update your compiler.

### Async Build On Non-Linux Platforms
`net_io_async` currently depends on the Linux event-loop backend (`epoll`/`eventfd`).

**Solution**: run full async validation on Linux, and keep Windows CI focused on `modern_io`, `net_io`, and `net_io_adapters` until another backend is implemented.

### Google Test Download Fails
```
Error: Could not download googletest
```
**Solution**: Check internet connection or manually install Google Test.

### Test Compilation Errors
```
Error: unknown type name 'import'
```
**Solution**: Ensure compiler has C++23 and modules enabled.

## Performance Testing

For performance-sensitive code, use:

```cpp
TEST(DataStreamTest, LargeDataTransfer) {
    auto start = std::chrono::high_resolution_clock::now();
    
    // Your performance test
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    EXPECT_LT(duration.count(), 1000);  // Should complete in < 1 second
}
```

## Memory Leak Detection

### With Valgrind (Linux)
```bash
valgrind --leak-check=full ./tests/modern_io/test_modern_io_file
```

### With AddressSanitizer
```bash
cmake -B build -DCMAKE_CXX_FLAGS="-fsanitize=address"
cmake --build build
./tests/modern_io/test_modern_io_file
```
