module;

// System headers (sorted)
#include <cstdint>
#include <cstring>
#include <exception>
#include <fcntl.h>        // For nonblocking sockets
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

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

#ifndef _MSC_VER
#include <vector>
#include <string>
#include <stdexcept>
#endif


export module net_io.tcp_server;

#ifdef _MSC_VER
import <vector>;
import <string>;
import <stdexcept>;
#endif

// Module imports (sorted)
import net_io_base;
import net_io_concepts; // <--- hinzugefügt
import net_io.tcp_client;
import net_io.tcp_endpoint;
export import net_io_base; // Export sock_t and invalid_socket

export namespace net_io
{
  /**
   * @brief TCP server for accepting incoming connections.
   *
   * Manages one or more listening sockets. Not copyable, but movable.
   * Automatically closes all sockets in the destructor.
   */
  class TcpServer
  {
  public:
    /// Construct and store the endpoint, but do not start
    explicit TcpServer(const TcpEndpoint& ep)
      : endpoint_(ep)
    {
      // Konstruktor macht nichts mehr außer speichern
    }

    /// Start and bind to the stored endpoint
    void start()
    {
#if defined(_WIN32)
      detail::ensure_wsa();
#endif
      std::string port_str = std::to_string(endpoint_.port);
      const char* port_s = port_str.c_str();

      for (int family : { AF_INET, AF_INET6 })
      {
        addrinfo hints{}, *res = nullptr;
        hints.ai_family   = family;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_flags    = AI_PASSIVE;

        int err = getaddrinfo(
          family == AF_INET ? nullptr : nullptr,
          port_s,
          &hints,
          &res
        );
        if (err != 0 || !res)
          throw std::runtime_error("getaddrinfo failed");

        for (auto rp = res; rp; rp = rp->ai_next)
        {
          sock_t listen_fd = ::socket(
            rp->ai_family,
            rp->ai_socktype,
            rp->ai_protocol
          );
          if (listen_fd == invalid_socket)
            continue;

          int opt = 1;
          ::setsockopt(
            listen_fd,
            SOL_SOCKET,
            SO_REUSEADDR,
            reinterpret_cast<char*>(&opt),
            sizeof(opt)
          );

          if (rp->ai_family == AF_INET6)
          {
            int v6only = 1;
            ::setsockopt(
              listen_fd,
              IPPROTO_IPV6,
              IPV6_V6ONLY,
              reinterpret_cast<char*>(&v6only),
              sizeof(v6only)
            );
          }

          if (::bind(listen_fd,
                     rp->ai_addr,
                     static_cast<int>(rp->ai_addrlen)) < 0 ||
              ::listen(listen_fd, SOMAXCONN) < 0)
          {
#if defined(_WIN32)
            ::closesocket(listen_fd);
#else
            ::close(listen_fd);
#endif
            continue;
          }

          listen_fds_.push_back(listen_fd);
        }
        freeaddrinfo(res);
      }

      if (listen_fds_.empty())
        throw std::runtime_error("Failed to bind any socket");
    }

    /// Set all listeners to nonblocking mode
    void set_nonblocking(bool enable)
    {
      for (auto fd : listen_fds_)
        set_socket_option(fd, SocketOption::NonBlocking, enable ? 1 : 0);
    }

    /// Set accept timeout (only for select)
    void set_accept_timeout(int ms)
    {
      accept_timeout_ms_ = ms;
    }

    /// Set an arbitrary socket option on all listeners
    void set_option(SocketOption opt, int value)
    {
      for (auto fd : listen_fds_)
        set_socket_option(fd, opt, value);
    }

    /**
     * @brief Accept an incoming connection.
     * @return TcpClient for the accepted connection
     * @throws SocketException on error or timeout
     */
    TcpClient accept()
    {
      while (true)
      {
        fd_set rfds;
        FD_ZERO(&rfds);
        sock_t maxfd = 0;
        for (auto fd : listen_fds_)
        {
          FD_SET(fd, &rfds);
          if (fd > maxfd) maxfd = fd;
        }

        timeval* tvptr = nullptr;
        timeval tv;
        if (accept_timeout_ms_ >= 0) {
          tv.tv_sec = accept_timeout_ms_ / 1000;
          tv.tv_usec = (accept_timeout_ms_ % 1000) * 1000;
          tvptr = &tv;
        }

        int ready = ::select(
          static_cast<int>(maxfd + 1),
          &rfds,
          nullptr,
          nullptr,
          tvptr
        );
        if (ready < 0)
          throw SocketException("select failed", errno);

        if (ready == 0 && accept_timeout_ms_ >= 0)
          throw SocketException("accept timeout", 0);

        for (auto fd : listen_fds_)
        {
          if (FD_ISSET(fd, &rfds))
          {
            socklen_t addrlen = sizeof(sockaddr_storage);
            sockaddr_storage peer{};
            sock_t client_fd = ::accept(
              fd,
              reinterpret_cast<sockaddr*>(&peer),
              &addrlen
            );
            if (client_fd == invalid_socket)
              continue;
            return TcpClient(client_fd);
          }
        }
      }
    }

    /// Explicit destructor for resource cleanup
    ~TcpServer() { stop(); }

    /// Stop the server and close all sockets
    void stop() noexcept
    {
      for (auto fd : listen_fds_)
      {
#if defined(_WIN32)
        ::closesocket(fd);
#else
        ::close(fd);
#endif
      }
      listen_fds_.clear();
    }

  private:
    std::vector<sock_t> listen_fds_;
    int accept_timeout_ms_ = -1; // -1 = no timeout
    TcpEndpoint endpoint_; // neu: speichert den Endpoint
  };
  
  //static assert for acceptable concept
  static_assert(net_io_concepts::Acceptable<TcpServer>, "TcpServer does not implement Acceptable concept!");
}
