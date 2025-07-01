module;

#include <string>
#include <stdexcept>
#include <cstring>
#include <utility>
#include <cstdint>

#ifdef _WIN32
  #define NOMINMAX
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #pragma comment(lib, "ws2_32.lib") // Nur wenn nötig
#else
  #include <netdb.h>
#endif

// This module provides a TCP endpoint abstraction for network I/O operations.

export module net_io.tcp_endpoint;
import net_io_base;
export import net_io_base; // sock_t und invalid_socket sichtbar machen

export namespace net_io
{
  struct TcpEndpoint
  {
    std::string address;
    uint16_t    port;

    TcpEndpoint(std::string addr, uint16_t p)
      : address(std::move(addr))
      , port(p)
    {
      if (address.empty()) {
        throw std::invalid_argument("TcpEndpoint: empty address");
      }
    }

    sockaddr_storage to_sockaddr(bool passive = false) const
    {
      addrinfo hints{}, *res = nullptr;
      hints.ai_family   = AF_UNSPEC;
      hints.ai_socktype = SOCK_STREAM;
      hints.ai_flags    = passive ? AI_PASSIVE : 0;

      std::string port_s = std::to_string(port);
      int err = getaddrinfo(
        passive ? nullptr : address.c_str(),
        port_s.c_str(),
        &hints,
        &res
      );
      if (err != 0 || !res) {
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
