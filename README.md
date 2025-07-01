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
git clone https://github.com/your-org/modern_io.git
cd modern_io
cmake -B build
cmake --build build
```

### Project Structure

```
modern_io/
  ├── modern_io.ixx           # Main module
  ├── modern_io_concepts.ixx  # Stream concepts
  ├── modern_io_file.ixx      # File streams
  ├── modern_io_data.ixx      # Data (de)serialization
  ├── modern_io_buffered.ixx  # Buffered streams
  ├── net_io.ixx              # Network umbrella module
  ├── net_io_base.ixx         # Base concepts/types
  ├── tcp_endpoint.ixx        # TCP endpoint abstraction
  ├── tcp_client.ixx          # TCP client
  ├── tcp_server.ixx          # TCP server
  ├── udp_endpoint.ixx        # UDP endpoint abstraction
  ├── udp_transport.ixx       # UDP transport
  ├── net_io_adapters.ixx     # Adapters and shared streams
  ├── main.cpp                # Example usage
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

// Client
TcpEndpoint ep("127.0.0.1", 9000);
auto stream = make_shared_stream(ep);
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

// Client
UdpEndpoint ep("127.0.0.1", 9001);
auto stream = make_shared_stream(ep);
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

    auto handler = [](auto&& stream) {
        DataInputStream in(std::move(stream), std::endian::big);
        DataOutputStream out(std::move(stream), std::endian::big);
        std::string msg = in.read_string();
        out.write_string("Echo: " + msg);
        out.flush();
    };

    run_server_with_executor(
        exec,
        tcp_stream_builder,
        handler,
        running,
        TcpEndpoint{address, PORT}
    );
}
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

