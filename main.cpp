import modern_io;
import modern_io_async;

import net_io;
import net_io_async;
import net_io_adapters;

// Einzelimporte (MSVC Workaround)
import net_io.tcp_endpoint;
import net_io.tcp_client;
import net_io.tcp_server;
import net_io.udp_endpoint;
import net_io.udp_transport;

#include <iostream>
#include <syncstream>
#include <thread>
#include <chrono>
#include <string>
#include <atomic>
#include <expected>
#include <system_error>
#include <span>
#include <exception>
#include <utility>
#include <latch>
#include <optional>
#include <coroutine>

using namespace modern_io;
using namespace net_io;
using namespace net_io_adapters;

constexpr uint16_t TCP_PORT = 9050;
constexpr uint16_t UDP_PORT = 9050;
std::string address = "127.0.0.1";

// Fire-and-forget Coroutine (keeps simple error reporting)
struct Detached {
    struct promise_type {
        Detached get_return_object() noexcept { return {}; }
        std::suspend_never initial_suspend() noexcept { return {}; }
        std::suspend_never final_suspend() noexcept { return {}; }
        void return_void() noexcept {}
        void unhandled_exception() noexcept {
            try { throw; } catch (const std::exception& ex) {
                std::osyncstream(std::cerr) << "[Detached] Exception: " << ex.what() << '\n';
            } catch (...) {
                std::osyncstream(std::cerr) << "[Detached] Unknown exception\n";
            }
        }
    };
};

// ---------- Helper to synchronously wait on ExpectedTask<void> ----------
// Old wait_expected removed (it didn't start coroutines and caused deadlocks)
// New helper function: explicitly starts the ExpectedTask<void> and waits synchronously.
static std::expected<void, std::error_code> run_and_wait(ExpectedTask<void> t) {
    t.start();                // initial_suspend = suspend_always → start explicitly
    return t.sync_wait();     // sync_wait setzt eigene Continuation & resumed das Task-Handle erneut NICHT (weil schon gestartet)
}

// ---------------- Synchron: TCP Server ----------------
void tcp_server() {
    std::atomic<bool> running{ true };
    ThreadExecutor exec;

    auto tcp_handler = [](auto shared_stream) {
        DataInputStream in(shared_stream);
        DataOutputStream out(shared_stream);
        std::string msg = in.read_string();
        std::osyncstream(std::cout) << "[TCP-Server] Received: " << msg << '\n';
        out.write_string("PONG");
        out.flush();
    };

    run_tcp_server(exec, std::move(tcp_handler), running, TcpEndpoint{ address, TCP_PORT });
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    running = false;
}

// ---------------- Synchron: UDP Server ----------------
void udp_server() {
    UdpEndpoint ep{ address, UDP_PORT, true, UDP_PORT };
    auto s = make_stream(ep);
    DataInputStream in(s);
    DataOutputStream out(s);
    std::osyncstream(std::cout) << "[UDP-Server] Waiting for datagram..." << '\n';
    std::string msg = in.read_string();
    std::osyncstream(std::cout) << "[UDP-Server] Received: " << msg << '\n';
    out.write_string("UDP-PONG");
    out.flush();
    std::this_thread::sleep_for(std::chrono::milliseconds(2000));
}

// ---------------- Async TCP Client Beispiel ----------------
void async_tcp_example() {
    constexpr uint16_t ASYNC_TCP_PORT = TCP_PORT + 10;
    struct Demo {
        static ExpectedTask<void> run(std::string addr, uint16_t port) {
            auto sock = std::make_shared<AsyncTcpSocket>();
            TcpEndpoint ep(addr, port);
            auto sa = ep.to_sockaddr(false);
            std::osyncstream(std::cout) << "[AsyncTCP-Client] Connecting to " << addr << ":" << port << '\n';
            auto conn = co_await sock->async_connect(sa, sizeof(sa));
            if (!conn) { 
                std::osyncstream(std::cerr) << "[AsyncTCP-Client] Connect failed: " << conn.error().message() << '\n';
                co_return std::unexpected(conn.error()); 
            }
            std::osyncstream(std::cout) << "[AsyncTCP-Client] Connected successfully\n";
            AsyncTcpStreamAdapter stream(sock);
            std::string msg = "PING-ASYNC";
            std::osyncstream(std::cout) << "[AsyncTCP-Client] Sending: " << msg << '\n';
            auto w = co_await stream.write_async(std::span<const char>(msg.data(), msg.size()));
            if (!w)
            {
                std::osyncstream(std::cerr) << "[AsyncTCP-Client] Write failed: " << w.error().message() << '\n';
                co_return std::unexpected(w.error()); 
            }
            std::osyncstream(std::cout) << "[AsyncTCP-Client] Write successful, " << w.value() << " bytes sent\n";
            char buf[64]{};
            auto r = co_await stream.read_async(std::span<char>(buf, sizeof(buf)));
            if (!r)
            { 
                co_return std::unexpected(r.error());
            }
            std::osyncstream(std::cout) << "[AsyncTCP] Received: " << std::string(buf, r.value()) << '\n';
            co_return std::expected<void,std::error_code>{};
        }

        static Detached serve_once(AsyncTcpServer& server) {
            auto cli = co_await server.accept();
            if (!cli) co_return;

            auto sock = cli.value();
            AsyncTcpStreamAdapter stream(sock);
            char buf[64]{};
            auto r = co_await stream.read_async(std::span<char>(buf, sizeof(buf)));
            if (r && r.value() > 0) {
                std::string pong = "PONG-ASYNC";
                (void)co_await stream.write_async(std::span<const char>(pong.data(), pong.size()));
            }
        }
    };

    // Mini server for this example
    std::thread([=]{
        AsyncTcpServer server;
        auto listen_res = server.start(TcpEndpoint(address, ASYNC_TCP_PORT));
        if (!listen_res) return;

        Demo::serve_once(server);
        
    // Keep thread alive until the client finishes
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }).detach();

    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    std::thread([=]{
    
        auto t = Demo::run(address, ASYNC_TCP_PORT);
        t.start();
        // auto rc = t.sync_wait();
        // if (!rc) {
    //     std::osyncstream(std::cerr) << "[AsyncTCP] Error: " << rc.error().message() << '\n';
        // }
    }).join();
}

// ---------------- Async UDP Beispiel ----------------
void async_udp_example() {
    struct UdpDemo {
        static ExpectedTask<void> server(std::string addr, uint16_t port) {
            auto sock = std::make_shared<AsyncUdpSocket>();
            UdpEndpoint ep(addr, port, true, port);
            auto sa = ep.to_sockaddr(true);
            auto b = sock->bind(sa, sizeof(sa));
            if (!b) co_return std::unexpected(b.error());

            AsyncUdpStreamAdapter stream(sock);
            char buf[128]{};

            // Simple call — retry logic is handled by the adapter
            auto r = co_await stream.read_async(std::span<char>(buf, sizeof(buf)));
            if (!r) co_return std::unexpected(r.error());

            std::string msg(buf, r.value());
            std::osyncstream(std::cout) << "[AsyncUDP-Server] Received: " << msg << '\n';

            std::string reply = "UDP-ASYNC-PONG";
            // Einfacher Aufruf - Retry-Logik ist im Adapter
            auto w = co_await stream.write_async(std::span<const char>(reply.data(), reply.size()));
            if (!w) co_return std::unexpected(w.error());

            co_return std::expected<void,std::error_code>{};
        }

        static ExpectedTask<void> client(std::string addr, uint16_t port) {
            auto sock = std::make_shared<AsyncUdpSocket>();
            std::osyncstream(std::cout) << "[AsyncUDP-Client] Creating socket for " << addr << ":" << port << '\n';
            
            // EXPLICIT BIND for client socket
            UdpEndpoint client_ep("0.0.0.0", 0, false, 0);  // Bind to any available port
            auto client_sa = client_ep.to_sockaddr(true);
            auto bind_res = sock->bind(client_sa, sizeof(client_sa));
            if (!bind_res) {
                std::osyncstream(std::cerr) << "[AsyncUDP-Client] Bind failed: " << bind_res.error().message() << '\n';
                co_return std::unexpected(bind_res.error());
            }
            std::osyncstream(std::cout) << "[AsyncUDP-Client] Socket bound successfully\n";
            
            AsyncUdpStreamAdapter stream(sock);
            
            // Set target address via the adapter
            UdpEndpoint ep(addr, port);
            auto sa = ep.to_sockaddr(false);
            stream.set_default_target(sa, sizeof(sa));
            std::osyncstream(std::cout) << "[AsyncUDP-Client] Target set via adapter\n";

            std::string msg = "UDP-ASYNC-PING";
            std::osyncstream(std::cout) << "[AsyncUDP-Client] Sending: " << msg << '\n';
    
            // debug: bind result available (suppressed)
    
            // Einfacher Aufruf - Retry-Logik ist im Adapter
            auto w = co_await stream.write_async(std::span<const char>(msg.data(), msg.size()));
            if (!w) {
                std::osyncstream(std::cerr) << "UDP-Client write_async failed: " << w.error().message() << "\n";
                co_return std::unexpected(w.error());
            }
            std::osyncstream(std::cout) << "[AsyncUDP-Client] Sent: " << msg << '\n';

            char buf[128]{};
            // Einfacher Aufruf - Retry-Logik ist im Adapter
            auto r = co_await stream.read_async(std::span<char>(buf, sizeof(buf)));
            if (!r) co_return std::unexpected(r.error());
            
            std::osyncstream(std::cout) << "[AsyncUDP-Client] Received: " << std::string(buf, r.value()) << '\n';
            co_return std::expected<void,std::error_code>{};
        }
    };

    std::thread([] {
    // Server coroutine: fire-and-forget (do not call sync_wait here)
        auto server_task = [](std::string addr, uint16_t port) -> Detached {
            auto sock = std::make_shared<AsyncUdpSocket>();
            UdpEndpoint ep(addr, port, true, port);
            auto sa = ep.to_sockaddr(true);
            auto b = sock->bind(sa, sizeof(sa));
            if (!b) co_return;

            AsyncUdpStreamAdapter stream(sock);
            char buf[128]{};

            // Simple call — retry logic is handled by the adapter
            auto r = co_await stream.read_async(std::span<char>(buf, sizeof(buf)));
            if (!r) co_return;

            std::string msg(buf, r.value());
            std::osyncstream(std::cout) << "[AsyncUDP-Server] Received: " << msg << '\n';

            std::string reply = "UDP-ASYNC-PONG";
            // Einfacher Aufruf - Retry-Logik ist im Adapter
            auto w = co_await stream.write_async(std::span<const char>(reply.data(), reply.size()));
            if (!w) co_return;
        }(address, UDP_PORT+10);
        
    // Keep the thread alive
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }).detach();

    std::this_thread::sleep_for(std::chrono::milliseconds(500)); // Wait a bit longer

     std::thread([] {
        auto ct = UdpDemo::client(address, UDP_PORT+10);
        ct.start();
        // auto cr = ct.sync_wait();
        // if (!cr) std::osyncstream(std::cerr) << "[AsyncUDP-Client] Error: " << cr.error().message() << '\n';
    }).join();
}

// ---------------- Echter Async TCP Server (AsyncTcpServer) ----------------
void async_tcp_server_example() {
    constexpr uint16_t ASYNC_TCP_PORT = TCP_PORT + 10;
    auto client_handler = [](std::shared_ptr<AsyncTcpSocket> sock) -> Detached {
        AsyncTcpStreamAdapter stream(sock);
        AsyncDataInputStream<AsyncTcpStreamAdapter>  in(stream);
        AsyncDataOutputStream<AsyncTcpStreamAdapter> out(stream);

        auto r_res = co_await in.read_string();
        if (!r_res) {
            std::osyncstream(std::cerr) << "[AsyncTCP-Server] read_string error: "
                                        << r_res.error().message() << '\n';
            co_return;
        }
        std::osyncstream(std::cout) << "[AsyncTCP-Server] Received: " << *r_res << '\n';

        auto w_res = co_await out.write_string("PONG-ASYNC");
        if (!w_res) {
            std::osyncstream(std::cerr) << "[AsyncTCP-Server] write_string error: "
                                        << w_res.error().message() << '\n';
            co_return;
        }
        auto f_res = out.flush();
        if (!f_res) {
            std::osyncstream(std::cerr) << "[AsyncTCP-Server] flush error: "
                                        << f_res.error().message() << '\n';
        }
    };

    auto server_task = [client_handler](std::string addr, uint16_t port) -> ExpectedTask<void> {
        AsyncTcpServer server;
        auto start_res = server.start(TcpEndpoint(addr, port));
        if (!start_res) {
            std::osyncstream(std::cerr) << "[AsyncTCP-Server] Listen failed: "
                                        << start_res.error().message() << '\n';
            co_return std::unexpected(start_res.error());
        }
        std::osyncstream(std::cout) << "[AsyncTCP-Server] Listening on " << addr << ":" << port << '\n';

        for (;;) {
            auto accepted = co_await server.accept();
            if (!accepted) {
                // std::osyncstream(std::cerr) << "[AsyncTCP-Server] Accept error: "
                //                             << accepted.error().message() << '\n';
                continue;
            }
            client_handler(accepted.value());
        }
        // unreachable
        // co_return {};
    };

    std::thread([&] {
    // runs indefinitely
        {
            auto st = server_task(address, ASYNC_TCP_PORT + 1);
            auto sr = run_and_wait(std::move(st));
            if (!sr) {
                std::osyncstream(std::cerr) << "[AsyncTCP-ServerMain] Error: " << sr.error().message() << '\n';
            }
        }
    }).detach();

    // Test-Client
    std::thread([&] {
        constexpr uint16_t ASYNC_TCP_PORT = TCP_PORT + 10;
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        auto sock = std::make_shared<AsyncTcpSocket>();
        TcpEndpoint ep(address, ASYNC_TCP_PORT+1);
        auto sa = ep.to_sockaddr(false);
        auto client_task = [sock, sa]() -> ExpectedTask<void> {
            auto conn = co_await sock->async_connect(sa, sizeof(sa));
            if (!conn) {
                std::osyncstream(std::cerr) << "[AsyncTCP-Client] Connect failed: "
                                            << conn.error().message() << '\n';
                co_return std::unexpected(conn.error());
            }
            AsyncTcpStreamAdapter stream(sock);
            AsyncDataOutputStream out(stream);
            AsyncDataInputStream  in(stream);

            auto w_res = co_await out.write_string("PING-ASYNC");
            if (!w_res) co_return std::unexpected(w_res.error());
            auto f_res = out.flush();
            if (!f_res) co_return std::unexpected(f_res.error());

            auto r_res = co_await in.read_string();
            if (r_res)
                std::osyncstream(std::cout) << "[AsyncTCP-Client] Received: " << *r_res << '\n';
            co_return {};
        };
        {
            auto ct = client_task();
            auto cr = run_and_wait(std::move(ct));
            if (!cr) {
                std::osyncstream(std::cerr) << "[AsyncTCP-ClientMain] Error: " << cr.error().message() << '\n';
            }
        }
    }).join();
}

// ---------------- Async Buffered Beispiel ----------------
void async_buffered_example() {
    auto task = [] (std::string addr, uint16_t port) -> ExpectedTask<void> {
        auto sock = std::make_shared<AsyncTcpSocket>();
        TcpEndpoint ep(addr, port);
        auto sa = ep.to_sockaddr(false);
        auto conn = co_await sock->async_connect(sa, sizeof(sa));
        if (!conn) co_return std::unexpected(conn.error());

        AsyncTcpStreamAdapter stream(sock);
        AsyncBufferedOutputStream bout(stream);
        AsyncDataOutputStream dout(std::move(bout));

        auto w = co_await dout.write_string("Hello Async Buffer!");
        if (!w) co_return std::unexpected(w.error());
        auto f = dout.flush();
        if (!f) co_return std::unexpected(f.error());

        AsyncBufferedInputStream bin(stream);
        AsyncDataInputStream din(std::move(bin));
        auto s_res = co_await din.read_string();
        if (s_res) {
            std::osyncstream(std::cout) << "[AsyncBuffered] Read: " << *s_res << '\n';
        }
        co_return {};
    };
    {
        auto bt = task(address, TCP_PORT);
        auto br = run_and_wait(std::move(bt));
        if (!br) {
            std::osyncstream(std::cerr) << "[AsyncBuffered] Error: " << br.error().message() << '\n';
        }
    }
}

int main() {
    // Start EventLoop in a separate thread so the main thread is not blocked
    std::thread event_loop_thread([]() {
        EventLoop::instance().start();
    });

    // Short sleep to let EventLoop initialize
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Sync TCP
    {
        tcp_server();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        TcpEndpoint ep({ address, TCP_PORT });
        auto stream = make_stream(ep);
        DataOutputStream out(stream);
        DataInputStream  in(stream);
        out.write_string("PING");
        out.flush();
        std::string reply = in.read_string();
        std::osyncstream(std::cout) << "[TCP-Client] Received: " << reply << '\n';
    }

    // Sync UDP
    {
        std::thread srv(udp_server);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        UdpEndpoint client_ep(address, UDP_PORT);
        auto stream = make_stream(client_ep);
        DataOutputStream out(stream);
        DataInputStream  in(stream);
        std::osyncstream(std::cout) << "[UDP-Client] Sending UDP-PING..." << '\n';
        out.write_string("UDP-PING");
        out.flush();
        std::osyncstream(std::cout) << "[UDP-Client] Waiting for reply..." << '\n';
        std::string reply = in.read_string();
        std::osyncstream(std::cout) << "[UDP-Client] Received: " << reply << '\n';
        srv.join();
    }

    // File unbuffered
    {
        FileOutputStream fs("test.bin");
        DataOutputStream dout(std::move(fs));
        dout.write_string("Hello File!");
        dout.flush();
        FileInputStream fi("test.bin");
        DataInputStream din(std::move(fi));
        std::string s = din.read_string();
        std::osyncstream(std::cout) << "[File] Read: " << s << '\n';
    }

    // File buffered
    {
        FileOutputStream fs("test2.bin");
        BufferedOutputStream bos(std::move(fs));
        DataOutputStream dout(std::move(bos));
        dout.write_string("Hello Buffer!");
        dout.flush();
        FileInputStream fi("test2.bin");
        BufferedInputStream bis(std::move(fi));
        DataInputStream din(std::move(bis));
        std::string s = din.read_string();
        std::osyncstream(std::cout) << "[File-Buffered] Read: " << s << '\n';
    }

    std::osyncstream(std::cout) << "\n[AsyncTCP] Beispiel:\n";
    async_tcp_example();

    std::osyncstream(std::cout) << "\n[AsyncUDP] Beispiel:\n";
    async_udp_example();

    std::osyncstream(std::cout) << "\n[AsyncTCP-Server] Beispiel:\n";
    async_tcp_server_example();

    std::osyncstream(std::cout) << "\n[AsyncBuffered] Beispiel:\n";
    async_buffered_example();

    EventLoop::instance().stop(); // EventLoop stoppen
    event_loop_thread.join();     // Auf EventLoop-Thread warten
    return 0;
}
// File: main.cpp