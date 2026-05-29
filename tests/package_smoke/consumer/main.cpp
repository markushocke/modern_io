import modern.io;
import modern.io.async;
import modern.net;
import modern.net.async;
import modern.net.adapters;

#include <memory>

int main() {
    modern::io::ConnectionArena arena;
    modern::trace::TraceContext trace;
    modern::net::TcpEndpoint endpoint{"127.0.0.1", 9000};
    auto tcp_socket = std::make_shared<modern::net::AsyncTcpSocket>();
    auto udp_socket = std::make_shared<modern::net::AsyncUdpSocket>();
    modern::net::AsyncTcpStreamAdapter tcp_stream(tcp_socket);
    modern::net::AsyncUdpStreamAdapter udp_stream(udp_socket);
    modern::net::AsyncTcpServer server;

    if (arena.memory_resource() == nullptr) {
        return 1;
    }
    if (trace.version != 0) {
        return 2;
    }
    if (endpoint.port != 9000) {
        return 3;
    }
    if (!modern::net::has_event(modern::net::IOEvent::Read | modern::net::IOEvent::Write,
                                 modern::net::IOEvent::Read)) {
        return 4;
    }
    if (tcp_stream.native_handle() != tcp_socket->native_handle()) {
        return 5;
    }
    if (udp_stream.native_handle() != udp_socket->native_handle()) {
        return 6;
    }
    if (&server.event_loop() != &modern::net::default_event_reactor()) {
        return 7;
    }

    return 0;
}