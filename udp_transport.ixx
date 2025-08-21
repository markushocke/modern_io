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
  #pragma comment(lib, "ws2_32.lib") // Link with Winsock2 library automatically.
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
import net_io_concepts; // Import network transport concepts for constraints.
import net_io.udp_endpoint;
export import net_io_base; // Export sock_t and invalid_socket

export namespace net_io
{

/**
 * @brief UDP transport for datagram communication.
 *
 * This class provides a cross-platform abstraction for UDP sockets.
 * - For server use: call open_bind() to bind to a local address/port. The socket will receive datagrams from any peer and can send to specific peers using write_to().
 * - For client use: call open_connect() to bind (optionally) and connect to a remote address/port. The socket can then send/receive datagrams to/from the connected peer using write() and read().
 * - The write() method uses the connected peer (only meaningful in client mode).
 *
 * Example usage:
 * @code
 * net_io::UdpEndpoint ep("127.0.0.1", 9000);
 * net_io::UdpTransport udp;
 * udp.open_connect(ep);
 * udp.write("hello", 5);
 * char buf[128];
 * std::size_t n = udp.read(buf, sizeof(buf));
 * udp.close();
 * @endcode
 */
class UdpTransport
{
public:
    /// Default constructor (no socket open)
    UdpTransport() = default;

    /**
     * @brief Construct from an existing socket handle.
     * @param fd The socket handle (must be a valid UDP socket).
     */
    explicit UdpTransport(sock_t fd) noexcept
      : fd_(fd)
    {}

    // Not copyable: copying a UDP transport is not allowed.
    UdpTransport(const UdpTransport&) = delete;
    UdpTransport& operator=(const UdpTransport&) = delete;

    /**
     * @brief Move constructor.
     *
     * Transfers ownership of the socket from another UdpTransport.
     * The moved-from object is left in a valid but unspecified state.
     */
    UdpTransport(UdpTransport&& other) noexcept
      : fd_(other.fd_)
    {
      other.fd_ = invalid_socket;
    }

    /**
     * @brief Move assignment operator.
     *
     * Transfers ownership of the socket from another UdpTransport.
     * The moved-from object is left in a valid but unspecified state.
     */
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

    /**
     * @brief Destructor. Closes the socket if it is open.
     */
    ~UdpTransport()
    {
      close();
    }

    /**
     * @brief Open a UDP socket and bind to a local address/port (server mode).
     * @param ep Target endpoint (address, port, bind_local, local_port).
     * @throws std::runtime_error on error.
     *
     * This method creates a UDP socket and binds it to the specified local address and port.
     * It does not connect the socket to a remote peer.
     */
    void open_bind(const UdpEndpoint& ep)
    {
#if defined(_WIN32)
        detail::ensure_wsa();
#endif
        isBoundLocal = ep.bind_local;
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
     * @brief Open a UDP socket, optionally bind to a local port, and connect to a remote address/port (client mode).
     * @param ep Target endpoint (address, port, bind_local, local_port).
     * @throws std::runtime_error on error.
     *
     * This method creates a UDP socket, optionally binds it to a local port, and connects it to a remote peer.
     * After connecting, write() and read() will use the connected peer.
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

    /**
     * @brief Set the socket to nonblocking mode.
     * @param enable True to enable nonblocking mode, false to disable.
     *
     * Example:
     * @code
     * udp.set_nonblocking(true);
     * @endcode
     */
    void set_nonblocking(bool enable)
    {
      set_socket_option(fd_, SocketOption::NonBlocking, enable ? 1 : 0);
    }

    /**
     * @brief Set the read timeout in milliseconds.
     * @param ms Timeout in milliseconds.
     */
    void set_read_timeout(int ms)
    {
      set_socket_option(fd_, SocketOption::ReadTimeoutMs, ms);
    }

    /**
     * @brief Set the write timeout in milliseconds.
     * @param ms Timeout in milliseconds.
     */
    void set_write_timeout(int ms)
    {
      set_socket_option(fd_, SocketOption::WriteTimeoutMs, ms);
    }

    /**
     * @brief Set an arbitrary socket option.
     * @param opt The socket option to set.
     * @param value The value to set for the option.
     */
    void set_option(SocketOption opt, int value)
    {
      set_socket_option(fd_, opt, value);
    }

    /**
     * @brief Enable or disable UDP broadcast.
     * @param enable True to enable broadcast, false to disable.
     */
    void enable_broadcast(bool enable)
    {
      set_socket_option(fd_, SocketOption::Broadcast, enable ? 1 : 0);
    }

    /**
     * @brief Join an IPv4 multicast group.
     * @param group_addr The multicast group address (e.g., "239.255.0.1").
     * @throws SocketException on error.
     */
    void join_multicast_group(const std::string& group_addr)
    {
      ip_mreq mreq{};
      mreq.imr_multiaddr.s_addr = inet_addr(group_addr.c_str());
      mreq.imr_interface.s_addr = INADDR_ANY;
      if (::setsockopt(fd_, IPPROTO_IP, IP_ADD_MEMBERSHIP, reinterpret_cast<char*>(&mreq), sizeof(mreq)) < 0)
        throw SocketException("join_multicast_group failed", errno, group_addr);
    }

    /**
     * @brief Leave an IPv4 multicast group.
     * @param group_addr The multicast group address.
     * @throws SocketException on error.
     */
    void leave_multicast_group(const std::string& group_addr)
    {
      ip_mreq mreq{};
      mreq.imr_multiaddr.s_addr = inet_addr(group_addr.c_str());
      mreq.imr_interface.s_addr = INADDR_ANY;
      if (::setsockopt(fd_, IPPROTO_IP, IP_DROP_MEMBERSHIP, reinterpret_cast<char*>(&mreq), sizeof(mreq)) < 0)
        throw SocketException("leave_multicast_group failed", errno, group_addr);
    }

    /**
     * @brief Read a datagram from the socket.
     * @param data Pointer to the destination buffer.
     * @param size Number of bytes to read.
     * @param from_addr Optional: pointer to sockaddr_storage to receive the sender address.
     * @param from_len Optional: pointer to socklen_t to receive the address length.
     * @return Number of bytes read.
     * @throws SocketException on error.
     *
     * If from_addr and from_len are provided, they will be filled with the sender's address.
     * If not provided, the sender address is ignored.
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
     * @brief Send a datagram to a specific recipient (for server use).
     * @param data Pointer to the source buffer.
     * @param size Number of bytes to send.
     * @param to_addr Destination address.
     * @param to_len Length of the destination address structure.
     *
     * This method is typically used by servers to send datagrams to different clients.
     */
    void write_to(const char* data, std::size_t size,
                  const sockaddr_storage& to_addr, socklen_t to_len) noexcept
    {
        if(isBoundLocal)
        {
#if defined(_WIN32)
        ::sendto(fd_, data, static_cast<int>(size), 0,
                  reinterpret_cast<const sockaddr*>(&to_addr), to_len);
#else
        ::sendto(fd_, data, size, 0,
                 reinterpret_cast<const sockaddr*>(&to_addr), to_len);
#endif
        }
        else
        {
            write(data, size);
        }
    }

    /**
     * @brief Send a datagram to the connected peer (client mode only).
     * @param data Pointer to the source buffer.
     * @param size Number of bytes to send.
     *
     * This method is only meaningful if the socket is connected to a peer (client mode).
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
     *
     * Closes the socket if it is open. This operation is idempotent:
     * calling close() multiple times is safe and has no effect after the first call.
     * The socket handle is set to invalid_socket after closing.
     *
     * Example:
     * @code
     * udp.close();
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
     * @return true if the socket is open and valid, false otherwise.
     *
     * Example:
     * @code
     * if (udp.is_open()) { ... }
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

    // Optional: Access to local/remote address (only if socket is open)
    // sockaddr_storage local_address() const;
    // sockaddr_storage remote_address() const;

private:
    sock_t fd_{ invalid_socket }; ///< The socket handle.
    bool isBoundLocal{ false };    ///< Whether the socket is bound to a local address.
};

} // namespace net_io
