#include "tests/test_net_async_helpers.hpp"
#include <gtest/gtest.h>
#include <coroutine>
#include <future>
#include <span>
#include <array>
#include <thread>
#include <vector>
#include <chrono>

import net_io.async_udp_socket;
import net_io.udp_endpoint;
import net_io.event_loop;

using namespace std::chrono_literals;

import net_io.async_utils;

// Use net_io::Task as the coroutine-based helper and provide sync wrappers
using net_io::Task;

template<typename Awaitable>
auto sync_run_timeout(Awaitable a, int ms) {
    auto runner = test_helpers::run_awaitable(std::move(a));
    // runner.get() blocks; emulate timeout by running in std::future
    auto fut = std::async(std::launch::async, [h=std::move(runner)]() mutable {
        return h.get();
    });
    if (fut.wait_for(std::chrono::milliseconds(ms)) == std::future_status::ready) {
        return fut.get();
    }
    throw std::runtime_error("timeout");
}

TEST(AsyncUdpMulti, MultipleClientsServerReceives)
{
    test_helpers::NetInit netinit;
    net_io::EventLoop::instance().start();

    net_io::AsyncUdpSocket server;
    uint16_t port = net_io::get_free_udp_port();
    net_io::UdpEndpoint server_ep("127.0.0.1", port, true, port);
    auto sa = server_ep.to_sockaddr(true);
    socklen_t salen = sizeof(sa);
    auto sres = server.bind(sa, salen);
    ASSERT_TRUE((bool)sres);
    // Debug: print server fd and bound port
    {
        int sfd = server.native_handle();
        sockaddr_storage las{}; socklen_t llen = sizeof(las);
        if (getsockname(sfd, reinterpret_cast<sockaddr*>(&las), &llen) == 0) {
            if (las.ss_family == AF_INET) {
                const sockaddr_in* sin = reinterpret_cast<const sockaddr_in*>(&las);
                char ip[INET_ADDRSTRLEN]{}; inet_ntop(AF_INET, &sin->sin_addr, ip, INET_ADDRSTRLEN);
                std::cerr << "[TEST DEBUG] server fd=" << sfd << " bound to " << ip << ":" << ntohs(sin->sin_port) << "\n";
            }
        }
    }

    // Start N clients; each sends a unique message and expects an echo
    const int N = 4;
    std::vector<std::thread> client_threads;
    std::vector<std::string> msgs;
    for (int i = 0; i < N; ++i) {
        msgs.push_back("msg-" + std::to_string(i));
    }

    // Server reader thread: loop N receives
    std::exception_ptr srv_exc = nullptr;
    std::thread srv([&server, N, &msgs, &srv_exc]() {
        try {
            for (int i = 0; i < N; ++i) {
                std::array<char, 128> buf{};
                // robust read: retry until we get non-zero length or timeout
                auto start = std::chrono::steady_clock::now();
                std::size_t rlen = 0;
                std::string received;
                while (true) {
                    auto rres = sync_run_timeout(server.async_read(std::span<char>(buf.data(), buf.size())), 500);
                    if (!rres) throw std::runtime_error("server read error");
                    rlen = rres.value();
                    if (rlen > 0) {
                        received.assign(buf.data(), rlen);
                        break;
                    }
                    auto now = std::chrono::steady_clock::now();
                    if (std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count() >= 2000) {
                        throw std::runtime_error("timeout waiting for non-zero server read");
                    }
                    // small backoff
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                }
                std::cerr << "[TEST DEBUG] server received len=" << rlen << " data='" << received << "'\n";
                // find matching message in msgs
                bool found = false;
                for (auto &m : msgs) if (m == received) { found = true; break; }
                EXPECT_TRUE(found);

                // echo back using stored peer
                ASSERT_TRUE(server.has_peer());
                const auto &peer = server.peer();
                socklen_t plen = server.peer_length();
                int sfd = server.native_handle();
                ssize_t sents = ::sendto(sfd, received.c_str(), received.size(), 0,
                                         reinterpret_cast<const sockaddr*>(&peer), plen);
                if (std::getenv("NET_IO_DEBUG")) {
                    if (peer.ss_family == AF_INET) {
                        const sockaddr_in* psin = reinterpret_cast<const sockaddr_in*>(&peer);
                        char pip[INET_ADDRSTRLEN]{}; inet_ntop(AF_INET, &psin->sin_addr, pip, INET_ADDRSTRLEN);
                        std::cerr << "[TEST DEBUG] server sent " << sents << " bytes to " << pip << ":" << ntohs(psin->sin_port) << "\n";
                    } else {
                        std::cerr << "[TEST DEBUG] server sent " << sents << " bytes (family=" << peer.ss_family << ")\n";
                    }
                }
            }
        } catch (...) {
            srv_exc = std::current_exception();
        }
    });
    // Give server thread a moment to register its async_read handlers
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // client threads: each create AsyncUdpSocket (unconnected) and send then receive
    std::vector<std::exception_ptr> client_excs(N);
    for (int i = 0; i < N; ++i) {
        client_threads.emplace_back([i, port, msg=msgs[i], &client_excs]() {
            try {
                net_io::AsyncUdpSocket client;
                auto cres = client.ensure_socket();
                ASSERT_TRUE((bool)cres);

                // build sockaddr_storage for server (IPv4)
                sockaddr_storage srvss{};
                sockaddr_in *sin = reinterpret_cast<sockaddr_in*>(&srvss);
                sin->sin_family = AF_INET;
                sin->sin_port = htons(port);
                inet_pton(AF_INET, "127.0.0.1", &sin->sin_addr);
                socklen_t slen = sizeof(sockaddr_in);

                // connect using async_connect
                auto cres2 = sync_run_timeout(client.async_connect(srvss, slen), 2000);
                if (!cres2) throw std::runtime_error("client connect failed");

                // write via async_write
                auto wres = sync_run_timeout(client.async_write(std::span<const char>(msg.data(), msg.size())), 2000);
                if (!wres) throw std::runtime_error("client write failed");

                // read echo
                std::array<char, 128> rbuf{};
                auto rres = sync_run_timeout(client.async_read(std::span<char>(rbuf.data(), rbuf.size())), 2000);
                if (!rres) throw std::runtime_error("client read failed");
                auto rn = rres.value();
                std::string resp(rbuf.data(), (size_t)rn);
                EXPECT_EQ(resp, msg);
                client.close();
            } catch (...) {
                client_excs[i] = std::current_exception();
            }
        });
    }

    for (auto &t : client_threads) t.join();
    srv.join();

    if (srv_exc) std::rethrow_exception(srv_exc);
    for (auto &e : client_excs) if (e) std::rethrow_exception(e);

    server.close();
    net_io::EventLoop::instance().stop();
}
