# Modern IO Framework

A modern, modular C++23 framework for high-level, type-safe, and efficient I/O: files, synchronous TCP/UDP networking, asynchronous networking, and data serialization. Designed for clarity, extensibility, and leveraging C++ modules, concepts, coroutines, and `std::expected`.

---

## Technology Stack

- **C++23**: Concepts, modules, coroutines, `std::expected`, and modern STL.
- **CMake 3.28+**: Build system with module support.
- **Sync surface**: `modern_io` and `net_io` are intended to stay portable across Linux and Windows.
- **Async surface**: `modern_io_async` and `net_io_async` are first-class libraries; `net_io_async` is currently validated on Linux.
- **No runtime dependencies** beyond the standard library, system socket APIs, and Google Test for development.

---

## Concept Overview

### Modular Design

- **Modules**: Each major component (file I/O, TCP, UDP, adapters, data streams) is a C++ module.
- **Adapters**: Uniform interfaces for files, TCP, UDP, and custom transports.
- **Streams**: InputStream/OutputStream concepts for generic, composable I/O.
- **Data Streams**: Type-safe serialization/deserialization for primitives and strings.
- **Buffering**: Optional buffered wrappers for performance.

## Public Libraries

- **`modern_io`**: synchronous file, data, buffered, and iostream adapters.
- **`modern_io_async`**: async task and async buffered/data layers.
- **`net_io`**: synchronous TCP/UDP/IPC networking primitives.
- **`net_io_async`**: event loop, awaiters, async sockets, async stream helpers, and async TCP server.
- **`net_io_adapters`**: adapters that bridge `net_io` transports into `modern_io` stream abstractions.

`modern_io_async` depends on `modern_io`, and `net_io_async` depends on `net_io`. This keeps sync and async consumption explicit at link time.

### Key Concepts

- **InputStream/OutputStream**: Abstract read/write interfaces.
- **Transportable**: Network transports (TCP/UDP) with open/close/read/write.
- **Adapters**: Bridge between transports and stream concepts.
- **SharedStream**: Shared ownership and method forwarding for streams.
- **Executors**: Pluggable concurrency for servers.

---

## How To: Build & Use

### Prerequisites

- C++23 compiler (Clang recommended) with modules support.
- CMake 3.28+.

### Build

```sh
git clone https://github.com/markushocke/modern_io.git
cd modern_io
# Use Ninja generator (recommended for C++ modules)
/usr/bin/cmake -B build -G Ninja -DCMAKE_CXX_COMPILER=clang++
cmake --build build
```

### Install And Consume Via CMake

```sh
cmake --install build --prefix /tmp/modern_io-install
```

```cmake
find_package(modern_io CONFIG REQUIRED)

add_executable(app main.cpp)
target_link_libraries(app PRIVATE
    modern_io::modern_io
    modern_io::net_io
)

# Async consumers link the async targets explicitly.
# target_link_libraries(app PRIVATE modern_io::modern_io_async modern_io::net_io_async)
```

The exported package currently provides `modern_io::modern_io`, `modern_io::modern_io_async`, `modern_io::net_io`, `modern_io::net_io_async`, and `modern_io::net_io_adapters`.

### Run Tests

```sh
cd build
ctest --output-on-failure -j 4

# Or run specific tests
./tests/modern_io/test_modern_io_file
./tests/modern_io/test_modern_io_data
./tests/net_io/test_net_io_async_tcp
```

See [TESTING.md](TESTING.md) and [UNITTEST_SUMMARY.md](UNITTEST_SUMMARY.md) for comprehensive test documentation.

### Project Structure

```
modern_io/
    ├── modern_io.ixx            # Sync umbrella module
    ├── modern_io_async.ixx      # Async umbrella module
    ├── modern_io_file.ixx       # File streams
    ├── modern_io_data.ixx       # Data (de)serialization
    ├── modern_io_async_data.ixx # Async data streams
    ├── modern_io_buffered.ixx   # Buffered streams
    ├── modern_io_async_buffered.ixx
    ├── net_io.ixx               # Sync network umbrella module
    ├── net_io_async.ixx         # Async network umbrella module
    ├── net_io_tcp_endpoint.ixx
    ├── net_io_tcp_client.ixx
    ├── net_io_tcp_server.ixx
    ├── net_io_udp_endpoint.ixx
    ├── net_io_udp_transport.ixx
    ├── net_io_async_tcp_socket.ixx
    ├── net_io_async_udp_socket.ixx
    ├── net_io_async_stream_base.ixx
    ├── net_io_async_utils.ixx
    ├── net_io_async_tcp_server.ixx
    ├── net_io_adapters.ixx      # Sync stream adapters
    ├── main.cpp                 # Example usage
    └── CMakeLists.txt
```

---

## First Steps

### 1. File I/O

```cpp
import modern_io;

using namespace modern_io;

// Write to file
FileOutputStream out("hello.bin");
out.write("Hello", 5);
out.flush();

// Read from file
FileInputStream in("hello.bin");
char buf[5];
in.read(buf, 5);
```

### 2. Data Serialization

```cpp
import modern_io;

using namespace modern_io;

FileOutputStream fs("data.bin");
DataOutputStream<FileOutputStream> dout(std::move(fs), std::endian::big);
dout.write_int32(42);
dout.write_string("Hello World");
dout.flush();

FileInputStream fi("data.bin");
DataInputStream<FileInputStream> din(std::move(fi), std::endian::big);
int value = din.read_int32();
std::string msg = din.read_string();
```

### 3. TCP Networking

```cpp
import modern_io;
import net_io;
import net_io_adapters;

using namespace net_io;
using namespace net_io_adapters;

// Client: Template parameters are automatically deduced!
auto stream = make_stream(TcpEndpoint("127.0.0.1", 9000));
DataOutputStream out(stream, std::endian::big);
out.write_string("Hello TCP");
out.flush();
```

### 4. UDP Networking

```cpp
import modern_io;
import net_io;
import net_io_adapters;

using namespace net_io;
using namespace net_io_adapters;

// Client: Template parameters are automatically deduced!
auto stream = make_stream(UdpEndpoint("127.0.0.1", 9001));
DataOutputStream out(stream, std::endian::big);
out.write_string("Hello UDP");
out.flush();
```

### 5. Async Networking

```cpp
import modern_io_async;
import net_io;
import net_io_async;

using namespace modern_io;
using namespace net_io;

auto connect_once(std::shared_ptr<AsyncTcpSocket> socket,
                  const sockaddr_storage& addr,
                  socklen_t addr_len) -> ExpectedTask<void> {
    auto connected = co_await socket->async_connect(addr, addr_len);
    if (!connected) {
        co_return std::unexpected(connected.error());
    }
    co_return std::expected<void, std::error_code>{};
}

EventLoop::instance().start();

auto socket = std::make_shared<AsyncTcpSocket>();
TcpEndpoint endpoint("127.0.0.1", 9000);
auto sockaddr = endpoint.to_sockaddr(false);

auto result = connect_once(socket, sockaddr, sizeof(sockaddr)).sync_wait();
EventLoop::instance().stop();
```

Use a named coroutine helper like `connect_once(...)` instead of an immediately invoked coroutine lambda with captures. That pattern is easier to reason about and avoids coroutine lifetime pitfalls.

`net_io_async` is currently validated on Linux. The sync libraries remain the portable baseline for Windows builds.

---

## Example: TCP Echo Server

```cpp
import modern_io;
import net_io;
import net_io_adapters;

using namespace net_io;
using namespace net_io_adapters;

constexpr uint16_t PORT = 9050;
constexpr std::string address = "127.0.0.1";

void tcp_server()
{
    std::atomic<bool> running{true};
    ThreadExecutor exec;

    auto handler = [](auto&& shared_stream) {
        DataInputStream<decltype(shared_stream)> in(std::move(shared_stream), std::endian::big);
        DataOutputStream<decltype(shared_stream)> out(std::move(shared_stream), std::endian::big);

        std::string msg = in.read_string();
        std::osyncstream(std::cout) << "[TCP-Server] Received: " << msg << std::endl;

        out.write_string("Echo: " + msg);
        out.flush();
    };

    run_tcp_server(
        exec,
        std::move(handler),
        running,
        TcpEndpoint{address, PORT}
    );

    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    running = false;
}
```

---

## Generic Server Factory

```cpp
ThreadExecutor exec;
std::atomic<bool> running{true};
run_server(
    exec,
    [](auto&& stream) {
        DataInputStream in(stream, std::endian::big);
        DataOutputStream out(stream, std::endian::big);
        std::string msg = in.read_string();
        out.write_string("Echo: " + msg);
        out.flush();
    },
    running,
    TcpEndpoint{"127.0.0.1", 9050}
);
```

---

## Features

- **Unified I/O**: Files, TCP, UDP with the same stream interface.
- **Type-safe**: Concepts and templates ensure correct usage.
- **Composable**: Streams, adapters, and buffers can be layered.
- **Separated sync/async surfaces**: Consumers opt into async explicitly via `modern_io_async` and `net_io_async`.
- **Platform strategy**: sync targets are the cross-platform baseline; `net_io_async` is currently Linux-first.
- **Modern C++**: Uses modules, concepts, and standard library only.

## Error handling (Exceptions)

The project uses exceptions for synchronous operations and explicit domain error types to provide clearer context (e.g. path, address, port, error code).

### Design principles

- Prefer specific exception types instead of `std::runtime_error` everywhere.
- Maintain backward compatibility: network exceptions still derive from `std::runtime_error` so existing catch blocks keep working.
- Asynchronous APIs should favor `std::expected`/error-codes rather than throwing exceptions.
- Exceptions carry additional context: filename, path, system error code, endpoint information where applicable.

### net_io exception types

- `net_io::NetworkException` - base for network related errors
- `net_io::SocketException` - socket operation failures (contains an error code)
- `net_io::ConnectionException` - connection failures
- `net_io::BindException` - errors during bind/listen
- `net_io::TimeoutException` - timeouts
- `net_io::ResolutionException` - hostname resolution failures

### modern_io exception types

- `modern_io::IOException` - base for filesystem / I/O errors
- `modern_io::FileIOException` - file operations with path and error code
- `modern_io::ReadWriteException` - mismatch between expected and actual bytes read/written
- `modern_io::UnexpectedEOFException` - unexpected end-of-file

### Example

```cpp
// before:
throw std::runtime_error("connect failed");

// now:
throw net_io::ConnectionException("connect failed", endpoint, ec);
```

Using domain-specific exceptions improves test assertions and makes error causes easier to diagnose.

---

## Extending

- Implement your own transport or stream by satisfying the InputStream/OutputStream concepts.
- Add new adapters for custom protocols.
- Use or implement custom executors for concurrency.

---

## License

MIT License. See [LICENSE](LICENSE).

<details>
<summary>MIT License (Click to show)</summary>

Copyright (c) 2024 Markus Hocke

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

</details>

---

## Testing

The project includes comprehensive unit tests using Google Test framework.

### Running Tests

```sh
# Build with tests
cmake -B build
cmake --build build

# Run all tests
cd build
ctest --output-on-failure

# Or run specific test executables
./test_modern_io_file
./test_modern_io_data
./test_net_io_tcp_endpoint
```

### Test Coverage

- **modern_io tests**: File I/O, data streams, buffering, iostream adapters, concepts
- **net_io tests**: TCP/UDP endpoints, clients, servers, base functionality
- **net_io_adapters tests**: Stream adapters, shared streams, factory functions

### Writing New Tests

Tests are organized in the `tests/` directory by module:
- `tests/modern_io/` - Modern I/O module tests
- `tests/net_io/` - Network I/O module tests
- `tests/net_io_adapters/` - Adapter tests

Add new test files and register them in the appropriate `CMakeLists.txt`.

---

## Error Handling

The framework provides specific exception types for better error handling:

### modern_io Exceptions

- `IOException` - Base exception for I/O errors
- `FileIOException` - File operation errors (includes filepath and errno)
- `ReadWriteException` - Read/write operation failures
- `UnexpectedEOFException` - Unexpected end of file
- `DataFormatException` - Invalid data format during deserialization
- `BufferException` - Buffer-related errors
- `StreamPositionException` - Stream seeking errors

### net_io Exceptions

- `SocketException` - Base socket error (from net_io_base)
- `NetworkException` - General network errors
- `ConnectionException` - Connection failures (includes endpoint info)
- `BindException` - Bind/listen errors (includes address and port)
- `TimeoutException` - Operation timeout
- `ResolutionException` - Hostname resolution failures

Example:
```cpp
try {
    FileInputStream in("nonexistent.txt");
} catch (const FileIOException& e) {
    std::cerr << "Failed to open: " << e.filepath() 
              << " (errno: " << e.error_code() << ")\n";
}

try {
    TcpClient client(TcpEndpoint{"invalid.host", 8080});
    client.open();
} catch (const ConnectionException& e) {
    std::cerr << "Connection failed: " << e.what() 
              << " to " << *e.endpoint() << "\n";
}
```

---

## Authors

- [Markus Hocke](https://github.com/markushocke)
- Contributors welcome!

---

## Support

For questions, bug reports, or contributions, please open an issue or pull request on GitHub.
