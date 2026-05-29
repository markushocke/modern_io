import modern.io;
import modern.io.async;
import modern.net;
import modern.net.async;
import modern.net.adapters;

#include <gtest/gtest.h>

#include <coroutine>
#include <sstream>

TEST(CanonicalImportsTest, CanonicalUmbrellasExposeExistingSurface) {
  auto make_task = []() -> modern::net::Task<int> {
    co_return 11;
  };
  modern::io::ConnectionArena arena;
  modern::io::TraceContext context;
  auto ipc_transport_size = sizeof(modern::net::IPCTransport);
  auto tcp_client_size = sizeof(modern::net::TcpClient);
  auto tcp_server_size = sizeof(modern::net::TcpServer);
  modern::net::UdpTransport udp_transport;
  modern::net::UdpEndpoint udp_endpoint{"127.0.0.1", 9001};
  modern::net::TcpEndpoint endpoint{"127.0.0.1", 9000};
  modern::net::EventLoop injected_loop;
  auto socket = std::make_shared<modern::net::AsyncTcpSocket>();
  auto injected_socket = std::make_shared<modern::net::AsyncTcpSocket>(injected_loop);
  auto udp_socket = std::make_shared<modern::net::AsyncUdpSocket>();
  auto injected_udp_socket = std::make_shared<modern::net::AsyncUdpSocket>(injected_loop);
  modern::net::AsyncTcpServer server;
  modern::net::AsyncTcpServer injected_server(injected_loop);
  modern::net::AsyncTcpStreamAdapter tcp_stream(socket);
  modern::net::AsyncUdpStreamAdapter udp_stream(udp_socket);
  auto helper_awaiter = modern::net::make_awaitable_with_hooks<int>(
      [](std::coroutine_handle<>) {},
      [] { return 7; });
  modern::net::IScheduler* scheduler = nullptr;
  auto* compat_loop = &modern::net::EventLoop::instance();
  auto* reactor = &modern::net::default_event_reactor();
  auto* injected_reactor = static_cast<modern::net::EventReactor*>(&injected_loop);
  auto read_write = modern::net::IOEvent::Read | modern::net::IOEvent::Write;
  auto adapter_stream_storage = std::make_shared<std::stringstream>();
  modern::net::adapters::SharedStream<std::stringstream> adapter_stream(adapter_stream_storage);

  EXPECT_TRUE(arena.memory_resource() != nullptr);
  EXPECT_EQ(context.version, 0);
  EXPECT_GT(ipc_transport_size, 0u);
  EXPECT_GT(tcp_client_size, 0u);
  EXPECT_GT(tcp_server_size, 0u);
  EXPECT_EQ(udp_endpoint.port, 9001);
  EXPECT_EQ(endpoint.port, 9000);
  EXPECT_EQ(udp_transport.native_handle(), modern::net::invalid_socket);
  EXPECT_NE(socket, nullptr);
  EXPECT_NE(injected_socket, nullptr);
  EXPECT_NE(udp_socket, nullptr);
  EXPECT_NE(injected_udp_socket, nullptr);
  EXPECT_NE(compat_loop, nullptr);
  EXPECT_NE(reactor, nullptr);
  EXPECT_NE(injected_reactor, nullptr);
  EXPECT_EQ(scheduler, nullptr);
  adapter_stream.write("ad", 2);
  adapter_stream.flush();
  EXPECT_EQ(adapter_stream_storage->str(), "ad");
  EXPECT_EQ(&server.event_loop(), reactor);
  EXPECT_EQ(&injected_server.event_loop(), injected_reactor);
  EXPECT_EQ(&injected_socket->event_loop(), injected_reactor);
  EXPECT_EQ(&injected_udp_socket->event_loop(), injected_reactor);
  EXPECT_EQ(tcp_stream.native_handle(), socket->native_handle());
  EXPECT_EQ(udp_stream.native_handle(), udp_socket->native_handle());
  EXPECT_EQ(helper_awaiter.await_resume(), 7);
  EXPECT_EQ(make_task().get(), 11);
  EXPECT_EQ(server.native_handle(), modern::net::invalid_socket);
  EXPECT_TRUE(modern::net::has_event(read_write, modern::net::IOEvent::Read));
  EXPECT_NE(compat_loop, injected_reactor);
}