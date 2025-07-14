module;

// System headers (sorted)
// These headers provide platform-specific networking and socket functionality.
// On Windows, Winsock2 is used. On POSIX systems, standard BSD sockets are used.
#if defined(_WIN32)
  #pragma comment(lib, "Ws2_32.lib") // Link with Winsock2 library automatically.
  #define NOMINMAX
  #include <winsock2.h>
  #include <ws2tcpip.h>
#else
  #include <arpa/inet.h>    // Provides functions for IP address manipulation (e.g., inet_pton, inet_ntop), multicast, and broadcast.
  #include <cstdint>
  #include <cstring>
  #include <fcntl.h>        // Provides file control options, including nonblocking sockets (O_NONBLOCK).
  #include <netdb.h>        // Provides network database operations (e.g., getaddrinfo).
  #include <sys/socket.h>   // Core socket API (socket, bind, connect, etc.).
  #include <sys/types.h>
  #include <unistd.h>       // Provides close(), read(), write(), and other POSIX APIs.
#endif

// Standard library headers (sorted)
// These headers provide threading, synchronization, and utility types.
#include <mutex>
#include <optional>

#ifndef _MSC_VER
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#endif

export module net_io_base;

#ifdef _MSC_VER
import <stdexcept>;
import <string>;
import <utility>;
import <vector>;
#endif

// --- Platform-specific helpers ---
// These helpers ensure correct initialization of platform-specific networking subsystems.
#if defined(_WIN32)
namespace detail
{
  // Ensures that WSAStartup is called only once for the process.
  inline std::once_flag wsa_flag;

  /**
   * @brief Ensures that the Windows Sockets API (WSA) is initialized.
   *
   * This function is safe to call multiple times; initialization will only occur once.
   * It also registers WSACleanup to be called at program exit.
   *
   * Example:
   * @code
   * detail::ensure_wsa();
   * @endcode
   */
  export inline void ensure_wsa()
  {
    std::call_once(wsa_flag, []()
    {
      WSADATA wsa;
      if (WSAStartup(MAKEWORD(2,2), &wsa) != 0)
        throw std::runtime_error("WSAStartup failed");
      std::atexit([]()
      {
        WSACleanup();
      });
    });
  }
}
#endif

namespace net_io
{
# if defined(_WIN32)
  // Type alias for socket handles on Windows.
  export using sock_t = SOCKET;
#  ifndef INVALID_SOCKET
#    define INVALID_SOCKET static_cast<SOCKET>(-1)
#  endif
  // Constant representing an invalid socket handle on Windows.
  export inline constexpr sock_t invalid_socket = INVALID_SOCKET;
# else
  // Type alias for socket handles on POSIX systems.
  export using sock_t = int;
  // Constant representing an invalid socket handle on POSIX systems.
  export inline constexpr sock_t invalid_socket = -1;
# endif

  /**
   * @brief Exception type for socket errors, providing additional context.
   *
   * This exception extends std::runtime_error and includes the error code and
   * optionally the peer address involved in the error.
   *
   * Example:
   * @code
   * throw SocketException("Connection failed", errno, "192.168.1.1:1234");
   * @endcode
   */
  export class SocketException : public std::runtime_error
  {
  public:
    SocketException(const std::string& msg, int err, std::optional<std::string> peer = std::nullopt)
      : std::runtime_error(msg), error_code_(err), peer_(peer) {}
    int error_code() const noexcept { return error_code_; }
    std::optional<std::string> peer() const noexcept { return peer_; }
  private:
    int error_code_;
    std::optional<std::string> peer_;
  };

  /**
   * @brief Enumeration of common socket options for cross-platform configuration.
   *
   * These options can be set using set_socket_option() to control socket behavior.
   * - ReuseAddr: Allows reuse of local addresses.
   * - KeepAlive: Enables TCP keepalive packets.
   * - Broadcast: Enables sending of broadcast packets (UDP).
   * - NonBlocking: Sets the socket to non-blocking mode.
   * - ReadTimeoutMs: Sets the receive timeout in milliseconds.
   * - WriteTimeoutMs: Sets the send timeout in milliseconds.
   */
  export enum class SocketOption
  {
    ReuseAddr,
    KeepAlive,
    Broadcast,
    NonBlocking,
    ReadTimeoutMs,
    WriteTimeoutMs
  };

  /**
   * @brief Sets a socket option in a cross-platform way.
   *
   * This function abstracts away platform-specific details for setting common socket options.
   * Not all options are supported on all platforms or socket types.
   *
   * Example:
   * @code
   * set_socket_option(fd, SocketOption::ReuseAddr, 1);
   * set_socket_option(fd, SocketOption::NonBlocking, 1);
   * @endcode
   *
   * @param fd The socket handle.
   * @param opt The socket option to set.
   * @param value The value to set for the option.
   */
  export inline void set_socket_option(sock_t fd, SocketOption opt, int value)
  {
    switch(opt)
    {
      case SocketOption::ReuseAddr:
      {
        ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<char*>(&value), sizeof(value));
        break;
      }
      case SocketOption::KeepAlive:
      {
        ::setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, reinterpret_cast<char*>(&value), sizeof(value));
        break;
      }
      case SocketOption::Broadcast:
      {
        ::setsockopt(fd, SOL_SOCKET, SO_BROADCAST, reinterpret_cast<char*>(&value), sizeof(value));
        break;
      }
      case SocketOption::NonBlocking:
      {
#if defined(_WIN32)
        u_long mode = value;
        ::ioctlsocket(fd, FIONBIO, &mode);
#else
        int flags = ::fcntl(fd, F_GETFL, 0);
        if (flags < 0) break;
        if (value)
          ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
        else
          ::fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);
#endif
        break;
      }
      case SocketOption::ReadTimeoutMs:
      {
#if defined(_WIN32)
        ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<char*>(&value), sizeof(value));
#else
        struct timeval tv;
        tv.tv_sec = value / 1000;
        tv.tv_usec = (value % 1000) * 1000;
        ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif
        break;
      }
      case SocketOption::WriteTimeoutMs:
      {
#if defined(_WIN32)
        ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<char*>(&value), sizeof(value));
#else
        struct timeval tv;
        tv.tv_sec = value / 1000;
        tv.tv_usec = (value % 1000) * 1000;
        ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
#endif
        break;
      }
    }
  }

} // namespace net_io
