module;

#include <expected>
#include <memory>
#include <span>
#include <system_error>

#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
#else
  #include <unistd.h>
  #include <errno.h>
  #include <fcntl.h>
  #include <sys/socket.h>
#endif

module net_io.async_tcp_socket;

import modern_io.connection_arena;
import net_io_base;

namespace modern::net {

namespace {

std::error_code map_errno_blocking(int error) {
#ifdef _WIN32
    if (error == WSAEWOULDBLOCK || error == WSAEINPROGRESS) {
        return std::make_error_code(std::errc::operation_would_block);
    }
#else
    if (error == EAGAIN || error == EWOULDBLOCK || error == EINPROGRESS) {
        return std::make_error_code(std::errc::operation_would_block);
    }
#endif
    return std::error_code(error, std::system_category());
}

} // namespace

AsyncTcpSocket::AsyncTcpSocket(
    EventReactor& loop,
    std::shared_ptr<modern_io::ConnectionArena> arena)
    : fd_(invalid_socket), loop_(&loop), arena_(normalize_arena(std::move(arena))) {}

AsyncTcpSocket::AsyncTcpSocket(
    EventReactor& loop,
    modern_io::ConnectionArenaSettings arena_settings)
    : fd_(invalid_socket), loop_(&loop), arena_(modern_io::make_connection_arena(arena_settings)) {}

AsyncTcpSocket::AsyncTcpSocket(
    sock_t fd,
    bool already_nonblocking,
    EventReactor& loop,
    std::shared_ptr<modern_io::ConnectionArena> arena)
    : fd_(fd), loop_(&loop), arena_(normalize_arena(std::move(arena))) {
    if (fd_ != invalid_socket && !already_nonblocking) {
        set_socket_option(fd_, SocketOption::NonBlocking, 1);
    }
}

AsyncTcpSocket::AsyncTcpSocket(
    sock_t fd,
    bool already_nonblocking,
    EventReactor& loop,
    modern_io::ConnectionArenaSettings arena_settings)
    : fd_(fd), loop_(&loop), arena_(modern_io::make_connection_arena(arena_settings)) {
    if (fd_ != invalid_socket && !already_nonblocking) {
        set_socket_option(fd_, SocketOption::NonBlocking, 1);
    }
}

AsyncTcpSocket::~AsyncTcpSocket() { close(); }

void AsyncTcpSocket::close() {
    if (fd_ != invalid_socket) {
        loop_->deregister(fd_);
#ifdef _WIN32
        ::closesocket(fd_);
#else
        ::close(fd_);
#endif
        fd_ = invalid_socket;
    }
}

ssize_t AsyncTcpSocket::low_level_read(char* data, std::size_t n, std::error_code& ec) {
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

ssize_t AsyncTcpSocket::low_level_write(const char* data, std::size_t n, std::error_code& ec) {
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

std::expected<std::size_t, std::error_code> AsyncTcpSocket::read_some(std::span<char> buf) {
    std::error_code ec;
    ssize_t n = low_level_read(buf.data(), buf.size(), ec);
    if (n < 0) return std::unexpected(ec);
    return static_cast<std::size_t>(n);
}

std::expected<std::size_t, std::error_code> AsyncTcpSocket::write_some(std::span<const char> buf) {
    std::error_code ec;
    ssize_t n = low_level_write(buf.data(), buf.size(), ec);
    if (n < 0) return std::unexpected(ec);
    return static_cast<std::size_t>(n);
}

bool AsyncTcpSocket::ensure_socket(int family) {
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

} // namespace modern::net