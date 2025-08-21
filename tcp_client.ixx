module;

#include <errno.h>

#ifdef _WIN32
  #define NOMINMAX
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #pragma comment(lib, "ws2_32.lib")
#else
  #include <netdb.h>
  #include <sys/socket.h>
  #include <unistd.h>
#endif

#ifndef _MSC_VER
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include <iostream>
#include <optional>
#endif

export module net_io.tcp_client;

#ifdef _MSC_VER
import <cstdint>;
import <cstring>;
import <stdexcept>;
import <string>;
import <utility>;
import <vector>;
import <iostream>;
import <optional>;
#endif

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
   * This class manages a TCP socket connection to a remote endpoint.
   * The class is not copyable, but is movable. The socket is automatically
   * closed in the destructor to ensure resource safety.
   *
   * Example usage:
   * @code
   * net_io::TcpEndpoint ep("127.0.0.1", 8080);
   * net_io::TcpClient client(ep);
   * client.open();
   * client.write("hello", 5);
   * char buf[128];
   * std::size_t n = client.read(buf, sizeof(buf));
   * client.close();
   * @endcode
   */
  class TcpClient
  {
  public:
    /**
     * @brief Construct a TcpClient from an existing socket handle.
     * @param fd The socket handle (must be a valid TCP socket).
     */
    explicit TcpClient(sock_t fd)
      : fd_(fd), ep_(std::nullopt)
    {}

    /**
     * @brief Construct a TcpClient with a TCP endpoint (does not connect yet).
     * @param ep The TCP endpoint to connect to.
     */
    explicit TcpClient(const TcpEndpoint& ep)
      : ep_(ep)
    {}

    // Not copyable: copying a socket client is not allowed.
    TcpClient(const TcpClient&) = delete;
    TcpClient& operator=(const TcpClient&) = delete;

    /**
     * @brief Move constructor.
     *
     * Transfers ownership of the socket and endpoint from another TcpClient.
     * The moved-from object is left in a valid but unspecified state.
     */
    TcpClient(TcpClient&& other) noexcept
      : fd_(other.fd_), ep_(std::move(other.ep_))
    {
      other.fd_ = invalid_socket;
    }

    /**
     * @brief Move assignment operator.
     *
     * Transfers ownership of the socket and endpoint from another TcpClient.
     * The moved-from object is left in a valid but unspecified state.
     */
    TcpClient& operator=(TcpClient&& other) noexcept
    {
      if (this != &other)
      {
        close();
        fd_ = other.fd_;
        ep_ = std::move(other.ep_);
        other.fd_ = invalid_socket;
      }
      return *this;
    }

    /**
     * @brief Destructor. Closes the socket if it is open.
     */
    ~TcpClient()
    {
      // Niemals Exception im Destruktor werfen!
      try {
        close();
      } catch (...) {
        // Fehler ignorieren, niemals Exception propagieren
      }
    }

    /**
     * @brief Open a connection to the stored TCP endpoint.
     * @throws SocketException if the endpoint is not set or if the connection fails.
     *
     * This method creates a socket and connects to the endpoint specified in the constructor.
     * On failure, the socket is closed and an exception is thrown.
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
      if (fd_ == invalid_socket)
      {
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
     * @param data Pointer to the destination buffer.
     * @param size Number of bytes to read.
     * @return Number of bytes actually read, or 0 on error.
     *
     * This method reads up to 'size' bytes from the socket into 'data'.
     * Returns the number of bytes read, or 0 if an error occurs.
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
     * @param data Pointer to the source buffer.
     * @param size Number of bytes to write.
     * @throws SocketException if the socket is not open or if the write fails.
     *
     * Writes data to the socket.
     * Throws a SocketException if the socket is not open or if the write fails.
     * Example:
     * @code
     * client.write("hello", 5);
     * @endcode
     */
    void write(const char* data, std::size_t size)
    {
      if (fd_ == invalid_socket)
      {
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
     *
     * Closes the socket if it is open. This operation is idempotent:
     * calling close() multiple times is safe and has no effect after the first call.
     * The socket handle is set to invalid_socket after closing.
     *
     * Example:
     * @code
     * client.close();
     * @endcode
     */
    void close() noexcept
    {
      if (fd_ != invalid_socket)
      {
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
     *
     * @return true if the socket is open and valid, false otherwise.
     *
     * Example:
     * @code
     * if (client.is_open()) { ... }
     * @endcode
     */
    bool is_open() const noexcept
    {
      return fd_ != invalid_socket;
    }

    /**
     * @brief Returns the native socket handle.
     * @return The socket handle (platform-dependent type).
     */
    sock_t native_handle() const noexcept
    {
      return fd_;
    }

    /**
     * @brief Set the socket to nonblocking mode.
     * @param enable True to enable nonblocking mode, false to disable.
     *
     * Example:
     * @code
     * client.set_nonblocking(true);
     * @endcode
     */
    void set_nonblocking(bool enable)
    {
      set_socket_option(fd_, SocketOption::NonBlocking, enable ? 1 : 0);
    }

    /**
     * @brief Set the read timeout in milliseconds.
     * @param ms Timeout in milliseconds.
     *
     * Example:
     * @code
     * client.set_read_timeout(1000); // 1 second
     * @endcode
     */
    void set_read_timeout(int ms)
    {
      set_socket_option(fd_, SocketOption::ReadTimeoutMs, ms);
    }

    /**
     * @brief Set the write timeout in milliseconds.
     * @param ms Timeout in milliseconds.
     *
     * Example:
     * @code
     * client.set_write_timeout(1000); // 1 second
     * @endcode
     */
    void set_write_timeout(int ms)
    {
      set_socket_option(fd_, SocketOption::WriteTimeoutMs, ms);
    }

    /**
     * @brief Set an arbitrary socket option.
     * @param opt The socket option to set.
     * @param value The value to set for the option.
     *
     * Example:
     * @code
     * client.set_option(SocketOption::KeepAlive, 1);
     * @endcode
     */
    void set_option(SocketOption opt, int value)
    {
      set_socket_option(fd_, opt, value);
    }

    // Optional: Access to local/remote address (only if socket is open)
    // sockaddr_storage local_address() const;
    // sockaddr_storage remote_address() const;

  private:
    sock_t fd_{ invalid_socket }; ///< The socket handle.
    std::optional<TcpEndpoint> ep_; ///< The endpoint to connect to (if any).
  };

  // Compile-time check: ensure TcpClient satisfies the Transportable concept.
  static_assert(net_io_concepts::Transportable<net_io::TcpClient>, "TcpClient does not implement Transportable concept!");
}
