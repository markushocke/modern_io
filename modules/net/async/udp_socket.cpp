module;

#include <expected>
#include <memory>
#include <span>
#include <system_error>
#include <cstdlib>
#include <iostream>

#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
#else
  #include <unistd.h>
  #include <errno.h>
  #include <fcntl.h>
  #include <arpa/inet.h>
  #include <sys/socket.h>
#endif

module net_io.async_udp_socket;

import modern_io.connection_arena;
import net_io_base;

namespace modern::net {

AsyncUdpSocket::AsyncUdpSocket(
    EventReactor& loop,
    std::shared_ptr<modern_io::ConnectionArena> arena)
    : fd_(invalid_socket), loop_(&loop), arena_(normalize_arena(std::move(arena))) {}

AsyncUdpSocket::AsyncUdpSocket(
    EventReactor& loop,
    modern_io::ConnectionArenaSettings arena_settings)
    : fd_(invalid_socket), loop_(&loop), arena_(modern_io::make_connection_arena(arena_settings)) {}

AsyncUdpSocket::~AsyncUdpSocket() { close(); }

std::expected<void, std::error_code> AsyncUdpSocket::bind(const sockaddr_storage& sa, socklen_t len) {
    if (fd_ == invalid_socket) {
        if (!ensure_socket(sa.ss_family)) {
            return std::unexpected(std::make_error_code(std::errc::bad_file_descriptor));
        }
    }
    if (::bind(fd_, reinterpret_cast<const sockaddr*>(&sa), len) != 0) {
#ifdef _WIN32
        int error = WSAGetLastError();
#else
        int error = errno;
#endif
        return std::unexpected(std::error_code(error, std::system_category()));
    }
    local_addr_ = sa;
    local_addrlen_ = len;
    return {};
}

ssize_t AsyncUdpSocket::low_level_read(char* data, std::size_t n, std::error_code& ec) {
    ec.clear();
    if (fd_ == invalid_socket) {
        if (!ensure_socket()) {
            ec = std::make_error_code(std::errc::bad_file_descriptor);
            return -1;
        }
    }

    sockaddr_storage src{};
    socklen_t src_len = sizeof(src);
#ifdef _WIN32
    int ret = ::recvfrom(fd_, data, static_cast<int>(n), 0,
                         reinterpret_cast<sockaddr*>(&src), &src_len);
    if (ret < 0) {
        int error = WSAGetLastError();
        if (error == WSAEWOULDBLOCK || error == WSAEINPROGRESS) {
            ec = std::make_error_code(std::errc::operation_would_block);
        } else {
            ec = std::error_code(error, std::system_category());
        }
        return -1;
    }
#else
    ssize_t ret = ::recvfrom(fd_, data, n, 0,
                             reinterpret_cast<sockaddr*>(&src), &src_len);
    if (ret < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            ec = std::make_error_code(std::errc::operation_would_block);
        } else {
            ec = std::error_code(errno, std::system_category());
        }
        if (std::getenv("NET_IO_DEBUG")) {
            std::cout << "[UDP DEBUG] recvfrom(fd=" << fd_ << ") returned " << ret
                      << ", errno=" << errno << "\n";
        }
        return -1;
    }
#endif

    if (!connected_ && ret > 0) {
        peer_ = src;
        peer_len_ = src_len;
    }
    return ret;
}

ssize_t AsyncUdpSocket::low_level_write(const char* data, std::size_t n, std::error_code& ec) {
    ec.clear();

    if (fd_ == invalid_socket) {
        if (!ensure_socket()) {
            ec = std::make_error_code(std::errc::bad_file_descriptor);
            return -1;
        }
    }

    if (!connected_ && peer_len_ == 0) {
        ec = std::make_error_code(std::errc::destination_address_required);
        return -1;
    }

    if (peer_len_ > 0) {
        const sockaddr_in* sin = reinterpret_cast<const sockaddr_in*>(&peer_);
        char ip[INET_ADDRSTRLEN]{};
        inet_ntop(AF_INET, &(sin->sin_addr), ip, INET_ADDRSTRLEN);
    }

    ssize_t ret;
    if (connected_) {
#ifdef _WIN32
        ret = ::send(fd_, data, static_cast<int>(n), 0);
#else
        ret = ::send(fd_, data, n, 0);
#endif
    } else {
#ifdef _WIN32
        ret = ::sendto(fd_, data, static_cast<int>(n), 0,
                       reinterpret_cast<const sockaddr*>(&peer_), peer_len_);
#else
        ret = ::sendto(fd_, data, n, 0,
                       reinterpret_cast<const sockaddr*>(&peer_), peer_len_);
#endif
    }

    if (ret < 0) {
#ifdef _WIN32
        int error = WSAGetLastError();
        if (error == WSAEWOULDBLOCK) {
            ec = std::make_error_code(std::errc::operation_would_block);
        } else {
            ec = std::error_code(error, std::system_category());
        }
#else
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            ec = std::make_error_code(std::errc::operation_would_block);
        } else {
            ec = std::error_code(errno, std::system_category());
        }
#endif
        return -1;
    }
    return ret;
}

std::expected<std::size_t, std::error_code> AsyncUdpSocket::read_some(std::span<char> buf) {
    std::error_code ec;
    ssize_t n = low_level_read(buf.data(), buf.size(), ec);
    if (n < 0) return std::unexpected(ec);
    return static_cast<std::size_t>(n);
}

std::expected<std::size_t, std::error_code> AsyncUdpSocket::write_some(std::span<const char> buf) {
    std::error_code ec;
    ssize_t n = low_level_write(buf.data(), buf.size(), ec);
    if (n < 0) return std::unexpected(ec);
    return static_cast<std::size_t>(n);
}

void AsyncUdpSocket::close() {
    if (fd_ != invalid_socket) {
        loop_->deregister(fd_);
#ifdef _WIN32
        ::closesocket(fd_);
#else
        ::close(fd_);
#endif
        fd_ = invalid_socket;
    }
    peer_len_ = 0;
    connected_ = false;
}

bool AsyncUdpSocket::ensure_socket(int family) {
    if (fd_ != invalid_socket) return true;
#ifdef _WIN32
    detail::ensure_wsa();
#endif
    fd_ = ::socket(family, SOCK_DGRAM, 0);
    if (fd_ == invalid_socket) return false;

    set_socket_option(fd_, SocketOption::ReuseAddr, 1);

#ifdef _WIN32
    u_long mode = 1;
    ::ioctlsocket(fd_, FIONBIO, &mode);
#else
    int flags = ::fcntl(fd_, F_GETFL, 0);
    ::fcntl(fd_, F_SETFL, flags | O_NONBLOCK);
#endif
    return fd_ != invalid_socket;
}

} // namespace modern::net