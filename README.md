
# ![Modern IO Logo](graphics/modern_io.png)
A C++23 framework for type-safe I/O — files, networking, and async coroutine surfaces.  
Built on modules, concepts, `std::expected`, `modern_trace` for the shared trace contract, and `modern_runtime` for the coroutine task core.

---

## Architecture

The framework is split into modules that implement the shared concept hierarchy:

```
                  Stream Capabilities
                          │
        ┌─────────────────┼─────────────────┐
        │                 │                 │
        ▼                 ▼                 ▼
   File Streams      TCP Resources     UDP Resources
        │                 │                 │
        │                 └────────┬────────┘
        │                          │
        │                     as_stream()
                          │
        ┌─────────────────┼─────────────────┐
        │                                   │
        ▼                                   ▼
   InputStream                        OutputStream
        │                                   │
        └─────────────────┬─────────────────┘
                          │
                     Data Streams

                  Sync / Async Parity

      Readable              ↔ AsyncReadable
      Writable              ↔ AsyncWritable
      Transportable         ↔ AsyncTransportable
      Connectable           ↔ AsyncConnectable
      InputStream           ↔ AsyncInputStream
      OutputStream          ↔ AsyncOutputStream
```

This design emphasizes reusability and composability across different I/O transports.

```
modern_io              sync file / data / buffered / iostream streams
modern_io_async        async task types, async buffered & data layers
net_io                 sync TCP / UDP / IPC primitives
net_io_async           EventLoop, awaiters, async sockets & server
net_io_adapters        bridges net_io transports into modern_io streams
```

## Core Concepts

modern_io is built around a small set of composable concepts rather than
concrete implementations.

Files, TCP sockets, UDP sockets, adapters, and future transports all
participate in the same conceptual model. Algorithms, buffering layers,
serialization, and stream abstractions work across transport types without
modification.

### Transport Concepts

| Capability | Sync | Async |
|------|------|--------|
| Read | `InputStream` | `AsyncInputStream` |
| Write | `OutputStream` | `AsyncOutputStream` |
| Read | `Readable` | `AsyncReadable` |
| Writ | `Writable` | `AsyncWritable` |
| Duplex Transport | `Transportable` | `AsyncTransportable` |
| Connect | `Connectable` | `AsyncConnectable` |
| Connect Endpoint | `ConnectableTo<T>` | `AsyncConnectableTo<T>` |
| Close | `Closable` | `AsyncClosable` |
| Flush | `Flushable` | `AsyncFlushable` |
| EOF Detection | `EOFAware` | `AsyncEOFAware` |
| Server Accept | `Acceptable` | `AsyncAcceptable` |

### Stream Concepts

| Sync | Async |
|------|--------|
| `InputStream` | `AsyncInputStream` |
| `OutputStream` | `AsyncOutputStream` |
| `DuplexStream` | `AsyncDuplexStream` |


### Concept Documentation

- [Streams](docs/concepts/streams.md)
- [Transports](docs/concepts/transports.md)
- [Adapters](docs/concepts/adapters.md)
- [Servers](docs/concepts/servers.md)
- [Async Runtime](docs/concepts/async_runtime.md)

### Design Principles

- **Concept-first architecture**: All modules implement a shared set of concepts for composability.
- **Sync and async parity**: Both synchronous and asynchronous APIs follow the same conceptual model with `std::expected` for async error transport.
- **Type-safe APIs**: Concepts enforce compile-time correctness across all I/O operations.
- **C++23 modules and concepts**: Leverages modern C++ features for better organization and type safety.
- **Zero-overhead abstractions**: No runtime overhead from the conceptual model.
- **Explicit resource lifecycle**: Opening, connecting, and binding remain visible operations.
- **Transport-independent stream adapters**: `as_stream()` adapts an already-open resource without hidden I/O.

### Transport Adapters

A core feature of modern_io is the separation between resource setup and stream adaptation:

```cpp
FileInputStream file("input.bin"); // Already models InputStream.

auto tcp = std::make_shared<TcpClient>(TcpEndpoint("127.0.0.1", 9000));
tcp->open();
auto tcp_stream = as_stream(tcp);

auto udp = std::make_shared<UdpTransport>();
udp->open_connect(UdpEndpoint("127.0.0.1", 9000));
auto udp_stream = as_stream(udp);
```

`as_stream()` only adapts and retains the resource. It never opens, connects, binds, or takes ownership of a raw pointer. The resulting streams compose with the same data and buffering layers.


---

## Build & Install

```sh
git clone https://github.com/markushocke/modern_io.git
cd modern_io
# Source builds resolve modern_trace automatically:
# sibling ../modern_trace when present, otherwise GitHub fetch.
# Override with -DMODERN_TRACE_PROVIDER=package|local|fetch
# and -DMODERN_TRACE_ROOT=../modern_trace.
# modern_runtime still defaults to GitHub fetch.
# Alternatives: -DMODERN_RUNTIME_PROVIDER=local -DMODERN_RUNTIME_ROOT=../modern_runtime
# or -DMODERN_RUNTIME_PROVIDER=package
cmake -B build -G Ninja -DCMAKE_CXX_COMPILER=clang++
cmake --build build
```

```sh
# Install modern_io and the co-built sibling packages modern_trace and modern_runtime
cmake --install build --prefix /tmp/modern_io-install
```

For installed consumers, point `CMAKE_PREFIX_PATH` at the install prefix so CMake can resolve the sibling packages `modern_io`, `modern_trace`, and `modern_runtime`.

```cmake
find_package(modern_io CONFIG REQUIRED)
target_link_libraries(app PRIVATE
    modern_io::modern_io
    modern_io::net_io
    # modern_io::modern_io_async  modern_io::net_io_async   # opt-in
)
```

```sh
cmake -S consumer -B consumer-build -DCMAKE_PREFIX_PATH=/tmp/modern_io-install
```

Requires C++23 (Clang recommended) and CMake 3.28+.

---

## Examples

### File I/O

```cpp
import modern_io;
using namespace modern_io;

FileOutputStream out("data.bin");
DataOutputStream dout(std::move(out), std::endian::big);
dout.write_int32(42);
dout.write_string("Hello");
dout.flush();

FileInputStream in("data.bin");
DataInputStream din(std::move(in), std::endian::big);
auto value = din.read_int32();   // 42
auto msg   = din.read_string();  // "Hello"
```

### Sync TCP — explicit connection and stream adaptation

```cpp
import modern_io;
import net_io;
import net_io_adapters;
using namespace net_io;
using namespace net_io_adapters;

auto client = std::make_shared<TcpClient>(TcpEndpoint("127.0.0.1", 9000));
client->open();
auto stream = as_stream(client);
DataOutputStream out(stream, std::endian::big);
out.write_string("Hello TCP");
out.flush();
```

UDP follows the same pattern with `UdpTransport::open_connect()` or `open_bind()` before `as_stream()`.

### Sync TCP Echo Server

```cpp
import modern_io;
import net_io;
import net_io_adapters;
using namespace net_io;
using namespace net_io_adapters;

std::atomic<bool> running{true};
ThreadExecutor exec;

run_tcp_server(exec, [](auto&& stream) {
    DataInputStream  in(stream, std::endian::big);
    DataOutputStream out(stream, std::endian::big);
    out.write_string("Echo: " + in.read_string());
    out.flush();
}, running, TcpEndpoint{"127.0.0.1", 9050});
```

`run_tcp_server` is generic — replace the endpoint with any `Acceptable` server type and the handler keeps working.

### Async TCP — coroutine server + client

```cpp
import modern_io_async;
import net_io;
import net_io_async;
using namespace net_io;

// Accept one client, read a message, reply
ExpectedTask<void> serve_once(AsyncTcpServer& server) {
    auto cli = co_await server.accept();
    if (!cli) co_return std::unexpected(cli.error());

    AsyncTcpStreamAdapter stream(cli.value());
    char buf[64]{};
    auto r = co_await stream.read_async(std::span<char>(buf, sizeof(buf)));
    if (r && r.value() > 0) {
        std::string pong = "PONG";
        co_await stream.write_async({pong.data(), pong.size()});
    }
    co_return {};
}

// Connect, send a ping, read the reply
ExpectedTask<void> ping(std::string addr, uint16_t port) {
    auto sock = std::make_shared<AsyncTcpSocket>();
    auto sa   = TcpEndpoint(addr, port).to_sockaddr(false);
    auto conn = co_await sock->async_connect(sa, sizeof(sa));
    if (!conn) co_return std::unexpected(conn.error());

    AsyncTcpStreamAdapter stream(sock);
    std::string msg = "PING";
    co_await stream.write_async({msg.data(), msg.size()});

    char buf[64]{};
    auto r = co_await stream.read_async(std::span<char>(buf, sizeof(buf)));
    if (r) std::cout << std::string(buf, r.value()) << '\n';
    co_return {};
}

// Drive it
EventLoop::instance().start();
run_and_wait(serve_once(server));
run_and_wait(ping("127.0.0.1", 9000));
EventLoop::instance().stop();
```

Prefer named coroutine functions over lambda coroutines — it avoids lifetime pitfalls.

Async UDP follows the same pattern via `AsyncUdpSocket` + `AsyncUdpStreamAdapter`.

> `net_io_async` is currently Linux-only (epoll). The sync surface is the portable baseline for Windows.

---

## Error Handling

Sync APIs throw domain exceptions. Async APIs return `std::expected<T, std::error_code>`.

### Sync — exception hierarchy

**modern_io** (all derive from `IOException`):

| Type | Context |
|---|---|
| `FileIOException` | filepath, errno |
| `ReadWriteException` | expected vs. actual bytes |
| `UnexpectedEOFException` | — |
| `DataFormatException` | deserialization errors |
| `BufferException` | buffer overflow / underflow |
| `StreamPositionException` | seek errors |

**net_io** (all derive from `NetworkException` → `std::runtime_error`):

| Type | Context |
|---|---|
| `SocketException` | system error code |
| `ConnectionException` | endpoint info |
| `BindException` | address, port |
| `TimeoutException` | — |
| `ResolutionException` | hostname |

```cpp
try {
    FileInputStream in("missing.txt");
} catch (const FileIOException& e) {
    std::cerr << e.filepath() << ": errno " << e.error_code() << '\n';
}
```

### Async — `std::expected` + error codes

```cpp
auto r = co_await stream.read_async(buf);
if (!r) {
    std::cerr << "read failed: " << r.error().message() << '\n';
    co_return std::unexpected(r.error());
}
```

No exceptions cross coroutine suspension points.

---

## Extending the Framework

The concept-based design means new transports plug in without modifying existing code.

**Example — adding a TLS transport:**

```cpp
// 1. Satisfy the Transportable concept
class TlsClient {
public:
    void open();
    void close();
    std::size_t read(char* buf, std::size_t n);
    void write(const char* buf, std::size_t n);
};
// TlsClient now models Transportable — done.

// 2. All existing adapters and data streams work immediately
auto tls = std::make_shared<TlsClient>(/* ... */);
tls->open();
auto stream = as_stream(tls);   // TransportSource + TransportSink, deduced
DataOutputStream out(stream, std::endian::big);
out.write_string("Hello TLS");
out.flush();
```

For async transports: implement `low_level_read`/`low_level_write` + register with the `EventLoop`, then wrap in an `AsyncStreamBase`-derived adapter.

---

## Tests

```sh
cd build && ctest --output-on-failure -j4
```

102 tests covering sync I/O, data streams, buffering, TCP/UDP endpoints, async sockets, event loop, and multi-client accept scenarios.  
CI runs a dedicated ASAN async gate on every push.

See [TESTING.md](TESTING.md) and [UNITTEST_SUMMARY.md](UNITTEST_SUMMARY.md) for details.

---

## License

Apache License 2.0 — see [LICENSE](LICENSE).

---

## Authors

[Markus Hocke](https://github.com/markushocke) — contributions welcome.
