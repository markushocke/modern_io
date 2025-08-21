module;

#include <optional>
#include <mutex>
#include <functional>

// Platform-specific includes for sockaddr_storage and socklen_t
#if defined(_WIN32)
  #define NOMINMAX
  #include <winsock2.h>
  #include <ws2tcpip.h>
#else
  #include <sys/socket.h>
  #include <netinet/in.h>
#endif

#ifndef _MSC_VER
#include <cstddef>
#include <concepts>
#include <string>
#include <vector>
#include <stdexcept>
#include <fstream>
#include <cstdint>
#include <cstring>
#include <span>
#include <type_traits>
#include <utility>
#include <limits>
#include <bit>
#include <iostream>
#include <memory>
#include <thread>
#endif


export module net_io_adapters;


#ifdef _MSC_VER
import <cstddef>;
import <concepts>;
import <string>;
import <vector>;
import <stdexcept>;
import <fstream>;
import <cstdint>;
import <cstring>;
import <span>;
import <type_traits>;
import <utility>;
import <limits>;
import <bit>;
import <iostream>;
#endif

import modern_io;   // Provides modern_io::OutputStream and modern_io::InputStream interfaces.
import net_io;      // Provides net_io::Transportable, net_io::TcpClient, and other network primitives.

// The following imports are required for MSVC due to incomplete umbrella import support.
import net_io.tcp_endpoint;
import net_io.tcp_client;
import net_io.tcp_server;
import net_io.udp_endpoint;
import net_io.udp_transport;
import net_io_concepts; // Imports network transport concepts for constraints.

export namespace net_io_adapters
{

    /**
     * @brief Adapter that turns any Writable transport into a modern_io::OutputStream.
     *
     * This class allows you to use any transport that satisfies the Writable concept
     * as an OutputStream in the modern_io framework.
     *
     * Example:
     * @code
     * net_io::TcpClient client(endpoint);
     * client.open();
     * TransportSink sink(client);
     * sink.write("hello", 5);
     * @endcode
     */
    template<net_io_concepts::Writable T>
    class TransportSink
    {
    public:
        /**
         * @brief Konstruktor: shared_ptr statt Referenz!
         */
        explicit TransportSink(std::shared_ptr<T> t) noexcept
            : t_(std::move(t))
        {
        }

        /**
         * @brief Writes data to the underlying transport.
         * @param data Pointer to the data to write.
         * @param size Number of bytes to write.
         */
        void write(const char* data, std::size_t size)
        {
            t_->write(data, size);
        }

        /**
         * @brief Writes a span of bytes to the underlying transport.
         * @param data Span of bytes to write.
         */
        void write(std::span<const std::byte> data)
        {
            write(reinterpret_cast<const char*>(data.data()), data.size());
        }

        /**
         * @brief Writes a span of chars to the underlying transport.
         * @param data Span of chars to write.
         */
        void write(std::span<const char> data)
        {
            write(data.data(), data.size());
        }

        /**
         * @brief Flushes the output stream.
         *
         * For socket-based transports, this is typically a no-op because there is no userland buffering.
         */
        void flush() noexcept
        {
            // No userland buffering for sockets.
        }

        /**
         * @brief Optionally forwards write_to if the underlying transport supports it.
         *
         * This allows sending data to a specific address, for example in UDP transports.
         * The method is only available if T provides a compatible write_to method.
         *
         * Example:
         * @code
         * sockaddr_storage addr = ...;
         * sink.write_to("data", 4, addr, sizeof(addr));
         * @endcode
         */
        template<typename U = T, typename... Args>
        auto write_to(Args&&... args)
            -> decltype(std::declval<U&>().write_to(std::forward<Args>(args)...))
        {
            return t_->write_to(std::forward<Args>(args)...);
        }

        // Zugriff auf das shared_ptr (z.B. für native_handle)
        std::shared_ptr<T>& underlying() noexcept { return t_; }
        const std::shared_ptr<T>& underlying() const noexcept { return t_; }

    private:
        std::shared_ptr<T> t_; ///< shared_ptr statt Referenz!
    };

    /**
     * @brief Adapter that turns any Readable transport into a modern_io::InputStream.
     *
     * This class allows you to use any transport that satisfies the Readable concept
     * as an InputStream in the modern_io framework.
     *
     * Example:
     * @code
     * net_io::TcpClient client(endpoint);
     * client.open();
     * TransportSource source(client);
     * char buf[128];
     * size_t n = source.read(buf, sizeof(buf));
     * @endcode
     */
    template<net_io_concepts::Readable T>
    class TransportSource
    {
    public:
        /**
         * @brief Konstruktor: shared_ptr statt Referenz!
         */
        explicit TransportSource(std::shared_ptr<T> t) noexcept
            : t_(std::move(t))
        {
        }

        /**
         * @brief Reads data from the underlying transport.
         * @param data Pointer to the buffer to fill.
         * @param size Maximum number of bytes to read.
         * @return Number of bytes actually read.
         */
        std::size_t read(char* data, std::size_t size)
        {
            return t_->read(data, size);
        }

        /**
         * @brief Reads data into a span of bytes.
         * @param data Span of bytes to fill.
         * @return Number of bytes actually read.
         */
        std::size_t read(std::span<std::byte> data)
        {
            return read(reinterpret_cast<char*>(data.data()), data.size());
        }

        /**
         * @brief Reads data into a span of chars.
         * @param data Span of chars to fill.
         * @return Number of bytes actually read.
         */
        std::size_t read(std::span<char> data)
        {
            return read(data.data(), data.size());
        }

        /**
         * @brief Checks for end-of-file (EOF) condition.
         * @return Always returns false for sockets, as EOF is not typically meaningful.
         */
        bool eof() const noexcept
        {
            return false;
        }

        /**
         * @brief Optionally reads data and obtains the sender address, if supported.
         *
         * This method is only enabled if the underlying transport provides a compatible
         * read method that accepts sender address parameters (e.g., UDP).
         *
         * @param data Buffer to fill.
         * @param size Maximum number of bytes to read.
         * @param from Pointer to sockaddr_storage to receive sender address.
         * @param fromlen Pointer to socklen_t to receive address length.
         * @return Number of bytes actually read, or 0 if not supported.
         */
        std::size_t read(char* data, std::size_t size, sockaddr_storage* from, socklen_t* fromlen)
        {
            if constexpr (requires { t_->read(data, size, from, fromlen); }) 
            {
                return t_->read(data, size, from, fromlen);
            } 
            else 
            {
                // Fallback: not supported.
                return 0;
            }
        }

        std::shared_ptr<T>& underlying() noexcept { return t_; }
        const std::shared_ptr<T>& underlying() const noexcept { return t_; }

    private:
        std::shared_ptr<T> t_; ///< shared_ptr statt Referenz!
    };

    // Adapter for UDP datagram output, buffers until flush
    template<net_io_concepts::Writable T> // <--- angepasst
    class DatagramSink
    {
    public:
        /**
         * @brief Constructs a DatagramSink that adapts a writable transport for datagram (UDP) output.
         *
         * This adapter buffers outgoing data until flush() is called, at which point the entire buffer
         * is sent as a single datagram. This is useful for protocols where each message must be sent
         * as a discrete packet (e.g., UDP).
         *
         * @param transport Reference to the underlying writable transport (e.g., UdpTransport).
         *
         * Example usage:
         * @code
         * net_io::UdpTransport udp;
         * udp.open_connect(endpoint);
         * DatagramSink sink(udp);
         * sink.write("hello", 5);
         * sink.flush(); // sends the datagram
         * @endcode
         */
        explicit DatagramSink(const std::shared_ptr<T>& transport) noexcept
            : transport_(transport)
        {
        }

        /**
         * @brief Writes data to the internal buffer.
         *
         * The data is not sent immediately. It is accumulated in the buffer until flush() is called.
         *
         * @param data Pointer to the data to write.
         * @param size Number of bytes to write.
         */
        void write(const char* data, std::size_t size)
        {
            buffer_.insert(buffer_.end(), data, data + size);
        }

        /**
         * @brief Writes a span of bytes to the internal buffer.
         *
         * @param data Span of bytes to write.
         */
        void write(std::span<const std::byte> data)
        {
            write(reinterpret_cast<const char*>(data.data()), data.size());
        }

        /**
         * @brief Writes a span of chars to the internal buffer.
         *
         * @param data Span of chars to write.
         */
        void write(std::span<const char> data)
        {
            write(data.data(), data.size());
        }

        /**
         * @brief Sends the contents of the buffer as a single datagram.
         *
         * If the buffer is empty, nothing is sent. After sending, the buffer is cleared.
         *
         * Example:
         * @code
         * sink.write("abc", 3);
         * sink.write("def", 3);
         * sink.flush(); // sends "abcdef" as one datagram
         * @endcode
         */
        void flush() noexcept
        {
            if (!buffer_.empty())
            {
                transport_->write(buffer_.data(), buffer_.size());
                buffer_.clear();
            }
        }

        /**
         * @brief Sends a datagram directly to a specified address.
         *
         * This method bypasses the internal buffer and immediately sends the provided data
         * to the given destination address. It is typically used in scenarios where the
         * destination may change per datagram (e.g., server responding to multiple clients).
         *
         * @param data Pointer to the data to send.
         * @param size Number of bytes to send.
         * @param to_addr Destination address (sockaddr_storage).
         * @param to_len Length of the destination address structure.
         *
         * Example:
         * @code
         * sockaddr_storage addr = ...;
         * sink.write_to("pong", 4, addr, sizeof(addr));
         * @endcode
         */
        void write_to(const char* data, std::size_t size, const sockaddr_storage& to_addr, socklen_t to_len)
        {
            transport_->write_to(data, size, to_addr, to_len);
        }

    private:
        std::shared_ptr<T> transport_;              // Reference to the underlying transport object
        std::vector<char> buffer_;  // Buffer for accumulating outgoing datagram data
    };

    // Factory function for DatagramSink
    template<net_io_concepts::Writable T> // <--- angepasst
    DatagramSink<T> make_datagram_sink(std::shared_ptr<T> t)
    {
        return DatagramSink<T>(*t);
    }

    // Adapter for UDP datagram input, reads one datagram at a time
    template<net_io_concepts::Readable T> // <--- angepasst
    class DatagramSource
    {
    public:
        explicit DatagramSource(const std::shared_ptr<T>& transport) noexcept
            : transport_(transport), pos_(0)
        {
        }
        std::size_t read(char* data, std::size_t size)
        {
            if (pos_ >= buffer_.size())
            {
                // Fetch next datagram
                buffer_.resize(max_packet_);
                auto got = transport_->read(buffer_.data(), buffer_.size());
                if (got == 0)
                {
                    return 0; // No datagram – EOF
                }
                buffer_.resize(got);
                pos_ = 0;
            }
            // Deliver data from buffer
            std::size_t chunk = std::min(size, buffer_.size() - pos_);
            std::memcpy(data, buffer_.data() + pos_, chunk);
            pos_ += chunk;
            return chunk;
        }
        std::size_t read(std::span<std::byte> data)
        {
            return read(reinterpret_cast<char*>(data.data()), data.size());
        }
        std::size_t read(std::span<char> data)
        {
            return read(data.data(), data.size());
        }

        // Addition: read with sender address (for DuplexDatagramStream)
        std::size_t read(char* data, std::size_t size, sockaddr_storage* from, socklen_t* fromlen)
        {
            // Clear the buffer and read a datagram directly from the transport
            return transport_->read(data, size, from, fromlen);
        }

        bool eof() const noexcept
        {
            return false;
        }

    private:
        static constexpr std::size_t max_packet_ = 65507; // max UDP payload
        std::shared_ptr<T> transport_;
        std::vector<char> buffer_;
        std::size_t pos_;
    };

    // Factory function for DatagramSource
    template<net_io_concepts::Readable T>
    DatagramSource<T> make_datagram_source(std::shared_ptr<T> t)
    {
        return DatagramSource<T>(*t);
    }

    // DuplexDatagramStream: Bidirectional datagram stream with address management
    template<typename Source, typename Sink>
    class DuplexDatagramStream
    {
    public:
        using endpoint_type = sockaddr_storage;

        explicit DuplexDatagramStream(Source src, Sink sink)
            : src_(std::move(src)), sink_(std::move(sink))
        {
        }
        DuplexDatagramStream(const DuplexDatagramStream& other)
            : src_(other.src_)
            , sink_(other.sink_)
            , last_peer_(other.last_peer_)
            // mtx_ default-construction!
        {}
        // Read: remembers the sender address
        std::size_t read(char* data, std::size_t size)
        {
            std::lock_guard<std::mutex> lock(mtx_);
            endpoint_type from{};
            socklen_t fromlen = sizeof(from);
            // Source must support read(char*, std::size_t, sockaddr_storage*, socklen_t*)
            std::size_t n = src_.read(data, size, &from, &fromlen);
            if (n > 0)
                last_peer_ = std::make_pair(from, fromlen);
            return n;
        }
        std::size_t read(std::span<std::byte> data)
        {
            return read(reinterpret_cast<char*>(data.data()), data.size());
        }
        std::size_t read(std::span<char> data)
        {
            return read(data.data(), data.size());
        }

        // Write: sends to the last stored address
        void write(const char* data, std::size_t size)
        {
            std::lock_guard<std::mutex> lock(mtx_);
            // if (!last_peer_)
            //     throw std::runtime_error("No peer address known for write");
            sink_.write_to(data, size, last_peer_->first, last_peer_->second);
        }
        void write(std::span<const std::byte> data)
        {
            write(reinterpret_cast<const char*>(data.data()), data.size());
        }
        void write(std::span<const char> data)
        {
            write(data.data(), data.size());
        }

        void flush()
        {
            std::lock_guard<std::mutex> lock(mtx_);
            if constexpr (requires { sink_.flush(); }) 
            {
                sink_.flush();
            }
        }

        // Explicitly set the target address for outgoing datagrams
        void set_peer(const endpoint_type& addr, socklen_t addrlen)
        {
            std::lock_guard<std::mutex> lock(mtx_);
            last_peer_ = std::make_pair(addr, addrlen);
        }

        // Generic variant: set_peer with Endpoint (e.g. UdpEndpoint, TcpEndpoint)
        template<typename Endpoint>
        void set_peer(const Endpoint& ep)
        {
            endpoint_type addr = ep.to_sockaddr(false);
            socklen_t addrlen = sizeof(addr);
            set_peer(addr, addrlen);
        }

        // Returns the currently stored peer address (optional)
        std::optional<std::pair<endpoint_type, socklen_t>> get_peer() const
        {
            std::lock_guard<std::mutex> lock(mtx_);
            return last_peer_;
        }

        // EOF status (delegates to Source)
        bool eof() const noexcept
        {
            return src_.eof();
        }

    private:
        mutable std::mutex mtx_;
        Source src_;
        Sink sink_;
        std::optional<std::pair<endpoint_type, socklen_t>> last_peer_;
    };

    // Generic duplex stream for TCP (read/write/flush/eof)
    template<typename Source, typename Sink>
    class TcpDuplexStream
    {
    public:
        TcpDuplexStream(Source src, Sink sink)
            : src_(std::move(src)), sink_(std::move(sink))
        {
        }

        // OutputStream methods
        void write(const char* data, std::size_t size)
        {
            sink_.write(data, size);
        }
        void write(std::span<const std::byte> data)
        {
            sink_.write(data);
        }
        void write(std::span<const char> data)
        {
            sink_.write(data);
        }
        void flush()
        {
            sink_.flush();
        }

        // InputStream methods
        std::size_t read(char* data, std::size_t size)
        {
            return src_.read(data, size);
        }
        std::size_t read(std::span<std::byte> data)
        {
            return src_.read(data);
        }
        std::size_t read(std::span<char> data)
        {
            return src_.read(data);
        }
        bool eof() const noexcept
        {
            return src_.eof();
        }

        // Access to the native socket handle (if available)
        auto native_handle() const
        {
            // Assumption: Source is TransportSource<TcpClient>
            return src_.underlying().native_handle();
        }

    private:
        Source src_;
        Sink sink_;
    };

    // Helper function to create a DuplexDatagramStream
    template<typename Source, typename Sink>
    DuplexDatagramStream<Source, Sink> make_duplex_datagram_stream(Source src, Sink sink)
    {
        return DuplexDatagramStream<Source, Sink>(std::move(src), std::move(sink));
    }

    // Factory function for OutputStream adapter
    template<net_io_concepts::Writable T>
    TransportSink<T> make_sink(T& t)
    {
        return TransportSink<T>(t);
    }

    // Factory function for InputStream adapter
    template<net_io_concepts::Readable T>
    TransportSource<T> make_source(T& t)
    {
        return TransportSource<T>(t);
    }

    // ------------------------------------------------------------------------
    // SharedStream<T>: Wrapper for std::shared_ptr<T> with method forwarding
    // ------------------------------------------------------------------------
    /**
     * @brief Wrapper for std::shared_ptr<T> that forwards all methods of T.
     *        Satisfies OutputStream/InputStream if T does.
     */
    template<typename T>
    class SharedStream
    {
    public:
        using element_type = T;

        explicit SharedStream(std::shared_ptr<T> ptr)
            : ptr_(std::move(ptr)) 
        {
        }

        // OutputStream methods
        void write(const char* data, std::size_t size)
        {
            ptr_->write(data, size);
        }
        void write(std::span<const std::byte> data)
        {
            ptr_->write(data);
        }
        void write(std::span<const char> data)
        {
            ptr_->write(data);
        }
        void flush()
        {
            ptr_->flush();
        }

        // InputStream methods
        std::size_t read(char* data, std::size_t size)
        {
            return ptr_->read(data, size);
        }
        std::size_t read(std::span<std::byte> data)
        {
            return ptr_->read(data);
        }
        std::size_t read(std::span<char> data)
        {
            return ptr_->read(data);
        }
        bool eof() const noexcept
        {
            return ptr_->eof();
        }

        // Optional methods (e.g. set_peer, get_peer, etc.) only if available
        template<typename Endpoint, typename... Args>
        auto set_peer(Endpoint&& endpoint, Args&&... args)
            requires requires(T& t, Endpoint&& e, Args&&... a) { t.set_peer(std::forward<Endpoint>(e), std::forward<Args>(a)...); }
        {
            return ptr_->set_peer(std::forward<Endpoint>(endpoint), std::forward<Args>(args)...);
        }

        template<typename... Args>
        auto get_peer(Args&&... args) const
            requires requires(const T& t, Args&&... a) { t.get_peer(std::forward<Args>(a)...); }
        {
            return ptr_->get_peer(std::forward<Args>(args)...);
        }

        // Access to the shared object
        std::shared_ptr<T>& get() noexcept 
        { 
            return ptr_; 
        }
        const std::shared_ptr<T>& get() const noexcept 
        { 
            return ptr_; 
        }

    private:
        std::shared_ptr<T> ptr_;
    };

    // --- Concepts for Endpoints ---
    /**
     * @brief Concept for types that behave like a UDP endpoint.
     *
     * A type satisfies UdpEndpointLike if it provides the following members:
     *   - address: convertible to std::string (the IP address or hostname)
     *   - port: convertible to uint16_t (the UDP port)
     *   - bind_local: convertible to bool (whether to bind to a local address)
     *   - local_port: convertible to uint16_t (the local port to bind)
     *
     * This concept is useful for generic code that needs to accept any type
     * representing a UDP endpoint, not just a specific class.
     *
     * Example:
     * @code
     * struct MyUdpEndpoint {
     *     std::string address;
     *     uint16_t port;
     *     bool bind_local;
     *     uint16_t local_port;
     * };
     * static_assert(UdpEndpointLike<MyUdpEndpoint>);
     * @endcode
     */
    template<typename T>
    concept UdpEndpointLike = requires(T ep)
    {
        { ep.address }    -> std::convertible_to<std::string>;
        { ep.port }       -> std::convertible_to<uint16_t>;
        { ep.bind_local } -> std::convertible_to<bool>;
        { ep.local_port } -> std::convertible_to<uint16_t>;
    };

    /**
     * @brief Concept for types that behave like a TCP endpoint.
     *
     * A type satisfies TcpEndpointLike if it provides the following members:
     *   - address: convertible to std::string (the IP address or hostname)
     *   - port: convertible to uint16_t (the TCP port)
     *
     * This concept explicitly excludes types that satisfy UdpEndpointLike,
     * to avoid ambiguity when a type could represent both protocols.
     *
     * Example:
     * @code
     * struct MyTcpEndpoint {
     *     std::string address;
     *     uint16_t port;
     * };
     * static_assert(TcpEndpointLike<MyTcpEndpoint>);
     * @endcode
     */
    template<typename T>
    concept TcpEndpointLike =
        requires(T ep)
        {
            { ep.address } -> std::convertible_to<std::string>;
            { ep.port }    -> std::convertible_to<uint16_t>;
        }
        && (!UdpEndpointLike<T>);

    // --- Generische Factory für beliebige Transporttypen (Client) ---
    /**
     * @brief Erzeugt einen SharedStream für beliebige Transporttypen.
     *        Die Factory erkennt TCP/UDP/sonstige Transports automatisch.
     *        Die Rückgabe ist immer ein SharedStream, der InputStream/OutputStream erfüllt.
     *
     * Beispiel:
     *   auto stream = make_stream(TcpEndpoint{...});
     *   auto stream = make_stream(UdpEndpoint{...});
     *   auto stream = make_stream(std::make_shared<MyTransport>(...));
     */
    template<typename EndpointOrTransport>
    auto make_stream(EndpointOrTransport&& ep_or_transport)
    {
        using T = std::decay_t<EndpointOrTransport>;
        if constexpr (TcpEndpointLike<T>) {
            // TCP Endpoint: Erzeuge TcpClient, öffne Verbindung, adaptiere
            auto client = std::make_shared<net_io::TcpClient>(std::forward<EndpointOrTransport>(ep_or_transport));
            client->open();
            auto src  = TransportSource<net_io::TcpClient>(client);
            auto sink = TransportSink<net_io::TcpClient>(client);
            using DuplexType = TcpDuplexStream<decltype(src), decltype(sink)>;
            auto duplex = std::make_shared<DuplexType>(std::move(src), std::move(sink));
            return SharedStream<DuplexType>(duplex);
        } else if constexpr (UdpEndpointLike<T>) {
            // UDP Endpoint: Erzeuge UdpTransport, öffne Verbindung, adaptiere
            auto udp = std::make_shared<net_io::UdpTransport>();
            if (ep_or_transport.bind_local)
            {
                // Server-Modus: bind auf lokalen Port
                udp->open_bind(std::forward<EndpointOrTransport>(ep_or_transport));
            }
            else
            {
                // Client-Modus: connect zu Ziel
                udp->open_connect(std::forward<EndpointOrTransport>(ep_or_transport));
            }
            auto src  = DatagramSource<net_io::UdpTransport>(udp);
            auto sink = DatagramSink<net_io::UdpTransport>(udp);
            using DuplexType = DuplexDatagramStream<decltype(src), decltype(sink)>;
            auto duplex = std::make_shared<DuplexType>(std::move(src), std::move(sink));
            return SharedStream<DuplexType>(duplex);
        } else if constexpr (std::is_pointer_v<T> ||
                             std::is_same_v<T, std::shared_ptr<std::decay_t<decltype(*ep_or_transport)>>>)
        {
            // Für shared_ptr<T> oder T* (z.B. eigene Transportklassen)
            using U = std::remove_pointer_t<T>;
            std::shared_ptr<U> ptr = std::is_pointer_v<T> ? std::shared_ptr<U>(ep_or_transport) : ep_or_transport;
            auto src  = TransportSource<U>(ptr);
            auto sink = TransportSink<U>(ptr);
            using DuplexType = TcpDuplexStream<decltype(src), decltype(sink)>;
            auto duplex = std::make_shared<DuplexType>(std::move(src), std::move(sink));
            return SharedStream<DuplexType>(duplex);
        } else {
            // Für beliebige Transport-Objekte (by value/ref)
            auto obj = std::make_shared<T>(std::forward<EndpointOrTransport>(ep_or_transport));
            auto src  = TransportSource<T>(obj);
            auto sink = TransportSink<T>(obj);
            using DuplexType = TcpDuplexStream<decltype(src), decltype(sink)>;
            auto duplex = std::make_shared<DuplexType>(std::move(src), std::move(sink));
            return SharedStream<DuplexType>(duplex);
        }
    }

    // --- Convenience StreamBuilder for TCP ---
    /**
     * @brief Convenience StreamBuilder for TCP: creates a SharedStream with keepalive.
     * @param client shared_ptr to TcpClient
     * @param server shared_ptr to TcpServer
     * @return SharedStream with keepalive for both server and client
     */
    inline auto tcp_stream_builder(
        std::shared_ptr<net_io::TcpClient> client,
        std::shared_ptr<net_io::TcpServer> server)
    {
        auto src  = TransportSource<net_io::TcpClient>(client);
        auto sink = TransportSink<net_io::TcpClient>(client);
        using DuplexType = TcpDuplexStream<decltype(src), decltype(sink)>;
        struct DuplexWithKeepalive : DuplexType {
            std::shared_ptr<net_io::TcpServer> keepalive_server_;
            std::shared_ptr<net_io::TcpClient> keepalive_client_;
            DuplexWithKeepalive(DuplexType&& base,
                               std::shared_ptr<net_io::TcpServer> s,
                               std::shared_ptr<net_io::TcpClient> c)
                : DuplexType(std::move(base)), keepalive_server_(std::move(s)), keepalive_client_(std::move(c)) {}
        };
        auto duplex = std::make_shared<DuplexWithKeepalive>(
            DuplexType(std::move(src), std::move(sink)), server, client
        );
        return SharedStream<DuplexWithKeepalive>(duplex);
    }

    // --- Executor concept und Beispiel-Executor ---
    /**
     * @brief Concept for Executor: Must support execute(std::function<void()>).
     */
    template<typename E>
    concept Executor = requires(E e, std::function<void()> f) {
        { e.execute(std::move(f)) };
    };

    /**
     * @brief Simple executor that starts a new thread for each task.
     */
    class ThreadExecutor {
    public:
        void execute(std::function<void()> f) {
            std::thread(std::move(f)).detach();
        }
    };

    /**
     * @brief Generic server factory that submits a task to the executor for each new connection.
     *        Die Callback- und StreamBuilder-Parameter stehen jetzt vorne, damit Template-Deduktion funktioniert.
     */
    template<
        typename Callback,
        typename StreamBuilder,
        net_io_concepts::Acceptable ServerType,
        net_io_concepts::Transportable ClientType,
        Executor Exec,
        typename... ServerArgs
    >
    void run_server_with_executor(
        Exec& executor,
        StreamBuilder make_stream,
        Callback&& on_client,
        std::atomic<bool>& running,
        ServerArgs&&... server_args
    )
    {
        auto server = std::make_shared<ServerType>(std::forward<ServerArgs>(server_args)...);
        server->start();

        executor.execute([server, &executor, make_stream, on_client = std::forward<Callback>(on_client), &running]() mutable {
            while (running) {
                try {
                    auto accepted = server->accept();
                    auto client = std::make_shared<ClientType>(std::move(accepted));
                    auto stream = make_stream(client, server);
                    on_client(stream);
                } catch (const std::exception& ex) {
                    std::cerr << "[net_io_adapters] Error accepting connection: " << ex.what() << std::endl;
                }
            }
        });
    }

    /**
     * @brief Convenience function to run a TCP server using run_server_with_executor.
     *        Behält die Flexibilität bzgl. Executor, Callback und StreamBuilder.
     * @tparam Callback        Typ des Client-Callbacks
     * @tparam Executor        Typ des Executors
     * @tparam StreamBuilder   Typ des Stream-Builders
     * @param executor         Executor-Instanz
     * @param on_client        Callback für neue Clients
     * @param running          Atomic-Flag zum Stoppen des Servers
     * @param server_args      Argumente für den TcpServer-Konstruktor
     */
    template<
        typename Callback,
        typename Executor,
        typename StreamBuilder = decltype(tcp_stream_builder),
        typename... ServerArgs
    >
    void run_tcp_server(
        Executor& executor,
        Callback&& on_client,
        std::atomic<bool>& running,
        ServerArgs&&... server_args
    )
    {
        run_server_with_executor<
            Callback,
            StreamBuilder,
            net_io::TcpServer,
            net_io::TcpClient,
            Executor,
            ServerArgs...
        >(
            executor,
            tcp_stream_builder,
            std::forward<Callback>(on_client),
            running,
            std::forward<ServerArgs>(server_args)...
        );
    }
} // namespace net_io_adapters

