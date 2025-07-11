// ---------------------------
// 1) global module fragment
// ---------------------------
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

// ---------------------------
// 2) Module interface
// ---------------------------

export module net_io_adapters;

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

import modern_io;   // provides modern_io::OutputStream/InputStream
import net_io;      // provides net_io::Transportable, net_io::TcpClient, etc.

// This can be removed when msvc better supports umbrella imports
import net_io.tcp_endpoint;
import net_io.tcp_client;
import net_io.tcp_server;
import net_io.udp_endpoint;
import net_io.udp_transport;
import net_io_concepts; // <--- hinzugefügt

export namespace net_io_adapters
{

    // Adapter: turns any Writable transport into a modern_io::OutputStream
    template<net_io_concepts::Writable T> // <--- angepasst
    class TransportSink
    {
    public:
        explicit TransportSink(T& t) noexcept
            : t_(t)
        {
        }

        void write(const char* data, std::size_t size)
        {
            t_.write(data, size);
        }

        void write(std::span<const std::byte> data)
        {
            write(reinterpret_cast<const char*>(data.data()), data.size());
        }
        void write(std::span<const char> data)
        {
            write(data.data(), data.size());
        }

        void flush() noexcept
        {
            // No userland buffering for sockets
        }

        // Optional: write_to passthrough, if T supports it
        template<typename U = T, typename... Args>
        auto write_to(Args&&... args)
            -> decltype(std::declval<U&>().write_to(std::forward<Args>(args)...))
        {
            return t_.write_to(std::forward<Args>(args)...);
        }

    private:
        T& t_;
    };

    // Adapter: turns any Readable transport into a modern_io::InputStream
    template<net_io_concepts::Readable T> // <--- angepasst
    class TransportSource
    {
    public:
        explicit TransportSource(T& t) noexcept
            : t_(t)
        {
        }

        std::size_t read(char* data, std::size_t size)
        {
            return t_.read(data, size);
        }

        std::size_t read(std::span<std::byte> data)
        {
            return read(reinterpret_cast<char*>(data.data()), data.size());
        }
        std::size_t read(std::span<char> data)
        {
            return read(data.data(), data.size());
        }

        bool eof() const noexcept
        {
            return false;
        }

        // Optional: read with sender address, if T supports it
        std::size_t read(char* data, std::size_t size, sockaddr_storage* from, socklen_t* fromlen)
        {
            if constexpr (requires { t_.read(data, size, from, fromlen); }) 
            {
                return t_.read(data, size, from, fromlen);
            } 
            else 
            {
                // Fallback: not supported
                return 0;
            }
        }

        // Access to the underlying object (e.g. for native_handle)
        T& underlying() noexcept { return t_; }
        const T& underlying() const noexcept { return t_; }

    private:
        T& t_;
    };

    // Adapter for UDP datagram output, buffers until flush
    template<net_io_concepts::Writable T> // <--- angepasst
    class DatagramSink
    {
    public:
        explicit DatagramSink(T& transport) noexcept
            : transport_(transport)
        {
        }

        void write(const char* data, std::size_t size)
        {
            buffer_.insert(buffer_.end(), data, data + size);
        }
        void write(std::span<const std::byte> data)
        {
            write(reinterpret_cast<const char*>(data.data()), data.size());
        }
        void write(std::span<const char> data)
        {
            write(data.data(), data.size());
        }

        void flush() noexcept
        {
            if (!buffer_.empty())
            {
                transport_.write(buffer_.data(), buffer_.size());
                buffer_.clear();
            }
        }

        // Addition: write_to for DuplexDatagramStream
        void write_to(const char* data, std::size_t size, const sockaddr_storage& to_addr, socklen_t to_len)
        {
            transport_.write_to(data, size, to_addr, to_len);
        }

    private:
        T& transport_;
        std::vector<char> buffer_;
    };

    // Factory function for DatagramSink
    template<net_io_concepts::Writable T> // <--- angepasst
    DatagramSink<T> make_datagram_sink(T& t)
    {
        return DatagramSink<T>(t);
    }

    // Adapter for UDP datagram input, reads one datagram at a time
    template<net_io_concepts::Readable T> // <--- angepasst
    class DatagramSource
    {
    public:
        explicit DatagramSource(T& transport) noexcept
            : transport_(transport), pos_(0)
        {
        }
        std::size_t read(char* data, std::size_t size)
        {
            if (pos_ >= buffer_.size())
            {
                // Fetch next datagram
                buffer_.resize(max_packet_);
                auto got = transport_.read(buffer_.data(), buffer_.size());
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
            return transport_.read(data, size, from, fromlen);
        }

        bool eof() const noexcept
        {
            return false;
        }

    private:
        static constexpr std::size_t max_packet_ = 65507; // max UDP payload
        T& transport_;
        std::vector<char> buffer_;
        std::size_t pos_;
    };

    // Factory function for DatagramSource
    template<net_io_concepts::Readable T> // <--- angepasst
    DatagramSource<T> make_datagram_source(T& t)
    {
        return DatagramSource<T>(t);
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
            if (!last_peer_)
                throw std::runtime_error("No peer address known for write");
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
    template<typename T>
    concept UdpEndpointLike = requires(T ep)
    {
        { ep.address }    -> std::convertible_to<std::string>;
        { ep.port }       -> std::convertible_to<uint16_t>;
        { ep.bind_local } -> std::convertible_to<bool>;
        { ep.local_port } -> std::convertible_to<uint16_t>;
    };

    template<typename T>
    concept TcpEndpointLike =
        requires(T ep)
        {
            { ep.address } -> std::convertible_to<std::string>;
            { ep.port }    -> std::convertible_to<uint16_t>;
        }
        && (!UdpEndpointLike<T>);

    // --- Generic Factory ---
    /**
     * @brief Creates a suitable SharedStream for TCP or UDP endpoints.
     * @param ep Endpoint (TcpEndpoint or UdpEndpoint)
     * @return SharedStream for TCP (Source+Sink) or UDP (DuplexDatagramStream)
     */

    // TCP variant: needs keepalive
    template<typename Endpoint>
        requires TcpEndpointLike<Endpoint>
    auto make_shared_stream(const Endpoint& ep)
    {
        // TCP: Duplex stream (read/write/flush/eof)
        // NOTE: shared_ptr must point to a dedicated TcpClient!
        auto client = std::make_shared<net_io::TcpClient>(ep);
        client->open();
        // The TransportSource/Sink must reference *client!
        auto src  = TransportSource<net_io::TcpClient>(*client);
        auto sink = TransportSink<net_io::TcpClient>(*client);
        using DuplexType = TcpDuplexStream<decltype(src), decltype(sink)>;
        struct DuplexWithClient : DuplexType {
            std::shared_ptr<net_io::TcpClient> keepalive_;
            DuplexWithClient(DuplexType&& base, std::shared_ptr<net_io::TcpClient> c)
                : DuplexType(std::move(base)), keepalive_(std::move(c)) {}
        };
        // explicit std::move for DuplexType
        auto duplex = std::make_shared<DuplexWithClient>(
            std::move(DuplexType(std::move(src), std::move(sink))), client
        );
        return SharedStream<DuplexWithClient>(duplex);
    }

    template<typename Endpoint>
        requires UdpEndpointLike<Endpoint>
    auto make_shared_stream(const Endpoint& ep)
    {
        auto udp = std::make_shared<net_io::UdpTransport>();
        udp->open_connect(ep);
        auto src  = make_datagram_source(*udp);
        auto sink = make_datagram_sink(*udp);
        using DuplexType = DuplexDatagramStream<decltype(src), decltype(sink)>;
        struct DuplexWithUdp : DuplexType
        {
            std::shared_ptr<net_io::UdpTransport> keepalive_;
            DuplexWithUdp(DuplexType&& base, std::shared_ptr<net_io::UdpTransport> u)
                : DuplexType(std::move(base)), keepalive_(std::move(u)) {}
        };
        // explicit std::move for DuplexType
        auto duplex = std::make_shared<DuplexWithUdp>(
            std::move(DuplexType(std::move(src), std::move(sink))), udp
        );
        auto shared_duplex = SharedStream<DuplexWithUdp>(duplex);
        shared_duplex.set_peer(ep);
        return shared_duplex;
    }

    // UDP-Server: Factory for SharedStream<DuplexDatagramStream<...>> (with correct namespace)
    inline auto make_shared_stream_for_server(const net_io::UdpEndpoint& ep)
    {
        auto udp = std::make_shared<net_io::UdpTransport>();
        udp->open_bind(ep);
        auto src  = make_datagram_source(*udp);
        auto sink = make_datagram_sink(*udp);
        using DuplexType = DuplexDatagramStream<decltype(src), decltype(sink)>;
        struct DuplexWithKeepalive : DuplexType {
            std::shared_ptr<net_io::UdpTransport> keepalive_;
            DuplexWithKeepalive(DuplexType&& base, std::shared_ptr<net_io::UdpTransport> u)
                : DuplexType(std::move(base)), keepalive_(std::move(u)) {}
        };
        auto duplex = std::make_shared<DuplexWithKeepalive>(
            std::move(DuplexType(std::move(src), std::move(sink))), udp
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
        net_io_concepts::Acceptable ServerType, // <--- angepasst
        net_io_concepts::Transportable ClientType, // <--- angepasst
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
                    auto client = std::make_shared<ClientType>(server->accept());
                    auto stream = make_stream(client, server);
                    executor.execute([stream, on_client]() mutable {
                        on_client(std::move(stream));
                    });
                } catch (const std::exception& ex) {
                    std::cerr << "[net_io_adapters] Error accepting connection: " << ex.what() << std::endl;
                }
            }
        });
    }

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
        auto src  = TransportSource<net_io::TcpClient>(*client);
        auto sink = TransportSink<net_io::TcpClient>(*client);
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
