// Strict compile-time checks for async sockets
import net_io.async_tcp_socket;
import net_io.async_udp_socket;
import net_io.async_stream_adapters;
import modern_io.concepts;
import net_io.async_concepts;
#include <span>
#include <expected>
#include <system_error>

// Check that AsyncTcpSocket fulfills AsyncInputStream/AsyncOutputStream like API
static_assert(requires(net_io::AsyncTcpSocket& s, std::span<char> rb, std::span<const char> wb) {
    { s.read_some(rb) } -> std::same_as<std::expected<std::size_t, std::error_code>>;
    { s.write_some(wb) } -> std::same_as<std::expected<std::size_t, std::error_code>>;
    { s.native_handle() };
}, "AsyncTcpSocket must provide read_some/write_some/native_handle");

static_assert(requires(net_io::AsyncUdpSocket& s, std::span<char> rb, std::span<const char> wb) {
    { s.read_some(rb) } -> std::same_as<std::expected<std::size_t, std::error_code>>;
    { s.write_some(wb) } -> std::same_as<std::expected<std::size_t, std::error_code>>;
    { s.native_handle() };
}, "AsyncUdpSocket must provide read_some/write_some/native_handle");

static_assert(requires(net_io::AsyncTcpStreamAdapter& a, std::span<char> rb, std::span<const char> wb) {
    { a.read_async(rb) };
    { a.write_async(wb) };
}, "AsyncTcpStreamAdapter must provide read_async/write_async");

int main() { return 0; }
