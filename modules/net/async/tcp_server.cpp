module;

#include <system_error>
#include <iostream>
#include <memory>
#include <functional>
#include <coroutine>
#include <expected>
#include <sstream>
#include <array>
#include <stop_token>

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

module net_io.async_tcp_server;

import modern_io.connection_arena;
import net_io_base;
import net_io.tcp_endpoint;
import net_io.async_tcp_socket;
import net_io.event_loop;
import net_io.async_utils;
import modern.task_environment;

namespace modern::net {

AsyncTcpServer::AsyncTcpServer(
    EventReactor& loop,
    modern_io::ConnectionArenaSettings accepted_socket_arena_settings)
    : fd_(invalid_socket),
      listening_(false),
      loop_(&loop),
      accepted_socket_arena_settings_(accepted_socket_arena_settings) {}

AsyncTcpServer::~AsyncTcpServer() { stop(); }

std::expected<void, std::error_code> AsyncTcpServer::start(const TcpEndpoint& ep, int backlog) {
#ifdef _WIN32
    detail::ensure_wsa();
#endif
    if (listening_) return {};
    auto addr = ep.to_sockaddr(true);
    fd_ = ::socket(addr.ss_family, SOCK_STREAM, 0);
    if (fd_ == invalid_socket) return std::unexpected(last_socket_error());
    set_socket_option(fd_, SocketOption::NonBlocking, 1);
    set_socket_option(fd_, SocketOption::ReuseAddr, 1);
    if (::bind(fd_, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) < 0) {
        auto ec = last_socket_error();
        close_fd();
        return std::unexpected(ec);
    }
    if (::listen(fd_, backlog) < 0) {
        auto ec = last_socket_error();
        close_fd();
        return std::unexpected(ec);
    }
    listening_ = true;
    if (loop_->is_debug_enabled()) {
        std::ostringstream oss;
        oss << "[AsyncTcpServer] start this=" << this << " fd=" << fd_ << " listening=" << listening_;
        EventLoop::debug_log(oss.str());
    }
    return {};
}

void AsyncTcpServer::stop() {
    listening_ = false;
    if (fd_ != invalid_socket) {
        loop_->deregister(fd_);
    }
    close_fd();
}

IoTask<std::expected<std::shared_ptr<AsyncTcpSocket>, std::error_code>> AsyncTcpServer::accept() {
    auto completion_scheduler = co_await modern::this_task::scheduler();
    auto token = co_await modern::this_task::stop_token();
    co_return co_await accept(completion_scheduler, token);
}

IoTask<std::expected<std::shared_ptr<AsyncTcpSocket>, std::error_code>> AsyncTcpServer::accept(
    modern::scheduler completion_scheduler,
    std::stop_token token) {
    if (loop_->is_debug_enabled()) {
        std::ostringstream oss;
        oss << "[AsyncTcpServer] accept call this=" << this << " fd=" << fd_ << " listening=" << listening_;
        EventLoop::debug_log(oss.str());
    }

    if (loop_->is_debug_enabled()) {
        std::ostringstream oss;
        oss << "[AsyncTcpServer] accept enter fd=" << fd_ << " listening=" << listening_;
        EventLoop::debug_log(oss.str());
    }

    if (!listening_ || fd_ == invalid_socket) {
        if (loop_->is_debug_enabled()) {
            EventLoop::debug_log("[AsyncTcpServer] accept early bad_file_descriptor");
        }
        co_return std::unexpected(std::make_error_code(std::errc::bad_file_descriptor));
    }

    for (;;) {
        if (token.stop_requested())
            co_return std::unexpected(std::make_error_code(std::errc::operation_canceled));

            sockaddr_storage peer{};
            socklen_t sl = sizeof(peer);
#ifdef _WIN32
            sock_t client = ::accept(fd_, reinterpret_cast<sockaddr*>(&peer), &sl);
            if (client != INVALID_SOCKET) {
                set_socket_option(client, SocketOption::NonBlocking, 1);
            }
            int accept_error = client == INVALID_SOCKET ? WSAGetLastError() : 0;
#else
            sock_t client;
            do {
                client = ::accept4(fd_, reinterpret_cast<sockaddr*>(&peer), &sl, SOCK_NONBLOCK);
            } while (client < 0 && errno == EINTR);
            int accept_error = client < 0 ? errno : 0;
#endif
        if (accept_error == 0) {
#ifdef _WIN32
            auto socket = std::make_shared<AsyncTcpSocket>(client, true, *loop_, accepted_socket_arena_settings_);
#else
            auto socket = std::make_shared<AsyncTcpSocket>(client, true, *loop_, accepted_socket_arena_settings_);
#endif
        std::array<char, NI_MAXHOST> host{};
        std::array<char, NI_MAXSERV> service{};
        if (::getnameinfo(
              reinterpret_cast<const sockaddr*>(&peer), sl,
              host.data(), static_cast<socklen_t>(host.size()),
              service.data(), static_cast<socklen_t>(service.size()),
              NI_NUMERICHOST | NI_NUMERICSERV) == 0) {
            socket->set_peer_endpoint(TcpEndpoint{
                host.data(), static_cast<std::uint16_t>(std::stoul(service.data()))});
        }
            co_return socket;
        }

#ifdef _WIN32
        const bool would_block = accept_error == WSAEWOULDBLOCK || accept_error == WSAEINPROGRESS;
#else
        const bool would_block = accept_error == EAGAIN || accept_error == EWOULDBLOCK;
#endif
        if (!would_block)
            co_return std::unexpected(std::error_code(accept_error, std::system_category()));

        auto ready = co_await wait_io(
            *loop_, fd_, IOEvent::Read, completion_scheduler, token);
        if (!ready)
            co_return std::unexpected(ready.error());
    }
}

std::error_code AsyncTcpServer::last_socket_error() {
#ifdef _WIN32
    return std::error_code(WSAGetLastError(), std::system_category());
#else
    return std::error_code(errno, std::system_category());
#endif
}

void AsyncTcpServer::close_fd() {
    if (fd_ != invalid_socket) {
#ifdef _WIN32
        ::closesocket(fd_);
#else
        ::close(fd_);
#endif
        fd_ = invalid_socket;
    }
}

} // namespace modern::net
