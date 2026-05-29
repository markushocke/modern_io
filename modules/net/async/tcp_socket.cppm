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

import modern_io.connection_arena;
import net_io_base;
import net_io.generic_awaiter;
import net_io.event_loop;
import net_io.async_utils;

namespace modern::net {

using net_io::invalid_socket;
using net_io::set_socket_option;
using net_io::sock_t;
using net_io::SocketOption;

// ---------------- AsyncTcpSocket Class ----------------
export class AsyncTcpSocket : public std::enable_shared_from_this<AsyncTcpSocket> {
    static std::shared_ptr<modern_io::ConnectionArena>
    normalize_arena(std::shared_ptr<modern_io::ConnectionArena> arena) {
        if (arena) {
            return arena;
        }
        return std::make_shared<modern_io::ConnectionArena>();
    }

public:
    explicit AsyncTcpSocket(
        EventReactor& loop = default_event_reactor(),
        std::shared_ptr<modern_io::ConnectionArena> arena = {});

    explicit AsyncTcpSocket(
        EventReactor& loop,
        modern_io::ConnectionArenaSettings arena_settings);

    explicit AsyncTcpSocket(
        sock_t fd,
        bool already_nonblocking = false,
        EventReactor& loop = default_event_reactor(),
        std::shared_ptr<modern_io::ConnectionArena> arena = {});

    explicit AsyncTcpSocket(
        sock_t fd,
        bool already_nonblocking,
        EventReactor& loop,
        modern_io::ConnectionArenaSettings arena_settings);

    ~AsyncTcpSocket();

    void close();

    // ---------------- Low-level read/write ----------------
    ssize_t low_level_read(char* data, std::size_t n, std::error_code& ec);

    ssize_t low_level_write(const char* data, std::size_t n, std::error_code& ec);

    // ---------------- Mid-level helpers for async_utils ----------------
    [[nodiscard]] std::expected<std::size_t, std::error_code> read_some(std::span<char> buf);

    [[nodiscard]] std::expected<std::size_t, std::error_code> write_some(std::span<const char> buf);

    [[nodiscard]] auto native_handle() const noexcept { return fd_; }

    [[nodiscard]] EventReactor& event_loop() noexcept { return *loop_; }
    [[nodiscard]] const EventReactor& event_loop() const noexcept { return *loop_; }
    [[nodiscard]] modern_io::ConnectionArena& connection_arena() noexcept { return *arena_; }
    [[nodiscard]] const modern_io::ConnectionArena& connection_arena() const noexcept { return *arena_; }
    [[nodiscard]] std::shared_ptr<modern_io::ConnectionArena> connection_arena_handle() const noexcept { return arena_; }

    [[nodiscard]] auto async_read(std::span<char> buf) {
        return read_some_async_on(
            *loop_,
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
        return write_some_async_on(
            *loop_,
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
    bool ensure_socket(int family = AF_INET);
    
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
                self->event_loop().register_io(make_io_registration(fd, IOEvent::Write, h));
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
    EventReactor* loop_;
    std::shared_ptr<modern_io::ConnectionArena> arena_;
};
} // namespace modern::net

export namespace net_io {

using modern::net::AsyncTcpSocket;

} // namespace net_io
