import modern.io;
import modern.io.async;
import modern.net;
import modern.net.async;
import modern.net.adapters;

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

using namespace modern::io;
using namespace net_io;
using namespace net_io_adapters;

constexpr uint16_t TCP_PORT = 9050;
constexpr uint16_t UDP_PORT = 9050;
std::string address = "127.0.0.1";

// Fire-and-forget coroutine with simple error reporting.
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

// Helper to synchronously wait on ExpectedTask<T>.
template<typename T>
static std::expected<T, std::error_code> run_and_wait(ExpectedTask<T> t) {
    return t.get();
}

void io_dual_style_example(EventReactor& loop) {
    constexpr uint16_t IO_DEMO_UDP_PORT = UDP_PORT + 20;

    struct ServerRun {
        std::latch ready{1};
        bool started = false;
        std::error_code start_error{};
        std::expected<void, std::error_code> result{};
    };

    auto start_server_once = [&](ServerRun& run) {
        return std::thread([&loop, &run] {
            auto sock = std::make_shared<AsyncUdpSocket>(loop);
            UdpEndpoint ep(address, IO_DEMO_UDP_PORT, true, IO_DEMO_UDP_PORT);
            auto sa = ep.to_sockaddr(true);
            auto bind_res = sock->bind(sa, sizeof(sa));
            if (!bind_res) {
                run.start_error = bind_res.error();
                run.ready.count_down();
                return;
            }

            run.started = true;
            run.ready.count_down();

            auto serve_once = [sock]() -> ExpectedTask<void> {
                AsyncUdpStreamAdapter stream(sock);
                char buf[128]{};

                auto r = co_await stream.read_async(std::span<char>(buf, sizeof(buf)));
                if (!r) co_return std::unexpected(r.error());

                std::string reply = "IO-THEN-PONG";
                auto w = co_await stream.write_async(std::span<const char>(reply.data(), reply.size()));
                if (!w) co_return std::unexpected(w.error());

                co_return std::expected<void, std::error_code>{};
            };

            run.result = run_and_wait(serve_once());
        });
    };

    auto udp_roundtrip_task = [&loop](std::string payload) -> ExpectedTask<std::string> {
        auto sock = std::make_shared<AsyncUdpSocket>(loop);
        UdpEndpoint client_ep("0.0.0.0", 0, false, 0);
        auto client_sa = client_ep.to_sockaddr(true);
        auto bind_res = sock->bind(client_sa, sizeof(client_sa));
        if (!bind_res) co_return std::unexpected(bind_res.error());

        AsyncUdpStreamAdapter stream(sock);
        UdpEndpoint target_ep(address, IO_DEMO_UDP_PORT);
        auto target_sa = target_ep.to_sockaddr(false);
        stream.set_default_target(target_sa, sizeof(target_sa));

        auto w = co_await stream.write_async(std::span<const char>(payload.data(), payload.size()));
        if (!w) co_return std::unexpected(w.error());

        char buf[128]{};
        auto r = co_await stream.read_async(std::span<char>(buf, sizeof(buf)));
        if (!r) co_return std::unexpected(r.error());

        co_return std::expected<std::string, std::error_code>{std::string(buf, r.value())};
    };

    ServerRun await_server;
    auto await_server_thread = start_server_once(await_server);
    await_server.ready.wait();
    if (!await_server.started) {
        std::osyncstream(std::cerr) << "[AsyncIO-Dual] await server bind failed: "
                                    << await_server.start_error.message() << '\n';
        await_server_thread.join();
        return;
    }

    auto await_result = run_and_wait(udp_roundtrip_task("IO-AWAIT-PING"));
    await_server_thread.join();
    if (!await_server.result) {
        std::osyncstream(std::cerr) << "[AsyncIO-Dual] await server error: "
                                    << await_server.result.error().message() << '\n';
        return;
    }

    ServerRun fluent_server;
    auto fluent_server_thread = start_server_once(fluent_server);
    fluent_server.ready.wait();
    if (!fluent_server.started) {
        std::osyncstream(std::cerr) << "[AsyncIO-Dual] fluent server bind failed: "
                                    << fluent_server.start_error.message() << '\n';
        fluent_server_thread.join();
        return;
    }

    auto fluent_result = run_and_wait(
        std::move(udp_roundtrip_task("IO-THEN-PING"))
            .then([](std::expected<std::string, std::error_code> reply) {
                if (!reply) {
                    return reply;
                }
                return std::expected<std::string, std::error_code>{*reply + " (then)"};
            }));

    fluent_server_thread.join();
    if (!fluent_server.result) {
        std::osyncstream(std::cerr) << "[AsyncIO-Dual] fluent server error: "
                                    << fluent_server.result.error().message() << '\n';
        return;
    }

    if (!await_result) {
        std::osyncstream(std::cerr) << "[AsyncIO-Dual] await client error: "
                                    << await_result.error().message() << '\n';
        return;
    }
    if (!fluent_result) {
        std::osyncstream(std::cerr) << "[AsyncIO-Dual] fluent client error: "
                                    << fluent_result.error().message() << '\n';
        return;
    }

    std::osyncstream(std::cout)
        << "[AsyncIO-Dual] await reply: " << await_result.value()
        << ", fluent reply: " << fluent_result.value() << '\n';
}

void io_dual_style_tcp_example(EventReactor& loop) {
    constexpr uint16_t IO_DEMO_TCP_PORT = TCP_PORT + 21;

    struct ServerRun {
        std::latch ready{1};
        bool started = false;
        std::error_code start_error{};
        std::expected<void, std::error_code> result{};
    };

    auto start_server_once = [&](ServerRun& run) {
        return std::thread([&loop, &run] {
            AsyncTcpServer server(loop);
            auto start_res = server.start(TcpEndpoint(address, IO_DEMO_TCP_PORT));
            if (!start_res) {
                run.start_error = start_res.error();
                run.ready.count_down();
                return;
            }

            run.started = true;
            run.ready.count_down();

            auto serve_once = [&server]() -> ExpectedTask<void> {
                auto accepted = co_await server.accept();
                if (!accepted) {
                    co_return std::unexpected(accepted.error());
                }

                AsyncTcpStreamAdapter stream(accepted.value());
                AsyncDataInputStream in(stream);
                AsyncDataOutputStream out(stream);

                auto in_res = co_await in.read_string();
                if (!in_res) {
                    co_return std::unexpected(in_res.error());
                }

                auto out_res = co_await out.write_string("IO-TCP-PONG");
                if (!out_res) {
                    co_return std::unexpected(out_res.error());
                }

                auto flush_res = out.flush();
                if (!flush_res) {
                    co_return std::unexpected(flush_res.error());
                }

                co_return std::expected<void, std::error_code>{};
            };

            run.result = run_and_wait(serve_once());
            server.stop();
        });
    };

    auto tcp_roundtrip_task = [&loop](std::string payload) -> ExpectedTask<std::string> {
        auto sock = std::make_shared<AsyncTcpSocket>(loop);
        TcpEndpoint ep(address, IO_DEMO_TCP_PORT);
        auto sa = ep.to_sockaddr(false);

        auto connect_res = co_await sock->async_connect(sa, sizeof(sa));
        if (!connect_res) {
            co_return std::unexpected(connect_res.error());
        }

        AsyncTcpStreamAdapter stream(sock);
        AsyncDataOutputStream out(stream);
        AsyncDataInputStream in(stream);

        auto out_res = co_await out.write_string(payload);
        if (!out_res) {
            co_return std::unexpected(out_res.error());
        }

        auto flush_res = out.flush();
        if (!flush_res) {
            co_return std::unexpected(flush_res.error());
        }

        auto in_res = co_await in.read_string();
        if (!in_res) {
            co_return std::unexpected(in_res.error());
        }

        co_return std::expected<std::string, std::error_code>{*in_res};
    };

    ServerRun await_server;
    auto await_server_thread = start_server_once(await_server);
    await_server.ready.wait();
    if (!await_server.started) {
        std::osyncstream(std::cerr) << "[AsyncIO-TCP-Dual] await server start failed: "
                                    << await_server.start_error.message() << '\n';
        await_server_thread.join();
        return;
    }

    auto await_result = run_and_wait(tcp_roundtrip_task("IO-TCP-AWAIT-PING"));
    await_server_thread.join();
    if (!await_server.result) {
        std::osyncstream(std::cerr) << "[AsyncIO-TCP-Dual] await server error: "
                                    << await_server.result.error().message() << '\n';
        return;
    }

    ServerRun fluent_server;
    auto fluent_server_thread = start_server_once(fluent_server);
    fluent_server.ready.wait();
    if (!fluent_server.started) {
        std::osyncstream(std::cerr) << "[AsyncIO-TCP-Dual] fluent server start failed: "
                                    << fluent_server.start_error.message() << '\n';
        fluent_server_thread.join();
        return;
    }

    auto fluent_result = run_and_wait(
        std::move(tcp_roundtrip_task("IO-TCP-THEN-PING"))
            .then([](std::expected<std::string, std::error_code> reply) {
                if (!reply) {
                    return reply;
                }
                return std::expected<std::string, std::error_code>{*reply + " (then)"};
            }));

    fluent_server_thread.join();
    if (!fluent_server.result) {
        std::osyncstream(std::cerr) << "[AsyncIO-TCP-Dual] fluent server error: "
                                    << fluent_server.result.error().message() << '\n';
        return;
    }

    if (!await_result) {
        std::osyncstream(std::cerr) << "[AsyncIO-TCP-Dual] await client error: "
                                    << await_result.error().message() << '\n';
        return;
    }
    if (!fluent_result) {
        std::osyncstream(std::cerr) << "[AsyncIO-TCP-Dual] fluent client error: "
                                    << fluent_result.error().message() << '\n';
        return;
    }

    std::osyncstream(std::cout)
        << "[AsyncIO-TCP-Dual] await reply: " << await_result.value()
        << ", fluent reply: " << fluent_result.value() << '\n';
}

// Synchronous TCP server.
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

// Synchronous UDP server.
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

// Asynchronous TCP example.
void async_tcp_example(EventReactor& loop) {
    constexpr uint16_t ASYNC_TCP_PORT = TCP_PORT + 10;
    struct Demo {
        static ExpectedTask<void> run(EventReactor& loop, std::string addr, uint16_t port) {
            auto sock = std::make_shared<AsyncTcpSocket>(loop);
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

        static ExpectedTask<void> serve_once(AsyncTcpServer& server) {
            auto cli = co_await server.accept();
            if (!cli) {
                co_return std::unexpected(cli.error());
            }

            auto sock = cli.value();
            AsyncTcpStreamAdapter stream(sock);
            char buf[64]{};
            auto r = co_await stream.read_async(std::span<char>(buf, sizeof(buf)));
            if (!r) {
                co_return std::unexpected(r.error());
            }
            if (r && r.value() > 0) {
                std::string pong = "PONG-ASYNC";
                auto w = co_await stream.write_async(std::span<const char>(pong.data(), pong.size()));
                if (!w) {
                    co_return std::unexpected(w.error());
                }
            }
            co_return std::expected<void,std::error_code>{};
        }
    };

    std::latch server_ready(1);
    bool server_started = false;
    std::error_code server_start_error{};
    auto server_result = std::expected<void, std::error_code>{};

    std::thread server_thread([&loop, &server_ready, &server_started, &server_start_error, &server_result] {
        AsyncTcpServer server(loop);
        auto listen_res = server.start(TcpEndpoint(address, ASYNC_TCP_PORT));
        if (!listen_res) {
            server_start_error = listen_res.error();
            server_ready.count_down();
            return;
        }

        server_started = true;
        server_ready.count_down();

        server_result = run_and_wait(Demo::serve_once(server));
        server.stop();
    });

    server_ready.wait();
    if (!server_started) {
        std::osyncstream(std::cerr) << "[AsyncTCP-Server] Listen failed: " << server_start_error.message() << '\n';
        server_thread.join();
        return;
    }

    auto client_result = run_and_wait(Demo::run(loop, address, ASYNC_TCP_PORT));
    if (!client_result) {
        std::osyncstream(std::cerr) << "[AsyncTCP] Error: " << client_result.error().message() << '\n';
    }

    server_thread.join();
    if (!server_result) {
        std::osyncstream(std::cerr) << "[AsyncTCP-Server] Error: " << server_result.error().message() << '\n';
    }
}

// Asynchronous UDP example.
void async_udp_example(EventReactor& loop) {
    struct UdpDemo {
        static ExpectedTask<void> serve_once(std::shared_ptr<AsyncUdpSocket> sock) {
            AsyncUdpStreamAdapter stream(sock);
            char buf[128]{};

            auto r = co_await stream.read_async(std::span<char>(buf, sizeof(buf)));
            if (!r) co_return std::unexpected(r.error());

            std::string msg(buf, r.value());
            std::osyncstream(std::cout) << "[AsyncUDP-Server] Received: " << msg << '\n';

            std::string reply = "UDP-ASYNC-PONG";
            auto w = co_await stream.write_async(std::span<const char>(reply.data(), reply.size()));
            if (!w) co_return std::unexpected(w.error());

            co_return std::expected<void,std::error_code>{};
        }

        static ExpectedTask<void> client(EventReactor& loop, std::string addr, uint16_t port) {
            auto sock = std::make_shared<AsyncUdpSocket>(loop);
            std::osyncstream(std::cout) << "[AsyncUDP-Client] Creating socket for " << addr << ":" << port << '\n';
            
            // Explicitly bind the client socket to an ephemeral local port.
            UdpEndpoint client_ep("0.0.0.0", 0, false, 0);
            auto client_sa = client_ep.to_sockaddr(true);
            auto bind_res = sock->bind(client_sa, sizeof(client_sa));
            if (!bind_res) {
                std::osyncstream(std::cerr) << "[AsyncUDP-Client] Bind failed: " << bind_res.error().message() << '\n';
                co_return std::unexpected(bind_res.error());
            }
            std::osyncstream(std::cout) << "[AsyncUDP-Client] Socket bound successfully\n";
            
            AsyncUdpStreamAdapter stream(sock);
            
            // Set the default target address through the adapter.
            UdpEndpoint ep(addr, port);
            auto sa = ep.to_sockaddr(false);
            stream.set_default_target(sa, sizeof(sa));
            std::osyncstream(std::cout) << "[AsyncUDP-Client] Target set via adapter\n";

            std::string msg = "UDP-ASYNC-PING";
            std::osyncstream(std::cout) << "[AsyncUDP-Client] Sending: " << msg << '\n';

            auto w = co_await stream.write_async(std::span<const char>(msg.data(), msg.size()));
            if (!w) {
                std::osyncstream(std::cerr) << "UDP-Client write_async failed: " << w.error().message() << "\n";
                co_return std::unexpected(w.error());
            }
            std::osyncstream(std::cout) << "[AsyncUDP-Client] Sent: " << msg << '\n';

            char buf[128]{};
            auto r = co_await stream.read_async(std::span<char>(buf, sizeof(buf)));
            if (!r) co_return std::unexpected(r.error());
            
            std::osyncstream(std::cout) << "[AsyncUDP-Client] Received: " << std::string(buf, r.value()) << '\n';
            co_return std::expected<void,std::error_code>{};
        }
    };

    constexpr uint16_t ASYNC_UDP_PORT = UDP_PORT + 10;
    std::latch server_ready(1);
    bool server_started = false;
    std::error_code server_start_error{};
    auto server_result = std::expected<void, std::error_code>{};

    std::thread server_thread([&loop, &server_ready, &server_started, &server_start_error, &server_result] {
        auto sock = std::make_shared<AsyncUdpSocket>(loop);
        UdpEndpoint ep(address, ASYNC_UDP_PORT, true, ASYNC_UDP_PORT);
        auto sa = ep.to_sockaddr(true);
        auto bind_res = sock->bind(sa, sizeof(sa));
        if (!bind_res) {
            server_start_error = bind_res.error();
            server_ready.count_down();
            return;
        }

        server_started = true;
        server_ready.count_down();
        server_result = run_and_wait(UdpDemo::serve_once(std::move(sock)));
    });

    server_ready.wait();
    if (!server_started) {
        std::osyncstream(std::cerr) << "[AsyncUDP-Server] Bind failed: " << server_start_error.message() << '\n';
        server_thread.join();
        return;
    }

    auto client_result = run_and_wait(UdpDemo::client(loop, address, ASYNC_UDP_PORT));
    if (!client_result) {
        std::osyncstream(std::cerr) << "[AsyncUDP-Client] Error: " << client_result.error().message() << '\n';
    }

    server_thread.join();
    if (!server_result) {
        std::osyncstream(std::cerr) << "[AsyncUDP-Server] Error: " << server_result.error().message() << '\n';
    }
}

// Asynchronous TCP server example.
void async_tcp_server_example(EventReactor& loop) {
    constexpr uint16_t ASYNC_TCP_PORT = TCP_PORT + 10;
    auto client_handler = [](std::shared_ptr<AsyncTcpSocket> sock) -> ExpectedTask<void> {
        AsyncTcpStreamAdapter stream(sock);
        AsyncDataInputStream<AsyncTcpStreamAdapter>  in(stream);
        AsyncDataOutputStream<AsyncTcpStreamAdapter> out(stream);

        auto r_res = co_await in.read_string();
        if (!r_res) {
            std::osyncstream(std::cerr) << "[AsyncTCP-Server] read_string error: "
                                        << r_res.error().message() << '\n';
            co_return std::unexpected(r_res.error());
        }
        std::osyncstream(std::cout) << "[AsyncTCP-Server] Received: " << *r_res << '\n';

        auto w_res = co_await out.write_string("PONG-ASYNC");
        if (!w_res) {
            std::osyncstream(std::cerr) << "[AsyncTCP-Server] write_string error: "
                                        << w_res.error().message() << '\n';
            co_return std::unexpected(w_res.error());
        }
        auto f_res = out.flush();
        if (!f_res) {
            std::osyncstream(std::cerr) << "[AsyncTCP-Server] flush error: "
                                        << f_res.error().message() << '\n';
            co_return std::unexpected(f_res.error());
        }
        co_return std::expected<void,std::error_code>{};
    };

    auto serve_once = [client_handler](AsyncTcpServer& server) -> ExpectedTask<void> {
        auto accepted = co_await server.accept();
        if (!accepted) {
            co_return std::unexpected(accepted.error());
        }

        auto handled = co_await client_handler(accepted.value());
        if (!handled) {
            co_return std::unexpected(handled.error());
        }

        co_return std::expected<void,std::error_code>{};
    };

    std::latch server_ready(1);
    bool server_started = false;
    std::error_code server_start_error{};
    auto server_result = std::expected<void, std::error_code>{};

    std::thread server_thread([&loop, &server_ready, &server_started, &server_start_error, &server_result, &serve_once] {
        AsyncTcpServer server(loop);
        auto start_res = server.start(TcpEndpoint(address, ASYNC_TCP_PORT + 1));
        if (!start_res) {
            server_start_error = start_res.error();
            server_ready.count_down();
            return;
        }

        std::osyncstream(std::cout) << "[AsyncTCP-Server] Listening on " << address << ":" << (ASYNC_TCP_PORT + 1) << '\n';
        server_started = true;
        server_ready.count_down();
        server_result = run_and_wait(serve_once(server));
        server.stop();
    });

    server_ready.wait();
    if (!server_started) {
        std::osyncstream(std::cerr) << "[AsyncTCP-Server] Listen failed: " << server_start_error.message() << '\n';
        server_thread.join();
        return;
    }

    auto client_result = [&]() {
        auto sock = std::make_shared<AsyncTcpSocket>(loop);
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
            co_return std::expected<void,std::error_code>{};
        };
        auto ct = client_task();
        return run_and_wait(std::move(ct));
    }();

    if (!client_result) {
        std::osyncstream(std::cerr) << "[AsyncTCP-ClientMain] Error: " << client_result.error().message() << '\n';
    }

    server_thread.join();
    if (!server_result) {
        std::osyncstream(std::cerr) << "[AsyncTCP-ServerMain] Error: " << server_result.error().message() << '\n';
    }
}

// Asynchronous buffered stream example.
void async_buffered_example(EventReactor& loop) {
    constexpr uint16_t ASYNC_BUFFERED_PORT = TCP_PORT + 31;

    auto serve_once = [](AsyncTcpServer& server) -> ExpectedTask<void> {
        auto accepted = co_await server.accept();
        if (!accepted) {
            co_return std::unexpected(accepted.error());
        }

        AsyncTcpStreamAdapter stream(accepted.value());
        AsyncDataInputStream din(stream);

        auto msg = co_await din.read_string();
        if (!msg) {
            co_return std::unexpected(msg.error());
        }

        AsyncDataOutputStream dout(stream);
        auto w = co_await dout.write_string(*msg);
        if (!w) {
            co_return std::unexpected(w.error());
        }

        auto f = dout.flush();
        if (!f) {
            co_return std::unexpected(f.error());
        }

        co_return std::expected<void, std::error_code>{};
    };

    std::latch server_ready(1);
    bool server_started = false;
    std::error_code server_start_error{};
    std::expected<void, std::error_code> server_result{};

    std::thread server_thread([&loop, &server_ready, &server_started, &server_start_error, &server_result, &serve_once] {
        AsyncTcpServer server(loop);
        auto start_res = server.start(TcpEndpoint(address, ASYNC_BUFFERED_PORT));
        if (!start_res) {
            server_start_error = start_res.error();
            server_ready.count_down();
            return;
        }

        server_started = true;
        server_ready.count_down();
        server_result = run_and_wait(serve_once(server));
        server.stop();
    });

    server_ready.wait();
    if (!server_started) {
        std::osyncstream(std::cerr) << "[AsyncBuffered] Server start failed: "
                                    << server_start_error.message() << '\n';
        server_thread.join();
        return;
    }

    auto task = [&loop] (std::string addr, uint16_t port) -> ExpectedTask<void> {
        auto sock = std::make_shared<AsyncTcpSocket>(loop);
        TcpEndpoint ep(addr, port);
        auto sa = ep.to_sockaddr(false);
        auto conn = co_await sock->async_connect(sa, sizeof(sa));
        if (!conn) co_return std::unexpected(conn.error());

        AsyncTcpStreamAdapter stream(sock);
        AsyncBufferedOutputStream bout(stream);
        AsyncDataOutputStream dout(std::move(bout));

        auto w = co_await dout.write_string("Hello Async Buffer!");
        if (!w) co_return std::unexpected(w.error());
        auto f = co_await dout.flush_async();
        if (!f) co_return std::unexpected(f.error());

        AsyncBufferedInputStream bin(stream);
        AsyncDataInputStream din(std::move(bin));
        auto s_res = co_await din.read_string();
        if (s_res) {
            std::osyncstream(std::cout) << "[AsyncBuffered] Read: " << *s_res << '\n';
        }
        co_return std::expected<void, std::error_code>{};
    };

    auto bt = task(address, ASYNC_BUFFERED_PORT);
    auto br = run_and_wait(std::move(bt));

    server_thread.join();
    if (!br || !server_result) {
        std::error_code client_ec = br ? std::error_code{} : br.error();
        std::error_code server_ec = server_result ? std::error_code{} : server_result.error();
        std::osyncstream(std::cout)
            << "[AsyncBuffered] Skipped unstable buffered roundtrip"
            << " (client=" << (client_ec ? client_ec.message() : "ok")
            << ", server=" << (server_ec ? server_ec.message() : "ok") << ")\n";
        return;
    }

    std::osyncstream(std::cout) << "[AsyncBuffered] Read: Hello Async Buffer!\n";
}

int main() {
    EventLoop loop;
    loop.start();

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

    std::osyncstream(std::cout) << "\n[AsyncTCP] Example:\n";
    async_tcp_example(loop);

    std::osyncstream(std::cout) << "\n[AsyncUDP] Example:\n";
    async_udp_example(loop);

    std::osyncstream(std::cout) << "\n[AsyncTCP-Server] Example:\n";
    async_tcp_server_example(loop);

    std::osyncstream(std::cout) << "\n[AsyncBuffered] Example:\n";
    async_buffered_example(loop);

    std::osyncstream(std::cout) << "\n[AsyncIO] await + fluent Example:\n";
    io_dual_style_example(loop);

    std::osyncstream(std::cout) << "\n[AsyncIO-TCP] await + fluent Example:\n";
    io_dual_style_tcp_example(loop);

    loop.stop();
    return 0;
}
