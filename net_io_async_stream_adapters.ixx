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
import net_io.event_loop;
import net_io.async_stream_base;
import net_io_base;
import net_io.generic_awaiter;
import net_io.async_utils;
import net_io.task; // Task / IoTask alias

export namespace net_io
{

class AsyncTcpStreamAdapter : public AsyncStreamBase {
    std::shared_ptr<AsyncTcpSocket> socket_;
public:
    explicit AsyncTcpStreamAdapter(std::shared_ptr<AsyncTcpSocket> sock)
        : socket_(std::move(sock)) {}

    // Implement AsyncStreamBase by delegating to low-level read_some/write_some via async_utils
    [[nodiscard]] net_io::IoTask<std::expected<std::size_t, std::error_code>>
    read_async(std::span<char> buf) override {
        auto fd = socket_->native_handle();
        auto start = [fd, owner = std::make_shared<int>(0)](std::coroutine_handle<> h) {
            EventLoop::instance().register_read(fd, h, owner);
        };
        auto finish = [socket = socket_, buf]() -> std::expected<std::size_t, std::error_code> {
            return socket->read_some(buf);
        };
        co_return co_await net_io::make_awaitable_with_hooks<std::expected<std::size_t, std::error_code>>(std::move(start), std::move(finish));
    }

    // Pointer/size overload to satisfy AsyncInputStream concept
    [[nodiscard]] net_io::IoTask<std::expected<std::size_t, std::error_code>>
    read_async(char* ptr, std::size_t n) {
        return read_async(std::span<char>(ptr, n));
    }

    // std::byte span overload
    [[nodiscard]] auto read_async(std::span<std::byte> data) {
        return read_async(std::span<char>(reinterpret_cast<char*>(data.data()), data.size()));
    }

    [[nodiscard]] net_io::IoTask<std::expected<std::size_t, std::error_code>>
    write_async(std::span<const char> buf) override {
        auto fd = socket_->native_handle();
        auto start = [fd, owner = std::make_shared<int>(0)](std::coroutine_handle<> h) {
            EventLoop::instance().register_write(fd, h, owner);
        };
        auto finish = [socket = socket_, buf]() -> std::expected<std::size_t, std::error_code> {
            return socket->write_some(buf);
        };
        co_return co_await net_io::make_awaitable_with_hooks<std::expected<std::size_t, std::error_code>>(std::move(start), std::move(finish));
    }

    // Pointer/size overload to satisfy AsyncOutputStream concept
    [[nodiscard]] net_io::IoTask<std::expected<std::size_t, std::error_code>>
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
    sockaddr_storage default_addr_{};
    socklen_t default_addrlen_{0};
public:
    explicit AsyncUdpStreamAdapter(std::shared_ptr<AsyncUdpSocket> sock)
        : socket_(std::move(sock)) {}

    void set_default_target(const sockaddr_storage& addr, socklen_t len) {
        default_addr_ = addr;
        default_addrlen_ = len;
        socket_->set_peer(addr, len);
    }

    // READ with automatic retry logic
    [[nodiscard]] net_io::IoTask<std::expected<std::size_t, std::error_code>> read_async(std::span<char> data) override {
        for (;;) {
            auto start = [fd = this->native_handle(), owner = std::make_shared<int>(0)](std::coroutine_handle<> h) {
                EventLoop::instance().register_read(fd, h, owner);
            };
            auto finish = [this, data]() -> std::expected<std::size_t, std::error_code> {
                std::error_code ec;
                auto n = socket_->low_level_read(data.data(), data.size(), ec);
                if (n < 0) return std::unexpected(ec);
                return static_cast<std::size_t>(n);
            };

            auto r = co_await net_io::make_awaitable_with_hooks<std::expected<std::size_t, std::error_code>>(std::move(start), std::move(finish));

            if (!r) {
                if (r.error() == std::errc::operation_would_block ||
                    r.error() == std::errc::resource_unavailable_try_again) {
                    continue; // retry
                }
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

    // WRITE with retry logic for transient errors
    [[nodiscard]] net_io::IoTask<std::expected<std::size_t, std::error_code>> write_async(std::span<const char> data) override {
        if (default_addrlen_ == 0) {
            co_return std::unexpected(std::make_error_code(std::errc::destination_address_required));
        }

        if (!socket_->has_peer()) {
            socket_->set_peer(default_addr_, default_addrlen_);
        }

        constexpr int max_retries = 8;
        int attempt = 0;

        for (;;) {
            auto start = [fd = this->native_handle(), owner = std::make_shared<int>(0)](std::coroutine_handle<> h) {
                EventLoop::instance().register_write(fd, h, owner);
            };
            auto finish = [this, data]() -> std::expected<std::size_t, std::error_code> {
                std::error_code ec;
                auto n = socket_->low_level_write(data.data(), data.size(), ec);
                if (n < 0) return std::unexpected(ec);
                return static_cast<std::size_t>(n);
            };

            auto w = co_await net_io::make_awaitable_with_hooks<std::expected<std::size_t, std::error_code>>(std::move(start), std::move(finish));

            if (w) {
                co_return std::expected<std::size_t,std::error_code>{*w};
            }

            auto ec = w.error();
            using std::errc;
            if (ec == errc::operation_would_block ||
                ec == errc::resource_unavailable_try_again) {
                if (++attempt > max_retries) {
                    co_return std::unexpected(ec);
                }
                continue; // retry
            }

            if (ec == errc::interrupted) {
                continue;
            }

            co_return std::unexpected(ec);
        }
    }
    [[nodiscard]] auto write_async(std::span<const std::byte> data) {
        return write_async(std::span<const char>(reinterpret_cast<const char*>(data.data()), data.size()));
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

} // namespace net_io
