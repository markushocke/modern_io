module;

#ifndef _MSC_VER
#include <cstdint>
#include <cstring>
#include <errno.h>
#include <iostream>
#include <fcntl.h>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#endif

#ifdef _WIN32
  #define NOMINMAX
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #include <mstcpip.h> 
  #pragma comment(lib, "ws2_32.lib") // Nur wenn nötig
#else
  #include <arpa/inet.h>    // For multicast/broadcast
  #include <sys/socket.h>
  #include <unistd.h>
  #include <netdb.h>
#endif

export module net_io.udp_transport;

#ifdef _MSC_VER
import <cstdint>;
import <cstring>;
import <iostream>;
import <fcntl.h>;
import <stdexcept>;
import <string>;
import <utility>;
import <vector>;
#endif

// Module imports (sorted)
import net_io_base;
import net_io_concepts; // <--- hinzugefügt
import net_io.udp_endpoint;
export import net_io_base; // Export sock_t and invalid_socket

export namespace net_io
{

/**
 * @brief UDP transport für Datagram-Kommunikation.
 *
 * - Für Server: open_bind() → nur bind(), empfängt von allen Peers, sendet gezielt mit write_to().
 * - Für Client: open_connect() → bind()+connect(), sendet/empfängt wie TCP.
 * - write() nutzt connect()-Peer (nur im Client-Modus sinnvoll).
 */
class UdpTransport
{
public:
    /// Default constructor (no socket open)
    UdpTransport() = default;

    /// Constructor with existing socket handle
    explicit UdpTransport(sock_t fd) noexcept
      : fd_(fd)
    {}

    /// Not copyable
    UdpTransport(const UdpTransport&) = delete;
    UdpTransport& operator=(const UdpTransport&) = delete;

    /// Move constructor
    UdpTransport(UdpTransport&& other) noexcept
      : fd_(other.fd_)
    {
      other.fd_ = invalid_socket;
    }

    /// Move assignment
    UdpTransport& operator=(UdpTransport&& other) noexcept
    {
      if (this != &other)
      {
        close();
        fd_ = other.fd_;
        other.fd_ = invalid_socket;
      }
      return *this;
    }

    /// Destructor: automatically closes the socket
    ~UdpTransport()
    {
      close();
    }

    /**
     * @brief UDP-Server: Nur bind() auf lokale Adresse/Port, kein connect().
     * @param ep Ziel-Endpunkt (address, port, bind_local, local_port)
     * @throws std::runtime_error on error
     */
    void open_bind(const UdpEndpoint& ep)
    {
#if defined(_WIN32)
        detail::ensure_wsa();
#endif
        auto addr_local = ep.to_sockaddr(true);
        socklen_t len   = sizeof(addr_local);

        fd_ = ::socket(addr_local.ss_family, SOCK_DGRAM, 0);
        if (fd_ == invalid_socket)
        {
#if defined(_WIN32)
            int err = WSAGetLastError();
            throw std::runtime_error("UDP socket() failed: WSA error " + std::to_string(err));
#else
            int err = errno;
            throw std::runtime_error("UDP socket() failed: " + std::string(std::strerror(err)));
#endif
        }
#if defined(_WIN32)
        if (addr_local.ss_family == AF_INET6)
        {
            BOOL bFalse = FALSE;
            ::setsockopt(
                fd_,
                IPPROTO_IPV6,
                IPV6_V6ONLY,
                reinterpret_cast<char*>(&bFalse),
                sizeof(bFalse)
            );
        }
#endif
        if (::bind(fd_, reinterpret_cast<sockaddr*>(&addr_local), len) != 0)
        {
#if defined(_WIN32)
            int err = WSAGetLastError();
            throw std::runtime_error("UDP bind() failed: WSA error " + std::to_string(err));
#else
            int err = errno;
            throw std::runtime_error("UDP bind() failed: errno " + std::to_string(err) +
                                    " (" + std::strerror(err) + ")");
#endif
        }
    }

    /**
     * @brief UDP-Client: bind() (optional) und connect() auf Zieladresse.
     * @param ep Ziel-Endpunkt (address, port, bind_local, local_port)
     * @throws std::runtime_error on error
     */
    void open_connect(const UdpEndpoint& ep)
    {
#if defined(_WIN32)
        detail::ensure_wsa();
#endif
        auto addr_remote = ep.to_sockaddr(false);
        socklen_t len    = sizeof(addr_remote);

        fd_ = ::socket(addr_remote.ss_family, SOCK_DGRAM, 0);
        if (fd_ == invalid_socket)
        {
#if defined(_WIN32)
            int err = WSAGetLastError();
            throw std::runtime_error("UDP socket() failed: WSA error " + std::to_string(err));
#else
            int err = errno;
            throw std::runtime_error("UDP socket() failed: " + std::string(std::strerror(err)));
#endif
        }

        // Optional: bind auf lokalen Port
        if (ep.bind_local && ep.local_port != 0)
        {
            int family = addr_remote.ss_family;
            uint16_t lp = ep.local_port;
            std::string port_str = std::to_string(lp);

            if (family == AF_INET)
            {
                sockaddr_in local4{};
                local4.sin_family      = AF_INET;
                local4.sin_port        = htons(lp);
                local4.sin_addr.s_addr = INADDR_ANY;

                int rc = ::bind(fd_,
                                reinterpret_cast<sockaddr*>(&local4),
                                sizeof(local4));
                if (rc != 0)
                {
#if defined(_WIN32)
                    int err = WSAGetLastError();
                    throw std::runtime_error(
                      "UDP bind IPv4(port=" + port_str +
                      ") failed: WSA error " + std::to_string(err)
                    );
#else
                    int err = errno;
                    throw std::runtime_error(
                      "UDP bind IPv4(port=" + port_str +
                      ") failed: errno " + std::to_string(err) +
                      " (" + std::strerror(err) + ")"
                    );
#endif
                }
            }
            else if (family == AF_INET6)
            {
                sockaddr_in6 local6{};
                local6.sin6_family = AF_INET6;
                local6.sin6_port   = htons(lp);
                local6.sin6_addr   = in6addr_any;

                int v6only = 0;
                ::setsockopt(fd_, IPPROTO_IPV6, IPV6_V6ONLY,
                             reinterpret_cast<char*>(&v6only),
                             sizeof(v6only));

                int rc = ::bind(fd_,
                                reinterpret_cast<sockaddr*>(&local6),
                                sizeof(local6));
                if (rc != 0)
                {
#if defined(_WIN32)
                    int err = WSAGetLastError();
                    throw std::runtime_error(
                      "UDP bind IPv6(port=" + port_str +
                      ") failed: WSA error " + std::to_string(err)
                    );
#else
                    int err = errno;
                    throw std::runtime_error(
                      "UDP bind IPv6(port=" + port_str +
                      ") failed: errno " + std::to_string(err) +
                      " (" + std::strerror(err) + ")"
                    );
#endif
                }
            }
            else
            {
                throw std::runtime_error("UDP bind failed: unsupported address family");
            }
        }

        if (::connect(fd_,
                      reinterpret_cast<sockaddr*>(&addr_remote),
                      len) < 0)
        {
#if defined(_WIN32)
            int err = WSAGetLastError();
            throw std::runtime_error("UDP connect() failed: WSA error " + std::to_string(err));
#else
            int err = errno;
            throw std::runtime_error(
              "UDP connect() failed: errno " + std::to_string(err) +
              " (" + std::strerror(err) + ")"
            );
#endif
        }
    }

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

    /// Enable UDP broadcast
    void enable_broadcast(bool enable)
    {
      set_socket_option(fd_, SocketOption::Broadcast, enable ? 1 : 0);
    }

    /// Join multicast group (IPv4)
    void join_multicast_group(const std::string& group_addr)
    {
      ip_mreq mreq{};
      mreq.imr_multiaddr.s_addr = inet_addr(group_addr.c_str());
      mreq.imr_interface.s_addr = INADDR_ANY;
      if (::setsockopt(fd_, IPPROTO_IP, IP_ADD_MEMBERSHIP, reinterpret_cast<char*>(&mreq), sizeof(mreq)) < 0)
        throw SocketException("join_multicast_group failed", errno, group_addr);
    }

    /// Leave multicast group (IPv4)
    void leave_multicast_group(const std::string& group_addr)
    {
      ip_mreq mreq{};
      mreq.imr_multiaddr.s_addr = inet_addr(group_addr.c_str());
      mreq.imr_interface.s_addr = INADDR_ANY;
      if (::setsockopt(fd_, IPPROTO_IP, IP_DROP_MEMBERSHIP, reinterpret_cast<char*>(&mreq), sizeof(mreq)) < 0)
        throw SocketException("leave_multicast_group failed", errno, group_addr);
    }

    /**
     * @brief Read a datagram.
     * @param data Destination buffer
     * @param size Number of bytes
     * @param from_addr Optional: Zeiger auf sockaddr_storage für Absender
     * @param from_len Optional: Zeiger auf socklen_t für Länge
     * @return Number of bytes read
     * @throws SocketException on error
     */
    std::size_t read(char* data, std::size_t size,
                     sockaddr_storage* from_addr = nullptr, socklen_t* from_len = nullptr)
    {
#if defined(_WIN32)
        sockaddr_storage from{};
        int fromlen = sizeof(from);
        int ret = ::recvfrom(
            fd_, data, static_cast<int>(size), 0,
            reinterpret_cast<sockaddr*>(&from), &fromlen
        );
        if (ret == SOCKET_ERROR)
        {
            int err = WSAGetLastError();
            throw SocketException("UDP recvfrom failed", err);
        }
        if (from_addr) *from_addr = from;
        if (from_len)  *from_len  = fromlen;
        return static_cast<std::size_t>(ret);
#else
        if (fd_ == invalid_socket) {
            std::cerr << "[UdpTransport] read() called with invalid fd_! (socket already closed or never opened)" << std::endl;
            throw SocketException("UDP recvfrom failed: socket not open", EBADF);
        }
        sockaddr_storage from{};
        socklen_t    fromlen = sizeof(from);
        ssize_t ret = ::recvfrom(
            fd_, data, size, 0,
            reinterpret_cast<sockaddr*>(&from), &fromlen
        );
        if (ret < 0)
        {
            int err = errno;
            std::cerr << "[UdpTransport] recvfrom failed: errno=" << err << " (" << std::strerror(err) << "), fd_=" << fd_ << std::endl;
            throw SocketException("UDP recvfrom failed", err);
        }
        if (from_addr) *from_addr = from;
        if (from_len)  *from_len  = fromlen;
        return static_cast<std::size_t>(ret);
#endif
    }

    /**
     * @brief Datagramm an einen bestimmten Empfänger senden (für Server).
     * @param data Quellpuffer
     * @param size Anzahl Bytes
     * @param to_addr Zieladresse
     * @param to_len Länge der Zieladresse
     */
    void write_to(const char* data, std::size_t size,
                  const sockaddr_storage& to_addr, socklen_t to_len) noexcept
    {
#if defined(_WIN32)
        ::sendto(fd_, data, static_cast<int>(size), 0,
                  reinterpret_cast<const sockaddr*>(&to_addr), to_len);
#else
        ::sendto(fd_, data, size, 0,
                 reinterpret_cast<const sockaddr*>(&to_addr), to_len);
#endif
    }

    /**
     * @brief Datagramm senden (nur sinnvoll im Client-Modus mit connect()).
     * @param data Quellpuffer
     * @param size Anzahl Bytes
     */
    void write(const char* data, std::size_t size) noexcept
    {
#if defined(_WIN32)
        ::send(fd_, data, static_cast<int>(size), 0);
#else
        ::send(fd_, data, size, 0);
#endif
    }
    /**
     * @brief Close the UDP socket (idempotent).
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
     */
    bool is_open() const noexcept
    {
      return fd_ != invalid_socket;
    }

    /**
     * @brief Returns the native socket handle.
     */
    sock_t native_handle() const noexcept
    {
      return fd_;
    }

    // Optional: Access to local/remote address (only if socket is open)
    // sockaddr_storage local_address() const;
    // sockaddr_storage remote_address() const;

private:
    sock_t fd_{ invalid_socket };
};

}
