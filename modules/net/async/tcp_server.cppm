module;

#include <system_error>
#include <iostream>
#include <memory>
#include <unordered_map>
#include <deque>
#include <mutex>
#include <functional>
#include <coroutine>
#include <expected>
#include <cassert>
#include <thread>

#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
#else
  #include <netdb.h>
  #include <sys/socket.h>
  #include <unistd.h>
  #include <fcntl.h>
  #include <errno.h>
#endif

export module net_io.async_tcp_server;

import modern_io.connection_arena;
import net_io_base;
import net_io.tcp_endpoint;
import net_io.async_tcp_socket;
import net_io.event_loop;
import net_io.async_concepts; // optional for future concept checks
import net_io.generic_awaiter;
import net_io.async_utils;

namespace modern::net
{

using net_io::invalid_socket;
using net_io::set_socket_option;
using net_io::sock_t;
using net_io::SocketOption;

export class AsyncTcpServer {
public:
    explicit AsyncTcpServer(
        EventReactor& loop = default_event_reactor(),
        modern_io::ConnectionArenaSettings accepted_socket_arena_settings = {});
    ~AsyncTcpServer();

    std::expected<void, std::error_code> start(const TcpEndpoint& ep, int backlog = SOMAXCONN);

    void stop();

    sock_t native_handle() const noexcept { return fd_; }
    [[nodiscard]] EventReactor& event_loop() noexcept { return *loop_; }
    [[nodiscard]] const EventReactor& event_loop() const noexcept { return *loop_; }
    [[nodiscard]] modern_io::ConnectionArenaSettings connection_arena_settings() const noexcept {
        return accepted_socket_arena_settings_;
    }

    void set_connection_arena_settings(modern_io::ConnectionArenaSettings settings) noexcept {
        accepted_socket_arena_settings_ = settings;
    }

    // Async accept: returns IoTask with a shared_ptr<AsyncTcpSocket>
    IoTask<std::expected<std::shared_ptr<AsyncTcpSocket>, std::error_code>> accept();

private:
    static std::error_code last_socket_error();

    void close_fd();

    sock_t fd_;
    bool listening_;
    EventReactor* loop_;
    modern_io::ConnectionArenaSettings accepted_socket_arena_settings_{};
};

} // namespace modern::net

export namespace net_io {

using modern::net::AsyncTcpServer;

} // namespace net_io