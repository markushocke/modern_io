// Strict compile-time checks for async sockets
import net_io.async_tcp_socket;
import net_io.async_udp_socket;
import net_io.async_stream_adapters;
import modern_io.concepts;
import net_io.async_concepts;
import net_io.event_loop;
#include <span>
#include <expected>
#include <system_error>
#include <coroutine>

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

static_assert(net_io::NativeIoHandle<net_io::AsyncTcpSocket>, "AsyncTcpSocket must expose a runtime-registerable native handle");
static_assert(net_io::NativeIoHandle<net_io::AsyncUdpSocket>, "AsyncUdpSocket must expose a runtime-registerable native handle");
static_assert(net_io::IoRegistrationLike<net_io::IoRegistration>, "IoRegistration must satisfy the runtime bridge contract");
static_assert(net_io::EventReactorLike<net_io::EventLoop>, "EventLoop must satisfy the runtime-facing EventReactor bridge");
static_assert(std::same_as<decltype(net_io::default_event_reactor()), net_io::EventReactor&>, "default_event_reactor must expose the bridge type");
static_assert(net_io::has_event(net_io::IOEvent::Read | net_io::IOEvent::Write, net_io::IOEvent::Read));
static_assert(net_io::has_event(net_io::IOEvent::Read | net_io::IOEvent::Write, net_io::IOEvent::Write));
static_assert(!net_io::has_event(net_io::IOEvent::Read, net_io::IOEvent::Write));

int main() { return 0; }
