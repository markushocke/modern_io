// Compile-time concept / API presence checks
import net_io_concepts;
import net_io.ipc_transport;
import net_io.async_tcp_socket;
import net_io.async_stream_adapters;
import net_io.async_utils;
import modern_io.buffered;
import modern_io.concepts;
#include <span>
#include <expected>
#include <system_error>

// No runtime code required — this test is purely compile-time.
// Keep it as a test executable so CMake compiles it with project module imports.

static_assert(net_io_concepts::Readable<net_io::IPCTransport>, "IPCTransport must be Readable");
static_assert(net_io_concepts::Writable<net_io::IPCTransport>, "IPCTransport must be Writable");

// Check AsyncTcpSocket exposes async_read(std::span<char>) and async_write(std::span<const char>)
static_assert(
    requires(net_io::AsyncTcpSocket& s, std::span<char> buf) {
        { s.read_some(buf) } -> std::same_as<std::expected<std::size_t, std::error_code>>;
        { s.write_some(std::span<const char>{}) } -> std::same_as<std::expected<std::size_t, std::error_code>>;
        { s.native_handle() };
    },
    "AsyncTcpSocket must provide read_some/write_some/native_handle"
);

static_assert(
    requires(net_io::AsyncTcpStreamAdapter& a, std::span<char> buf) {
        { a.read_async(buf) } -> std::same_as<net_io::Task<std::expected<std::size_t, std::error_code>>>;
        { a.write_async(std::span<const char>{}) } -> std::same_as<net_io::Task<std::expected<std::size_t, std::error_code>>>;
    },
    "AsyncTcpStreamAdapter must provide read_async/write_async returning Task<std::expected<std::size_t, std::error_code>>"
);

int main() { return 0; }
