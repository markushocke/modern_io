module;

// System headers (sorted)
#if defined(_WIN32)
  #pragma comment(lib, "Ws2_32.lib")
  #define NOMINMAX
  #include <winsock2.h>
  #include <ws2tcpip.h>
#else
  #include <arpa/inet.h>    // For multicast/broadcast
  #include <cstdint>
  #include <cstring>
  #include <fcntl.h>        // For nonblocking sockets
  #include <netdb.h>
  #include <sys/socket.h>
  #include <sys/types.h>
  #include <unistd.h>
#endif

// Standard library headers (sorted)
#include <mutex>
#include <optional>

export module net_io_base;
import <stdexcept>;
import <string>;
import <utility>;
import <vector>;

// --- Platform-specific helpers ---
#if defined(_WIN32)
namespace detail {
  inline std::once_flag wsa_flag;

  // Ensure WSAStartup is called once for Windows sockets
  export inline void ensure_wsa()
  {
    std::call_once(wsa_flag, []() {
      WSADATA wsa;
      if (WSAStartup(MAKEWORD(2,2), &wsa) != 0)
        throw std::runtime_error("WSAStartup failed");
      std::atexit([](){
        WSACleanup();
      });
    });
  }
}
#endif

namespace net_io
{
# if defined(_WIN32)
  export using sock_t = SOCKET;
#  ifndef INVALID_SOCKET
#    define INVALID_SOCKET static_cast<SOCKET>(-1)
#  endif
  export inline constexpr sock_t invalid_socket = INVALID_SOCKET;
# else
  export using sock_t = int;
  export inline constexpr sock_t invalid_socket = -1;
# endif

  /**
   * @brief Exception with additional context for socket errors.
   */
  export class SocketException : public std::runtime_error {
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
   * @brief Enum for common socket options.
   */
  export enum class SocketOption {
    ReuseAddr,
    KeepAlive,
    Broadcast,
    NonBlocking,
    ReadTimeoutMs,
    WriteTimeoutMs
  };

  /**
   * @brief Helper function to set socket options in a cross-platform way.
   */
  export inline void set_socket_option(sock_t fd, SocketOption opt, int value) {
    switch(opt) {
      case SocketOption::ReuseAddr: {
        ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<char*>(&value), sizeof(value));
        break;
      }
      case SocketOption::KeepAlive: {
        ::setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, reinterpret_cast<char*>(&value), sizeof(value));
        break;
      }
      case SocketOption::Broadcast: {
        ::setsockopt(fd, SOL_SOCKET, SO_BROADCAST, reinterpret_cast<char*>(&value), sizeof(value));
        break;
      }
      case SocketOption::NonBlocking: {
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
      case SocketOption::ReadTimeoutMs: {
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
      case SocketOption::WriteTimeoutMs: {
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

}
