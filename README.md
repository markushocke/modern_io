# Modern IO Framework

A modern, modular C++20 framework for high-level, type-safe, and efficient I/O: files, TCP/UDP networking, and data serialization. Designed for clarity, extensibility, and leveraging C++20 modules and concepts.

---

## Technology Stack

- **C++20**: Concepts, modules, coroutines, and modern STL.
- **CMake**: Build system with module support.
- **Cross-platform**: Linux, Windows, macOS (POSIX and Winsock support).
- **No external dependencies**: Only standard C++ and system libraries.

---

## Concept Overview

### Modular Design

- **Modules**: Each major component (file I/O, TCP, UDP, adapters, data streams) is a C++20 module.
- **Adapters**: Uniform interfaces for files, TCP, UDP, and custom transports.
- **Streams**: InputStream/OutputStream concepts for generic, composable I/O.
- **Data Streams**: Type-safe serialization/deserialization for primitives and strings.
- **Buffering**: Optional buffered wrappers for performance.

### Key Concepts

- **InputStream/OutputStream**: Abstract read/write interfaces.
- **Transportable**: Network transports (TCP/UDP) with open/close/read/write.
- **Adapters**: Bridge between transports and stream concepts.
- **SharedStream**: Shared ownership and method forwarding for streams.
- **Executors**: Pluggable concurrency for servers.

---

## How To: Build & Use

### Prerequisites

- C++20 compiler (Clang, GCC, MSVC) with modules support.
- CMake 3.28+.

### Build

```sh
git clone https://github.com/markushocke/modern_io.git
cd modern_io
cmake -B build
cmake --build build
```

### Project Structure

```
modern_io/
  ├── modern_io.ixx           # Main module (umbrella)
  ├── modern_io_concepts.ixx  # Stream concepts (InputStream, OutputStream, Async*)
  ├── modern_io_file.ixx      # File streams (FileInputStream, FileOutputStream)
  ├── modern_io_data.ixx      # Data (de)serialization (DataInputStream, DataOutputStream)
  ├── modern_io_buffered.ixx  # Buffered streams (BufferedInputStream, BufferedOutputStream)
  ├── modern_io_iostream.ixx  # std::istream / std::ostream adapters
  ├── net_io.ixx              # Network umbrella module
  ├── net_io_base.ixx         # Platform abstraction (socket types, options, WSA init)
  ├── net_io_concepts.ixx     # Network concepts (Readable, Writable, Transportable, Acceptable)
  ├── tcp_endpoint.ixx        # TCP endpoint abstraction (address + port, DNS resolution)
  ├── tcp_client.ixx          # TCP client (connect, read, write, close)
  ├── tcp_server.ixx          # TCP server (listen, accept, dual-stack IPv4/IPv6)
  ├── udp_endpoint.ixx        # UDP endpoint abstraction (client/server mode)
  ├── udp_transport.ixx       # UDP transport (open_connect, open_bind, sendto/recvfrom)
  ├── net_io_adapters.ixx     # Adapters, factories, executors, and server helpers
  ├── main.cpp                # Example usage (TCP/UDP/File demos)
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

// Client: Template-Parameter werden automatisch deduziert!
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

// Client: Template-Parameter werden automatisch deduziert!
auto stream = make_stream(UdpEndpoint("127.0.0.1", 9001));
DataOutputStream out(stream, std::endian::big);
out.write_string("Hello UDP");
out.flush();
```

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

## Generische Server-Factory

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
- **Cross-platform**: Works on Linux, Windows, macOS.
- **Modern C++**: Uses modules, concepts, and standard library only.

---

## Extending

- Implement your own transport or stream by satisfying the InputStream/OutputStream concepts.
- Add new adapters for custom protocols.
- Use or implement custom executors for concurrency.

---

## License

MIT License. See [LICENSE](LICENSE).

<details>
<summary>MIT License (Klicken zum Anzeigen)</summary>

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

## Authors

- [Markus Hocke](https://github.com/markushocke)
- Contributors welcome!

---

## Support

For questions, bug reports, or contributions, please open an issue or pull request on GitHub.

---

## Roadmap & Next Steps

### 🔴 Important Fixes

1. **Test Infrastructure**: There is currently no formal test suite. Adding [Catch2](https://github.com/catchorg/Catch2) or [Google Test](https://github.com/google/googletest) with unit and integration tests would greatly improve reliability and catch regressions early. A basic end-to-end smoke test (`IOApp`) is now wired into CTest as a first step.
2. **Error Handling in Network Sockets**: Some socket-level failures (e.g., `set_socket_option` calls) are currently silently ignored. These should propagate errors or at least log warnings.

### 🟡 Improvements

4. **`std::filesystem` support**: Replace `const std::string& path` in `FileInputStream` / `FileOutputStream` with `const std::filesystem::path&` for better cross-platform path handling.
5. **`TransportSource::eof()` semantics**: The socket-based `eof()` always returns `false`. It should detect a graceful TCP connection close (peer sends FIN) and return `true` in that case.
6. **TcpServer connection limits / timeout**: Add configurable accept timeout and maximum simultaneous connections to prevent resource exhaustion.
7. **Async I/O implementation**: The `AsyncInputStream` and `AsyncOutputStream` concepts are defined but no concrete implementation exists. A `std::future`-based or coroutine-based implementation would complete the async story.
8. **Thread pool executor**: `ThreadExecutor` spawns an unbounded number of detached threads. A fixed-size thread pool would be safer for production use.
9. **Unit tests for adapters**: The datagram adapter factory fixes (`make_datagram_sink`, `make_datagram_source`) and the `DuplexDatagramStream` null-peer guard should be covered by dedicated unit tests once a test framework is integrated.
10. **Compiler-specific include guards**: The `#ifndef _MSC_VER` / `#ifdef _MSC_VER` blocks in every module file are repetitive. These can be unified with a single platform header.

### 🟢 Missing Features

11. **SSL/TLS support**: A `TlsClient` / `TlsServer` wrapping OpenSSL or a similar library would enable secure connections without changing the stream interface.
12. **HTTP protocol layer**: A lightweight HTTP/1.1 request/response parser built on top of `TcpDuplexStream` would make the framework usable for REST APIs and webhooks.
13. **WebSocket support**: WebSocket framing on top of TCP would enable bidirectional web communication.
14. **Connection pooling**: A `TcpConnectionPool` to reuse and manage multiple concurrent connections efficiently.
15. **DNS caching**: `TcpEndpoint` currently calls `getaddrinfo()` on every `open()`. A simple TTL-based cache would reduce latency for frequently reconnected endpoints.
16. **Graceful server shutdown**: `TcpServer::stop()` should drain in-flight connections before closing, instead of immediately closing the listening socket.
17. **Rate limiting / backpressure**: Mechanisms to throttle reads/writes on streams to prevent overwhelming slow consumers.
18. **Serialization for more types**: `DataOutputStream`/`DataInputStream` support fixed-width integers, floats, and strings. Adding support for `bool`, `int8_t`/`uint8_t`, `int16_t`/`uint16_t`, `std::vector`, and user-defined types via a traits hook would make it more general-purpose.
