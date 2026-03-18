module;

#include <coroutine>
#include <expected>
#include <span>
#include <system_error>
#include <memory>
#include <cstring>
#include <sstream>

#ifdef _WIN32
  #define NOMINMAX
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #pragma comment(lib, "ws2_32.lib")
#else
  #include <unistd.h>
  #include <errno.h>
  #include <fcntl.h>
  #include <sys/socket.h>
#endif

export module net_io.async_tcp_socket;

import net_io_base;
import net_io.generic_awaiter;
import net_io.event_loop;
import net_io.async_utils;

namespace net_io {

// ---------------- Helper to map blocking errors ----------------
inline std::error_code map_errno_blocking(int e) {
#ifdef _WIN32
    if (e == WSAEWOULDBLOCK || e == WSAEINPROGRESS)
        return std::make_error_code(std::errc::operation_would_block);
#else
    if (e == EAGAIN || e == EWOULDBLOCK || e == EINPROGRESS)
        return std::make_error_code(std::errc::operation_would_block);
#endif
    return std::error_code(e, std::system_category());
}

// ---------------- AsyncTcpSocket Class ----------------
export class AsyncTcpSocket : public std::enable_shared_from_this<AsyncTcpSocket> {
public:
    AsyncTcpSocket() : fd_(invalid_socket) {}

    explicit AsyncTcpSocket(sock_t fd, bool already_nonblocking = false) : fd_(fd) {
        if (fd_ != invalid_socket && !already_nonblocking)
            set_socket_option(fd_, SocketOption::NonBlocking, 1);
    }

    ~AsyncTcpSocket() { close(); }

    void close() {
        if (fd_ != invalid_socket) {
#ifdef _WIN32
            ::closesocket(fd_);
#else
            ::close(fd_);
#endif
            fd_ = invalid_socket;
        }
    }

    // ---------------- Low-level read/write ----------------
    ssize_t low_level_read(char* data, std::size_t n, std::error_code& ec) {
        ec.clear();
        if (fd_ == invalid_socket) {
            ec = std::make_error_code(std::errc::bad_file_descriptor);
            return -1;
        }
#ifdef _WIN32
        int ret = ::recv(fd_, data, static_cast<int>(n), 0);
        if (ret < 0) ec = map_errno_blocking(WSAGetLastError());
        return ret;
#else
        ssize_t ret = ::recv(fd_, data, n, 0);
        if (ret < 0) ec = map_errno_blocking(errno);
        return ret;
#endif
    }

    ssize_t low_level_write(const char* data, std::size_t n, std::error_code& ec) {
        ec.clear();
        if (fd_ == invalid_socket) {
            ec = std::make_error_code(std::errc::bad_file_descriptor);
            return -1;
        }
#ifdef _WIN32
        int ret = ::send(fd_, data, static_cast<int>(n), 0);
        if (ret < 0) ec = map_errno_blocking(WSAGetLastError());
        return ret;
#else
        ssize_t ret = ::send(fd_, data, n, 0);
        if (ret < 0) ec = map_errno_blocking(errno);
        return ret;
#endif
    }

    // ---------------- Mid-level helpers for async_utils ----------------
    [[nodiscard]] std::expected<std::size_t, std::error_code> read_some(std::span<char> buf) {
        std::error_code ec;
        ssize_t n = low_level_read(buf.data(), buf.size(), ec);
        if (n < 0) return std::unexpected(ec);
        return static_cast<std::size_t>(n);
    }

    [[nodiscard]] std::expected<std::size_t, std::error_code> write_some(std::span<const char> buf) {
        std::error_code ec;
        ssize_t n = low_level_write(buf.data(), buf.size(), ec);
        if (n < 0) return std::unexpected(ec);
        return static_cast<std::size_t>(n);
    }

    [[nodiscard]] auto native_handle() const noexcept { return fd_; }

    [[nodiscard]] auto async_read(std::span<char> buf) {
        return net_io::read_some_async(
            [this]() -> sock_t { return this->native_handle(); },
            [this, buf](sock_t&) -> std::expected<std::size_t, std::error_code> {
                std::error_code ec;
                auto n = low_level_read(buf.data(), buf.size(), ec);
                if (ec) return std::unexpected(ec);
                return static_cast<std::size_t>(n);
            }
        );
    }

    [[nodiscard]] auto async_write(std::span<const char> buf) {
        return net_io::write_some_async(
            [this]() -> sock_t { return this->native_handle(); },
            [this, buf](sock_t&) -> std::expected<std::size_t, std::error_code> {
                std::error_code ec;
                auto n = low_level_write(buf.data(), buf.size(), ec);
                if (ec) return std::unexpected(ec);
                return static_cast<std::size_t>(n);
            }
        );
    }

    // Ensure a valid non-blocking socket exists
    bool ensure_socket(int family = AF_INET) {
    if (fd_ != invalid_socket) return true;
#ifdef _WIN32
    detail::ensure_wsa();
#endif
    fd_ = ::socket(family, SOCK_STREAM, IPPROTO_TCP);
    if (fd_ == invalid_socket) return false;
    set_socket_option(fd_, SocketOption::ReuseAddr, 1);
#ifdef _WIN32
    u_long mode = 1;
    ::ioctlsocket(fd_, FIONBIO, &mode);
#else
    int flags = ::fcntl(fd_, F_GETFL, 0);
    ::fcntl(fd_, F_SETFL, flags | O_NONBLOCK);
#endif
    return true;
    }
    
    // Async connect awaiter for TCP
    [[nodiscard]] auto async_connect(const sockaddr_storage& addr, socklen_t len) {
        struct ConnectAwaiter {
            AsyncTcpSocket* self;
            sock_t& fd;
            const sockaddr_storage& addr;
            socklen_t len;

            bool await_ready() {
                if (fd == invalid_socket) {
                    if (!self->ensure_socket(addr.ss_family)) {
                        return true; // report error immediately
                    }
                }
                int res = ::connect(fd, reinterpret_cast<const sockaddr*>(&addr), len);
                if (res == 0) {
                    return true; // connected synchronously
                }
#ifdef _WIN32
                int err = WSAGetLastError();
                if (err != WSAEWOULDBLOCK && err != WSAEINPROGRESS)
#else
                int err = errno;
                if (err != EINPROGRESS && err != EAGAIN && err != EWOULDBLOCK)
#endif
                {
                    return true; // hard error
                }
                return false; // need to wait for write readiness
            }

            void await_suspend(std::coroutine_handle<> h) {
                EventLoop::instance().register_write(fd, h);
            }

            std::expected<void, std::error_code> await_resume() {
#ifdef _WIN32
                int err = 0; socklen_t l = sizeof(err);
                if (::getsockopt(fd, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&err), &l) < 0) {
                    err = WSAGetLastError();
                }
#else
                int err = 0; socklen_t l = sizeof(err);
                if (::getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &l) < 0) {
                    err = errno;
                }
#endif
                if (err != 0) return std::unexpected(std::error_code(err, std::system_category()));
                return {};
            }
        };

        return ConnectAwaiter{ this, fd_, addr, len };
    }
private:
    sock_t fd_;
};
} // namespace net_io
