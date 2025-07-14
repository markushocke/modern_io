module;
#ifndef _MSC_VER
#include <string>
#include <stdexcept>
#include <cstring>
#include <utility>
#include <cstdint>
#endif

#ifdef _WIN32
  #define NOMINMAX
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #pragma comment(lib, "ws2_32.lib") // Only needed for static linking on Windows.
#else
  #include <netdb.h>
#endif

// This module provides a TCP endpoint abstraction for network I/O operations.
// A TCP endpoint represents an address and port for connecting or binding sockets.

export module net_io.tcp_endpoint;
import net_io_base;
export import net_io_base; // Make sock_t and invalid_socket visible to users of this module.

#ifdef _MSC_VER
import <string>;
import <stdexcept>;
import <cstring>;
import <utility>;
import <cstdint>;
#endif

export namespace net_io
{
  /**
   * @brief Represents a TCP endpoint (address and port).
   *
   * This struct encapsulates the information needed to connect to or bind a TCP socket.
   * It provides a utility function to convert the endpoint to a sockaddr_storage structure,
   * which is required by low-level socket APIs.
   *
   * Example usage:
   * @code
   * net_io::TcpEndpoint ep("127.0.0.1", 8080);
   * sockaddr_storage addr = ep.to_sockaddr();
   * @endcode
   */
  struct TcpEndpoint
  {
    std::string address; ///< The IP address or hostname (e.g., "127.0.0.1" or "example.com").
    uint16_t    port;    ///< The TCP port number.

    /**
     * @brief Constructs a TCP endpoint from an address and port.
     * @param addr The IP address or hostname.
     * @param p The TCP port number.
     * @throws std::invalid_argument if the address is empty.
     */
    TcpEndpoint(std::string addr, uint16_t p)
      : address(std::move(addr))
      , port(p)
    {
      if (address.empty())
      {
        throw std::invalid_argument("TcpEndpoint: empty address");
      }
    }

    /**
     * @brief Converts the endpoint to a sockaddr_storage structure.
     *
     * This function resolves the address and port using getaddrinfo and fills a sockaddr_storage
     * structure suitable for use with socket APIs (e.g., connect, bind).
     *
     * @param passive If true, the address is resolved for binding (AI_PASSIVE).
     *                If false, the address is resolved for connecting.
     * @return sockaddr_storage structure representing the endpoint.
     * @throws std::runtime_error if address resolution fails.
     *
     * Example:
     * @code
     * net_io::TcpEndpoint ep("localhost", 1234);
     * sockaddr_storage addr = ep.to_sockaddr();
     * @endcode
     */
    sockaddr_storage to_sockaddr(bool passive = false) const
    {
      addrinfo hints{}, *res = nullptr;
      hints.ai_family   = AF_UNSPEC;      // Allow IPv4 or IPv6.
      hints.ai_socktype = SOCK_STREAM;    // TCP sockets.
      hints.ai_flags    = passive ? AI_PASSIVE : 0;

      std::string port_s = std::to_string(port);
      int err = getaddrinfo(
        passive ? nullptr : address.c_str(),
        port_s.c_str(),
        &hints,
        &res
      );
      if (err != 0 || !res)
      {
        std::string msg = "getaddrinfo failed: ";
#if defined(_WIN32)
        msg += std::to_string(WSAGetLastError());
#else
        msg += gai_strerror(err);
#endif
        throw std::runtime_error(msg);
      }

      sockaddr_storage storage;
      std::memset(&storage, 0, sizeof(storage));
      std::memcpy(&storage, res->ai_addr, res->ai_addrlen);
      freeaddrinfo(res);
      return storage;
    }
  };
}
