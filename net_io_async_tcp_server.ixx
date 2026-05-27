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
import net_io.async_concepts; // (optional für spätere Concept-Checks)
import net_io.generic_awaiter;
import net_io.async_utils;

namespace net_io
{
export class AsyncTcpServer {
public:
    explicit AsyncTcpServer(
        EventReactor& loop = default_event_reactor(),
        modern_io::ConnectionArenaSettings accepted_socket_arena_settings = {})
        : fd_(invalid_socket),
          listening_(false),
          loop_(&loop),
          accepted_socket_arena_settings_(accepted_socket_arena_settings) {}
    ~AsyncTcpServer() { stop(); }

    std::expected<void, std::error_code> start(const TcpEndpoint& ep, int backlog = SOMAXCONN) {
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
            auto ec = last_socket_error(); close_fd(); return std::unexpected(ec);
        }
        if (::listen(fd_, backlog) < 0) {
            auto ec = last_socket_error(); close_fd(); return std::unexpected(ec);
        }
        listening_ = true;
        if (loop_->is_debug_enabled()) {
            std::ostringstream oss;
            oss << "[AsyncTcpServer] start this=" << this << " fd=" << fd_ << " listening=" << listening_;
            EventLoop::debug_log(oss.str());
        }
        return {};
    }

    void stop() {
        listening_ = false;
        if (fd_ != invalid_socket) {
            loop_->deregister(fd_);
        }
        close_fd();
    }

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
    IoTask<std::expected<std::shared_ptr<AsyncTcpSocket>, std::error_code>>
    accept() {
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

        auto accepted = co_await make_awaiter(
            fd_,
            [this](sock_t&) -> std::expected<sock_t, std::error_code> {
                sockaddr_storage peer{};
                socklen_t sl = sizeof(peer);

#ifdef _WIN32
                sock_t client = ::accept(fd_, reinterpret_cast<sockaddr*>(&peer), &sl);
                if (client != INVALID_SOCKET) {
                    if (loop_->is_debug_enabled()) {
                        std::ostringstream oss;
                        oss << "[AsyncTcpServer] accept immediate success fd=" << client;
                        EventLoop::debug_log(oss.str());
                    }
                    return client;
                }

                int err = WSAGetLastError();
                if (err == WSAEWOULDBLOCK || err == WSAEINPROGRESS) {
                    return std::unexpected(std::make_error_code(std::errc::resource_unavailable_try_again));
                }
                return std::unexpected(std::error_code(err, std::system_category()));
#else
                while (true) {
                    sock_t client = ::accept4(fd_, reinterpret_cast<sockaddr*>(&peer), &sl, SOCK_NONBLOCK);
                    if (client >= 0) {
                        if (loop_->is_debug_enabled()) {
                            std::ostringstream oss;
                            oss << "[AsyncTcpServer] accept immediate success fd=" << client;
                            EventLoop::debug_log(oss.str());
                        }
                        return client;
                    }
                    if (errno == EINTR) {
                        continue;
                    }
                    if (errno == EAGAIN || errno == EWOULDBLOCK) {
                        if (loop_->is_debug_enabled()) {
                            EventLoop::debug_log("[AsyncTcpServer] accept would block");
                        }
                        return std::unexpected(std::make_error_code(std::errc::resource_unavailable_try_again));
                    }
                    if (loop_->is_debug_enabled()) {
                        std::ostringstream oss;
                        oss << "[AsyncTcpServer] accept error errno=" << errno;
                        EventLoop::debug_log(oss.str());
                    }
                    return std::unexpected(std::error_code(errno, std::system_category()));
                }
#endif
            },
            [this](sock_t& fd, std::coroutine_handle<> h, std::shared_ptr<void> owner) {
                loop_->register_io(make_io_registration(fd, IOEvent::Read, h, std::move(owner)));
            },
            false
        );

        if (!accepted) {
            if (loop_->is_debug_enabled()) {
                std::ostringstream oss;
                oss << "[AsyncTcpServer] accept completed with error=" << accepted.error().message();
                EventLoop::debug_log(oss.str());
            }
            co_return std::unexpected(accepted.error());
        }

        if (loop_->is_debug_enabled()) {
            std::ostringstream oss;
            oss << "[AsyncTcpServer] accept completed fd=" << *accepted;
            EventLoop::debug_log(oss.str());
        }

#ifdef _WIN32
    co_return std::make_shared<AsyncTcpSocket>(*accepted, false, *loop_, accepted_socket_arena_settings_);
#else
    co_return std::make_shared<AsyncTcpSocket>(*accepted, true, *loop_, accepted_socket_arena_settings_);
#endif
    }

private:
    static std::error_code last_socket_error() {
#ifdef _WIN32
        return std::error_code(WSAGetLastError(), std::system_category());
#else
        return std::error_code(errno, std::system_category());
#endif
    }

    void close_fd() {
        if (fd_ != invalid_socket) {
#ifdef _WIN32
            ::closesocket(fd_);
#else
            ::close(fd_);
#endif
            fd_ = invalid_socket;
        }
    }

    sock_t fd_;
    bool listening_;
    EventReactor* loop_;
    modern_io::ConnectionArenaSettings accepted_socket_arena_settings_{};
};

} // namespace net_io