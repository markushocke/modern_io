#include "tests/test_net_async_helpers.hpp"
#include <gtest/gtest.h>
#include <coroutine>
#include <future>
#include <span>
#include <array>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
import net_io.async_udp_socket;
import net_io.udp_endpoint;
import net_io.event_loop;
#include <string>

import net_io.async_utils;

// Use net_io::Task as the coroutine-based helper and provide sync wrappers
using net_io::Task;

template<typename Awaitable>
auto sync_run_timeout(Awaitable a, int ms) {
    auto runner = test_helpers::run_awaitable(std::move(a));
    auto fut = std::async(std::launch::async, [h=std::move(runner)]() mutable {
        return h.get();
    });
    if (fut.wait_for(std::chrono::milliseconds(ms)) == std::future_status::ready) return fut.get();
    throw std::runtime_error("timeout");
}

TEST(AsyncUdpTest, DatagramRoundtrip) {
    test_helpers::NetInit netinit;
    net_io::EventLoop::instance().start();

    // Server socket
    net_io::AsyncUdpSocket server;
    uint16_t port = net_io::get_free_udp_port();
    // Make endpoint with bind_local and local_port so to_sockaddr(true) uses our chosen port
    net_io::UdpEndpoint server_ep("127.0.0.1", port, true, port);
    auto sa = server_ep.to_sockaddr(true);
    socklen_t salen = sizeof(sa);
    auto sres = server.bind(sa, salen);
    ASSERT_TRUE((bool)sres);

    // Client socket (unnconnected)
    net_io::AsyncUdpSocket client;
    auto cres = client.ensure_socket();
    ASSERT_TRUE((bool)cres);

    // Client sends datagram to server
    const char* msg = "udp-hello";
    // sendto low-level: use platform socket to simplify (since client not connected)
    int fd = client.native_handle();
    sockaddr_in dst{}; dst.sin_family = AF_INET; dst.sin_port = htons(port); inet_pton(AF_INET, "127.0.0.1", &dst.sin_addr);
    ssize_t sent = ::sendto(fd, msg, strlen(msg), 0, reinterpret_cast<sockaddr*>(&dst), sizeof(dst));
    ASSERT_EQ(sent, (ssize_t)strlen(msg));

    // Server async read
    std::array<char, 64> buf{};
    auto rres = sync_run_timeout(server.async_read(std::span<char>(buf.data(), buf.size())), 200000);
    ASSERT_TRUE((bool)rres);
    auto rlen = rres.value();
    ASSERT_EQ((size_t)rlen, strlen(msg));
    ASSERT_EQ(std::string(buf.data(), (size_t)rlen), std::string(msg));

    // Echo back (use peer() and peer_length())
    ASSERT_TRUE(server.has_peer());
    const auto& peer_ss = server.peer();
    socklen_t peer_len = server.peer_length();
    int sfd = server.native_handle();
    ssize_t sents = ::sendto(sfd, msg, strlen(msg), 0, reinterpret_cast<const sockaddr*>(&peer_ss), peer_len);
    ASSERT_EQ(sents, (ssize_t)strlen(msg));

    // Client recv
    char rbuf[128];
    int rn = ::recv(fd, rbuf, sizeof(rbuf), 0);
    ASSERT_EQ(rn, (int)strlen(msg));
    ASSERT_EQ(std::string(rbuf, rn), std::string(msg));

#if defined(_WIN32)
    ::closesocket(fd);
#else
    ::close(fd);
#endif
    server.close();
    net_io::EventLoop::instance().stop();
}
