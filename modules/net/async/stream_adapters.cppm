module;

#include <span>
#include <expected>
#include <memory>
#include <system_error>
#include <functional>
#include <coroutine>
#include <iostream>

#ifdef _WIN32
  #define NOMINMAX
  #include <winsock2.h>
  #include <ws2tcpip.h>
#else
  #include <netdb.h>
  #include <sys/socket.h>
  #include <unistd.h>
#endif

export module net_io.async_stream_adapters;

import net_io.async_tcp_socket;
import net_io.async_udp_socket;
import modern_io.connection_arena;
import modern_io.async_buffered;
import net_io.event_loop;
import net_io.async_stream_base;
import net_io_base;
import net_io.generic_awaiter;
import net_io.async_utils;
import net_io.task; // Task / IoTask alias

export namespace modern::net
{

class AsyncTcpStreamAdapter : public AsyncStreamBase {
    std::shared_ptr<AsyncTcpSocket> socket_;
    std::shared_ptr<modern_io::ConnectionArena> arena_;

    static std::shared_ptr<modern_io::ConnectionArena>
    normalize_arena(
        const std::shared_ptr<AsyncTcpSocket>& socket,
        std::shared_ptr<modern_io::ConnectionArena> arena) {
        if (arena) {
            return arena;
        }
        if (socket) {
            auto socket_arena = socket->connection_arena_handle();
            if (socket_arena) {
                return socket_arena;
            }
        }
        return std::make_shared<modern_io::ConnectionArena>();
    }

public:
    explicit AsyncTcpStreamAdapter(
        std::shared_ptr<AsyncTcpSocket> sock,
        std::shared_ptr<modern_io::ConnectionArena> arena = {})
        : socket_(std::move(sock)), arena_(normalize_arena(socket_, std::move(arena))) {}

    [[nodiscard]] modern_io::ConnectionArena& connection_arena() noexcept { return *arena_; }
    [[nodiscard]] const modern_io::ConnectionArena& connection_arena() const noexcept { return *arena_; }
    [[nodiscard]] std::shared_ptr<modern_io::ConnectionArena> connection_arena_handle() const noexcept { return arena_; }

    template<std::size_t BufSize = 8192>
    [[nodiscard]] auto buffered_input() const {
        return modern_io::AsyncBufferedInputStream<AsyncTcpStreamAdapter, BufSize>(*this, *arena_);
    }

    template<std::size_t BufSize = 8192>
    [[nodiscard]] auto buffered_output() const {
        return modern_io::AsyncBufferedOutputStream<AsyncTcpStreamAdapter, BufSize>(*this, *arena_);
    }

    // Implement AsyncStreamBase by delegating to low-level read_some/write_some via async_utils
    [[nodiscard]] IoTask<std::expected<std::size_t, std::error_code>>
    read_async(std::span<char> buf) override {
        auto fd = socket_->native_handle();
        auto start = [fd, &loop = socket_->event_loop(), owner = std::make_shared<int>(0)](std::coroutine_handle<> h) {
            loop.register_io(make_io_registration(fd, IOEvent::Read, h, owner));
        };
        auto finish = [socket = socket_, buf]() -> std::expected<std::size_t, std::error_code> {
            return socket->read_some(buf);
        };
        co_return co_await make_awaitable_with_hooks<std::expected<std::size_t, std::error_code>>(std::move(start), std::move(finish));
    }

    // Pointer/size overload to satisfy AsyncInputStream concept
    [[nodiscard]] IoTask<std::expected<std::size_t, std::error_code>>
    read_async(char* ptr, std::size_t n) {
        return read_async(std::span<char>(ptr, n));
    }

    // std::byte span overload
    [[nodiscard]] auto read_async(std::span<std::byte> data) {
        return read_async(std::span<char>(reinterpret_cast<char*>(data.data()), data.size()));
    }

    [[nodiscard]] IoTask<std::expected<std::size_t, std::error_code>>
    write_async(std::span<const char> buf) override {
        auto fd = socket_->native_handle();
        auto start = [fd, &loop = socket_->event_loop(), owner = std::make_shared<int>(0)](std::coroutine_handle<> h) {
            loop.register_io(make_io_registration(fd, IOEvent::Write, h, owner));
        };
        auto finish = [socket = socket_, buf]() -> std::expected<std::size_t, std::error_code> {
            return socket->write_some(buf);
        };
        co_return co_await make_awaitable_with_hooks<std::expected<std::size_t, std::error_code>>(std::move(start), std::move(finish));
    }

    // Pointer/size overload to satisfy AsyncOutputStream concept
    [[nodiscard]] IoTask<std::expected<std::size_t, std::error_code>>
    write_async(const char* ptr, std::size_t n) {
        return write_async(std::span<const char>(ptr, n));
    }

    // std::byte span overload
    [[nodiscard]] auto write_async(std::span<const std::byte> data) {
        return write_async(std::span<const char>(reinterpret_cast<const char*>(data.data()), data.size()));
    }

    [[nodiscard]] std::expected<bool, std::error_code> eof() override { 
        return false; // TCP streams usually don't have EOF
    }
    
    [[nodiscard]] std::expected<void, std::error_code> flush() override {
        return {}; // TCP flush is normally not necessary
    }

    int native_handle() const noexcept override { return socket_->native_handle(); }
};

class AsyncUdpStreamAdapter : public AsyncStreamBase {
    std::shared_ptr<AsyncUdpSocket> socket_;
    std::shared_ptr<modern_io::ConnectionArena> arena_;
    sockaddr_storage default_addr_{};
    socklen_t default_addrlen_{0};

    static std::shared_ptr<modern_io::ConnectionArena>
    normalize_arena(
        const std::shared_ptr<AsyncUdpSocket>& socket,
        std::shared_ptr<modern_io::ConnectionArena> arena) {
        if (arena) {
            return arena;
        }
        if (socket) {
            auto socket_arena = socket->connection_arena_handle();
            if (socket_arena) {
                return socket_arena;
            }
        }
        return std::make_shared<modern_io::ConnectionArena>();
    }

public:
    explicit AsyncUdpStreamAdapter(
        std::shared_ptr<AsyncUdpSocket> sock,
        std::shared_ptr<modern_io::ConnectionArena> arena = {})
        : socket_(std::move(sock)), arena_(normalize_arena(socket_, std::move(arena))) {}

    [[nodiscard]] modern_io::ConnectionArena& connection_arena() noexcept { return *arena_; }
    [[nodiscard]] const modern_io::ConnectionArena& connection_arena() const noexcept { return *arena_; }
    [[nodiscard]] std::shared_ptr<modern_io::ConnectionArena> connection_arena_handle() const noexcept { return arena_; }

    template<std::size_t BufSize = 8192>
    [[nodiscard]] auto buffered_input() const {
        return modern_io::AsyncBufferedInputStream<AsyncUdpStreamAdapter, BufSize>(*this, *arena_);
    }

    template<std::size_t BufSize = 8192>
    [[nodiscard]] auto buffered_output() const {
        return modern_io::AsyncBufferedOutputStream<AsyncUdpStreamAdapter, BufSize>(*this, *arena_);
    }

    void set_default_target(const sockaddr_storage& addr, socklen_t len) {
        default_addr_ = addr;
        default_addrlen_ = len;
        socket_->set_peer(addr, len);
    }

    // READ with automatic retry logic
    [[nodiscard]] IoTask<std::expected<std::size_t, std::error_code>> read_async(std::span<char> data) override {
        for (;;) {
            auto r = co_await socket_->async_read(data);
            if (!r) {
                co_return std::unexpected(r.error());
            }

            auto n = r.value();
            if (n == 0) {
                // Empty datagram, ignore and continue
                continue;
            }
            co_return std::expected<std::size_t,std::error_code>{n};
        }
    }

    [[nodiscard]] auto read_async(std::span<std::byte> data) {
        return read_async(std::span<char>(reinterpret_cast<char*>(data.data()), data.size()));
    }

    [[nodiscard]] IoTask<std::expected<std::size_t, std::error_code>> read_async(char* ptr, std::size_t n) {
        return read_async(std::span<char>(ptr, n));
    }

    // WRITE with retry logic for transient errors
    [[nodiscard]] IoTask<std::expected<std::size_t, std::error_code>> write_async(std::span<const char> data) override {
        if (!socket_->has_peer()) {
            if (default_addrlen_ == 0) {
                co_return std::unexpected(std::make_error_code(std::errc::destination_address_required));
            }
            socket_->set_peer(default_addr_, default_addrlen_);
        }

        co_return co_await socket_->async_write(data);
    }
    [[nodiscard]] auto write_async(std::span<const std::byte> data) {
        return write_async(std::span<const char>(reinterpret_cast<const char*>(data.data()), data.size()));
    }
    [[nodiscard]] IoTask<std::expected<std::size_t, std::error_code>> write_async(const char* ptr, std::size_t n) {
        return write_async(std::span<const char>(ptr, n));
    }
    [[nodiscard]] auto write_async(const std::string& s) {
        return write_async(std::span<const char>(s.data(), s.size()));
    }

    // Optional: explicitly send to target (sets peer, then async_write)
    [[nodiscard]] auto write_to_async(const sockaddr_storage& addr, socklen_t len, std::span<const char> data) {
        socket_->set_peer(addr, len);
        return socket_->async_write(data);
    }

    // UDP connect (sets peer)
    [[nodiscard]] auto connect_async(const sockaddr_storage& addr, socklen_t len) {
        set_default_target(addr, len);
        return socket_->async_connect(addr, len);
    }

    bool is_connected() const noexcept { return socket_->is_connected(); }
    bool has_peer()      const noexcept { return socket_->has_peer(); }

    void sync_from_socket_peer() {
        if (socket_->has_peer()) {
            default_addr_ = socket_->peer();
            default_addrlen_ = socket_->peer_length();
        }
    }

    [[nodiscard]] std::expected<bool, std::error_code> eof() override { return false; }
    [[nodiscard]] std::expected<void, std::error_code> flush() override { return {}; }
    int native_handle() const noexcept override { return socket_->native_handle(); }
};

} // namespace modern::net

export namespace net_io {

using modern::net::AsyncTcpStreamAdapter;
using modern::net::AsyncUdpStreamAdapter;

} // namespace net_io
