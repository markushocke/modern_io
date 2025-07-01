module;

#include <cstdint>
#include <cstring>
#include <errno.h>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include <iostream>
#include <optional>

#ifdef _WIN32
  #define NOMINMAX
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #pragma comment(lib, "ws2_32.lib") // Nur wenn nötig
#else
  #include <netdb.h>
  #include <sys/socket.h>
  #include <unistd.h>
#endif

export module net_io.tcp_client;

// Module imports (sorted)
import net_io_base;
import net_io_concepts;
import net_io.tcp_endpoint;
export import net_io_base; // Export sock_t and invalid_socket

export namespace net_io
{
  /**
   * @brief TCP client for connecting to remote endpoints.
   *
   * Manages a TCP socket. Not copyable, but movable.
   * Automatically closes the socket in the destructor.
   */
  class TcpClient
  {
  public:
    explicit TcpClient(sock_t fd)
      : fd_(fd), ep_(std::nullopt)
    {}

    /// Konstruktor mit Endpoint (verbindet noch nicht)
    explicit TcpClient(const TcpEndpoint& ep)
      : ep_(ep)
    {}

    /// Not copyable
    TcpClient(const TcpClient&) = delete;
    TcpClient& operator=(const TcpClient&) = delete;

    /// Move constructor
    TcpClient(TcpClient&& other) noexcept
      : fd_(other.fd_), ep_(std::move(other.ep_))
    {
      other.fd_ = invalid_socket;
    }

    /// Move assignment
    TcpClient& operator=(TcpClient&& other) noexcept
    {
      if (this != &other) {
        close();
        fd_ = other.fd_;
        ep_ = std::move(other.ep_);
        other.fd_ = invalid_socket;
      }
      return *this;
    }

    /// Explicit destructor for resource cleanup
    ~TcpClient() { close(); }

    /**
     * @brief Open a connection to the stored TCP endpoint.
     * @throws SocketException on error
     */
    void open()
    {
      if (!ep_)
        throw SocketException("open() failed: no endpoint set", 0);

#if defined(_WIN32)
      detail::ensure_wsa();
#endif
      auto addr = ep_->to_sockaddr(false);
      socklen_t len = sizeof(addr);

      fd_ = ::socket(addr.ss_family, SOCK_STREAM, 0);
      if (fd_ == invalid_socket) {
        std::cerr << "[TcpClient] socket() failed, fd_=" << fd_ << std::endl;
        throw SocketException("socket() failed", errno);
      }

      if (::connect(
            fd_,
            reinterpret_cast<sockaddr*>(&addr),
            len
          ) < 0)
      {
#if defined(_WIN32)
        int err = WSAGetLastError();
        ::closesocket(fd_);
#else
        int err = errno;
        ::close(fd_);
#endif
        fd_ = invalid_socket;
        std::cerr << "[TcpClient] connect() failed, fd_=" << fd_ << std::endl;
        throw SocketException("connect() failed", err);
      }
      std::cerr << "[TcpClient] open() success, fd_=" << fd_ << std::endl;
    }

    /**
     * @brief Read data from the socket.
     * @param data Destination buffer
     * @param size Number of bytes
     * @return Number of bytes read
     */
    std::size_t read(char* data, std::size_t size) noexcept
    {
#if defined(_WIN32)
      int ret = ::recv(fd_, data, static_cast<int>(size), 0);
      if (ret < 0) return 0;
      return static_cast<std::size_t>(ret);
#else
      ssize_t ret = ::read(fd_, data, size);
      if (ret < 0) return 0;
      return static_cast<std::size_t>(ret);
#endif
    }

    /**
     * @brief Write data to the socket.
     * @param data Source buffer
     * @param size Number of bytes
     * @throws SocketException on error
     */
    void write(const char* data, std::size_t size)
    {
      if (fd_ == invalid_socket) {
        std::cerr << "[TcpClient] write() failed: fd_ is invalid!" << std::endl;
        throw SocketException("write() failed: socket not open", 0);
      }
#if defined(_WIN32)
      int ret = ::send(fd_, data, static_cast<int>(size), 0);
      if (ret < 0 || ret != static_cast<int>(size))
        throw SocketException("send() failed", WSAGetLastError());
#else
      ssize_t ret = ::write(fd_, data, size);
      if (ret < 0 || static_cast<std::size_t>(ret) != size)
        throw SocketException("write() failed", errno);
#endif
    }

    /**
     * @brief Close the connection (idempotent).
     */
    void close() noexcept
    {
      if (fd_ != invalid_socket) {
#if defined(_WIN32)
        ::closesocket(fd_);
#else
        ::close(fd_);
#endif
        fd_ = invalid_socket;
      }
    }

    /**
     * @brief Returns whether the socket is open.
     */
    bool is_open() const noexcept { return fd_ != invalid_socket; }

    /**
     * @brief Returns the native socket handle.
     */
    sock_t native_handle() const noexcept { return fd_; }

    /// Set the socket to nonblocking mode
    void set_nonblocking(bool enable)
    {
      set_socket_option(fd_, SocketOption::NonBlocking, enable ? 1 : 0);
    }

    /// Set read/write timeout in milliseconds
    void set_read_timeout(int ms)
    {
      set_socket_option(fd_, SocketOption::ReadTimeoutMs, ms);
    }
    void set_write_timeout(int ms)
    {
      set_socket_option(fd_, SocketOption::WriteTimeoutMs, ms);
    }

    /// Set an arbitrary socket option
    void set_option(SocketOption opt, int value)
    {
      set_socket_option(fd_, opt, value);
    }

    // Optional: Access to local/remote address (only if socket is open)
    // sockaddr_storage local_address() const;
    // sockaddr_storage remote_address() const;

  private:
    sock_t fd_{ invalid_socket };
    std::optional<TcpEndpoint> ep_;


  };
  static_assert(net_io_concepts::Transportable<net_io::TcpClient>, "TcpClient does not implement Transportable cocnept!");
}
