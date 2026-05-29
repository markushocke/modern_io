// Strict compile-time checks for async sockets
import net_io.async_tcp_socket;
import net_io.async_udp_socket;
import net_io.async_stream_adapters;
import net_io.scheduler;
import net_io.task;
import modern_io.concepts;
import net_io.async_concepts;
import net_io.event_loop;
#include <span>
#include <expected>
#include <system_error>
#include <coroutine>

// Check that AsyncTcpSocket fulfills AsyncInputStream/AsyncOutputStream like API
static_assert(requires(modern::net::AsyncTcpSocket& s, std::span<char> rb, std::span<const char> wb) {
    { s.read_some(rb) } -> std::same_as<std::expected<std::size_t, std::error_code>>;
    { s.write_some(wb) } -> std::same_as<std::expected<std::size_t, std::error_code>>;
    { s.native_handle() };
}, "AsyncTcpSocket must provide read_some/write_some/native_handle");

static_assert(requires(modern::net::AsyncUdpSocket& s, std::span<char> rb, std::span<const char> wb) {
    { s.read_some(rb) } -> std::same_as<std::expected<std::size_t, std::error_code>>;
    { s.write_some(wb) } -> std::same_as<std::expected<std::size_t, std::error_code>>;
    { s.native_handle() };
}, "AsyncUdpSocket must provide read_some/write_some/native_handle");

static_assert(requires(modern::net::AsyncTcpStreamAdapter& a, std::span<char> rb, std::span<const char> wb) {
    { a.read_async(rb) };
    { a.write_async(wb) };
}, "AsyncTcpStreamAdapter must provide read_async/write_async");

static_assert(std::same_as<modern::net::Task<int>, net_io::Task<int>>, "Legacy Task name must remain compatible");
static_assert(std::same_as<modern::net::IoTask<int>, net_io::IoTask<int>>, "Legacy IoTask name must remain compatible");
static_assert(std::same_as<modern::net::IScheduler, net_io::IScheduler>, "Legacy IScheduler name must remain compatible");
static_assert(std::same_as<modern::net::AsyncTcpStreamAdapter, net_io::AsyncTcpStreamAdapter>, "Legacy AsyncTcpStreamAdapter name must remain compatible");

static_assert(modern::net::NativeIoHandle<modern::net::AsyncTcpSocket>, "AsyncTcpSocket must expose a runtime-registerable native handle");
static_assert(std::same_as<modern::net::AsyncTcpSocket, net_io::AsyncTcpSocket>, "Legacy AsyncTcpSocket name must remain compatible");
static_assert(modern::net::NativeIoHandle<modern::net::AsyncUdpSocket>, "AsyncUdpSocket must expose a runtime-registerable native handle");
static_assert(std::same_as<modern::net::AsyncUdpSocket, net_io::AsyncUdpSocket>, "Legacy AsyncUdpSocket name must remain compatible");
static_assert(modern::net::IoRegistrationLike<modern::net::IoRegistration>, "IoRegistration must satisfy the runtime bridge contract");
static_assert(modern::net::EventReactorLike<modern::net::EventLoop>, "EventLoop must satisfy the runtime-facing EventReactor bridge");
static_assert(std::same_as<decltype(modern::net::EventLoop::instance()), modern::net::EventLoop&>, "EventLoop::instance must expose the canonical type");
static_assert(std::same_as<decltype(modern::net::default_event_reactor()), modern::net::EventReactor&>, "default_event_reactor must expose the bridge type");
static_assert(modern::net::has_event(modern::net::IOEvent::Read | modern::net::IOEvent::Write, modern::net::IOEvent::Read));
static_assert(modern::net::has_event(modern::net::IOEvent::Read | modern::net::IOEvent::Write, modern::net::IOEvent::Write));
static_assert(!modern::net::has_event(modern::net::IOEvent::Read, modern::net::IOEvent::Write));

int main() { return 0; }
