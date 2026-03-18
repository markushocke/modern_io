module;

#include <coroutine>
#include <system_error>
#include <span>
#include <memory>
#include <cassert>
#include <cstring>
#include <functional>
#include <optional>
#include <cerrno>
#include <iostream>

#if __has_include(<expected>)
#include <expected>
#elif __has_include(<experimental/expected>)
#include <experimental/expected>
namespace std {
    using std::experimental::expected;
    using std::experimental::unexpected;
}
#else
#error "No <expected> header found."
#endif

#ifdef _WIN32
  #define NOMINMAX
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #pragma comment(lib, "ws2_32.lib")
#else
  #include <netdb.h>
  #include <unistd.h>
  #include <errno.h>
  #include <arpa/inet.h>
  #include <fcntl.h>
#endif

export module net_io.async_udp_socket;
import net_io_base;
import net_io.event_loop;
import net_io.generic_awaiter;
import net_io.async_utils;
import net_io.async_concepts;

namespace net_io
{

inline std::error_code map_udp_block(int e) {
#ifdef _WIN32
    if (e == WSAEWOULDBLOCK || e == WSAEINPROGRESS)
        return std::make_error_code(std::errc::operation_would_block);
#else
    if (e == EAGAIN || e == EWOULDBLOCK)
        return std::make_error_code(std::errc::operation_would_block);
#endif
    return std::error_code(e, std::system_category());
}

namespace {
inline std::error_code map_udp_errno(int e) {
#ifdef _WIN32
    if (e == WSAEWOULDBLOCK || e == WSAEINPROGRESS)
        return std::make_error_code(std::errc::operation_would_block);
    if (e == WSAEDESTADDRREQ)
        return std::make_error_code(std::errc::destination_address_required);
    return std::error_code(e, std::system_category());
#else
    switch (e) {
        case EAGAIN:
#if defined(EWOULDBLOCK) && (EWOULDBLOCK != EAGAIN)
        case EWOULDBLOCK:
#endif
        case EINPROGRESS:
            return std::make_error_code(std::errc::operation_would_block);
        case EDESTADDRREQ:
            return std::make_error_code(std::errc::destination_address_required);
        default:
            return std::error_code(e, std::system_category());
    }
#endif
}
} // namespace

export class AsyncUdpSocket : public std::enable_shared_from_this<AsyncUdpSocket> {
public:
    AsyncUdpSocket() : fd_(invalid_socket) {}

    ~AsyncUdpSocket() { close(); }

    [[nodiscard]] std::expected<void, std::error_code> bind(const sockaddr_storage& sa, socklen_t len) {
        if (fd_ == invalid_socket) {
            if (!ensure_socket(sa.ss_family)) {
                return std::unexpected(std::make_error_code(std::errc::bad_file_descriptor));
            }
        }
        if (::bind(fd_, reinterpret_cast<const sockaddr*>(&sa), len) != 0) {
#ifdef _WIN32
            int e = WSAGetLastError();
#else
            int e = errno;
#endif
            return std::unexpected(std::error_code(e, std::system_category()));
        }
        local_addr_    = sa;
        local_addrlen_ = len;
        return {};
    }

    // Optional: already_bound() helper
    bool is_bound() const noexcept { return local_addrlen_ != 0; }

private:
    sock_t fd_{invalid_socket};
    sockaddr_storage local_addr_{};
    socklen_t local_addrlen_{0};
    sockaddr_storage peer_{}; // Zieladresse für send()
    socklen_t peer_len_{0};   // Länge der Zieladresse
    bool connected_{false};   // Connect wurde aufgerufen

public:
    // UDP connect setzt Default-Zieladresse für send/recv
    [[nodiscard]] auto async_connect(const sockaddr_storage& addr, socklen_t len) {
        struct ConnectAwaiter {
            AsyncUdpSocket* self; // Pointer to the enclosing class
            sock_t& fd;
            const sockaddr_storage& addr;
            socklen_t len;
            bool& connected;
            sockaddr_storage& peer;
            socklen_t& peer_len;

            bool await_ready() {
                // Socket erstellen falls nötig
                if (fd == invalid_socket) {
                    if (!self->ensure_socket(addr.ss_family)) { // self->
                        return true; // Report error immediately
                    }
                }

                // UDP connect blockiert nie wirklich
                int res = ::connect(fd, reinterpret_cast<const sockaddr*>(&addr), len);
                if (res == 0) {
                    connected = true;
                    peer = addr;
                    peer_len = len;
                    return true;
                }

#ifdef _WIN32
                int err = WSAGetLastError();
                if (err != WSAEWOULDBLOCK && err != WSAEINPROGRESS)
#else
                int err = errno;
                if (err != EAGAIN && err != EWOULDBLOCK && err != EINPROGRESS)
#endif
                {
                    return true; // Hard error, report immediately
                }

                // Speichern für sendto-Fallback
                peer = addr;
                peer_len = len;
                return false;
            }

            void await_suspend(std::coroutine_handle<> h) {
                // Bei UDP connected nach erfolgreichem connect sofort resumieren
                EventLoop::instance().register_write(fd, h);
            }

            std::expected<void, std::error_code> await_resume() {
#ifdef _WIN32
                int err = 0;
                int len = sizeof(err);
                if (::getsockopt(fd, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&err), &len) < 0) {
                    err = WSAGetLastError();
                }
#else
                int err = 0;
                socklen_t len = sizeof(err);
                if (::getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len) < 0) {
                    err = errno;
                }
#endif

                if (err != 0) {
                    return std::unexpected(std::error_code(err, std::system_category()));
                }

                connected = true;
                return {};
            }
        };

        return ConnectAwaiter{
            this, // pass pointer to current instance
            fd_, addr, len, connected_, peer_, peer_len_
        };
    }

    // Manually set the peer (if using connect is not desired)
    void set_peer(const sockaddr_storage& addr, socklen_t len) {
        peer_ = addr;
        peer_len_ = len;
    }

    // Hilfsfunktionen für async_read/write im GenericAwaiter-Stil

    ssize_t low_level_read(char* data, std::size_t n, std::error_code& ec) {
        ec.clear();
        if (fd_ == invalid_socket) {
            if (!ensure_socket()) {
                ec = std::make_error_code(std::errc::bad_file_descriptor);
                return -1;
            }
        }

        sockaddr_storage src{};
        socklen_t srclen = sizeof(src);
#ifdef _WIN32
        int ret = ::recvfrom(fd_, data, static_cast<int>(n), 0, 
                             reinterpret_cast<sockaddr*>(&src), &srclen);
        if (ret < 0) {
            int e = WSAGetLastError();
            if (e == WSAEWOULDBLOCK || e == WSAEINPROGRESS) {
                ec = std::make_error_code(std::errc::operation_would_block);
            } else {
                ec = std::error_code(e, std::system_category());
            }
            return -1;
        }
#else
        ssize_t ret = ::recvfrom(fd_, data, n, 0, 
                                reinterpret_cast<sockaddr*>(&src), &srclen);
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
        // debug logging removed for cleaner output
#endif
        // Remember sender for replies
        if (!connected_ && ret > 0) {
            peer_ = src;
            peer_len_ = srclen;
        }
        return ret;
    }

    ssize_t low_level_write(const char* data, std::size_t n, std::error_code& ec) {
        ec.clear();
        // debug logging removed for cleaner output

        if (fd_ == invalid_socket) {
            if (!ensure_socket()) {
                ec = std::make_error_code(std::errc::bad_file_descriptor);
                // ensure_socket() failed (previous debug log removed)
                return -1;
            }
        }

        // check status
        if (!connected_ && peer_len_ == 0) {
            ec = std::make_error_code(std::errc::destination_address_required);
            return -1;
        }

    // Output destination address
        if (peer_len_ > 0) {
            const sockaddr_in* sin = reinterpret_cast<const sockaddr_in*>(&peer_);
            char ip[INET_ADDRSTRLEN]{};
            inet_ntop(AF_INET, &(sin->sin_addr), ip, INET_ADDRSTRLEN);
            // peer info (debug removed)
        }

        ssize_t ret;
        if (connected_) {
            // using send()
#ifdef _WIN32
            ret = ::send(fd_, data, static_cast<int>(n), 0);
#else
            ret = ::send(fd_, data, n, 0);
#endif
        } else {
            // using sendto()
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
            int e = WSAGetLastError();
            if (e == WSAEWOULDBLOCK) {
                ec = std::make_error_code(std::errc::operation_would_block);
            } else {
                ec = std::error_code(e, std::system_category());
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

    [[nodiscard]] auto native_handle() const noexcept { return fd_; }

    // Mid-level read_some/write_some for use by adapters (mirror TCP API)
    [[nodiscard]] std::expected<std::size_t, std::error_code> read_some(std::span<char> buf) {
        std::error_code ec;
        ssize_t n = low_level_read(buf.data(), buf.size(), ec);
        if (n < 0) return std::unexpected(ec);
        return static_cast<std::size_t>(n);
    }

    [[nodiscard]] std::expected<std::size_t, std::error_code> write_some(std::span<const char> buf) {
        std::error_code ec;
        ssize_t n = low_level_write(reinterpret_cast<const char*>(buf.data()), buf.size(), ec);
        if (n < 0) return std::unexpected(ec);
        return static_cast<std::size_t>(n);
    }

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

    bool is_connected() const noexcept { return connected_; }
    bool has_peer() const noexcept { return peer_len_ > 0; }

    void close() {
        if (fd_ != invalid_socket) {
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

    // Create socket using ensure_socket
    bool ensure_socket(int family = AF_INET) {
        if (fd_ != invalid_socket) return true;
#ifdef _WIN32
        detail::ensure_wsa();
#endif
        fd_ = ::socket(family, SOCK_DGRAM, 0);
        if (fd_ == invalid_socket) return false;
        
    // Set socket options
        set_socket_option(fd_, SocketOption::ReuseAddr, 1);
        
    // Enable non-blocking
#ifdef _WIN32
        u_long mode = 1;
        ::ioctlsocket(fd_, FIONBIO, &mode);
#else
        int flags = ::fcntl(fd_, F_GETFL, 0);
        ::fcntl(fd_, F_SETFL, flags | O_NONBLOCK);
#endif
        return fd_ != invalid_socket;
    }

    // Füge diese Getter-Methoden hinzu:
    const sockaddr_storage& peer() const noexcept { 
        return peer_; 
    }
    
    socklen_t peer_length() const noexcept { 
        return peer_len_; 
    }
    
        // (No static_assert here: AsyncUdpSocket API is custom and may not match the generic async concepts exactly.)
};
}