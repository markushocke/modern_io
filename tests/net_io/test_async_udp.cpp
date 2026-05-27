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
#include <memory_resource>
import net_io.async_udp_socket;
import net_io.async_stream_adapters;
import net_io.udp_endpoint;
import net_io.event_loop;
import modern_io.connection_arena;
#include <string>

import net_io.async_utils;

// Use net_io::Task as the coroutine-based helper and provide sync wrappers
using net_io::Task;

class CountingMemoryResource : public std::pmr::memory_resource {
public:
    std::size_t allocation_count = 0;
    std::size_t deallocation_count = 0;

private:
    void* do_allocate(std::size_t bytes, std::size_t alignment) override {
        ++allocation_count;
        return std::pmr::new_delete_resource()->allocate(bytes, alignment);
    }

    void do_deallocate(void* p, std::size_t bytes, std::size_t alignment) override {
        ++deallocation_count;
        std::pmr::new_delete_resource()->deallocate(p, bytes, alignment);
    }

    bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override {
        return this == &other;
    }
};

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

TEST(AsyncUdpTest, DatagramRoundtripWithInjectedLoop) {
    test_helpers::NetInit netinit;

    net_io::EventLoop loop;
    loop.start();

    net_io::AsyncUdpSocket server(loop);
    uint16_t port = net_io::get_free_udp_port();
    net_io::UdpEndpoint server_ep("127.0.0.1", port, true, port);
    auto sa = server_ep.to_sockaddr(true);
    socklen_t salen = sizeof(sa);
    auto sres = server.bind(sa, salen);
    ASSERT_TRUE((bool)sres);

    net_io::AsyncUdpSocket client(loop);

    sockaddr_storage srvss{};
    auto* sin = reinterpret_cast<sockaddr_in*>(&srvss);
    sin->sin_family = AF_INET;
    sin->sin_port = htons(port);
    inet_pton(AF_INET, "127.0.0.1", &sin->sin_addr);
    socklen_t slen = sizeof(sockaddr_in);

    auto connect_res = sync_run_timeout(client.async_connect(srvss, slen), 2000);
    ASSERT_TRUE((bool)connect_res);

    const char* msg = "udp-hello";
    auto wres = sync_run_timeout(client.async_write(std::span<const char>(msg, strlen(msg))), 2000);
    ASSERT_TRUE((bool)wres);
    ASSERT_EQ(wres.value(), strlen(msg));

    std::array<char, 64> server_buf{};
    auto server_read = sync_run_timeout(server.async_read(std::span<char>(server_buf.data(), server_buf.size())), 2000);
    ASSERT_TRUE((bool)server_read);
    ASSERT_EQ(server_read.value(), strlen(msg));
    ASSERT_EQ(std::string(server_buf.data(), server_read.value()), std::string(msg));

    auto server_write = sync_run_timeout(server.async_write(std::span<const char>(msg, strlen(msg))), 2000);
    ASSERT_TRUE((bool)server_write);
    ASSERT_EQ(server_write.value(), strlen(msg));

    std::array<char, 64> client_buf{};
    auto client_read = sync_run_timeout(client.async_read(std::span<char>(client_buf.data(), client_buf.size())), 2000);
    ASSERT_TRUE((bool)client_read);
    ASSERT_EQ(client_read.value(), strlen(msg));
    ASSERT_EQ(std::string(client_buf.data(), client_read.value()), std::string(msg));

    client.close();
    server.close();
    loop.stop();
}

TEST(AsyncUdpTest, BufferedAdapterUsesSharedConnectionArena) {
    test_helpers::NetInit netinit;

    net_io::EventLoop loop;
    loop.start();

    auto server = std::make_shared<net_io::AsyncUdpSocket>(loop);
    uint16_t port = net_io::get_free_udp_port();
    net_io::UdpEndpoint server_ep("127.0.0.1", port, true, port);
    auto sa = server_ep.to_sockaddr(true);
    socklen_t salen = sizeof(sa);
    auto sres = server->bind(sa, salen);
    ASSERT_TRUE((bool)sres);

    CountingMemoryResource upstream;
    auto client = std::make_shared<net_io::AsyncUdpSocket>(
        loop,
        modern_io::ConnectionArenaSettings{256, &upstream});
    EXPECT_EQ(client->connection_arena().initial_buffer_size(), 256u);
    net_io::AsyncUdpStreamAdapter client_stream(client);
    EXPECT_EQ(client_stream.connection_arena_handle().get(), client->connection_arena_handle().get());

    sockaddr_storage srvss{};
    auto* sin = reinterpret_cast<sockaddr_in*>(&srvss);
    sin->sin_family = AF_INET;
    sin->sin_port = htons(port);
    inet_pton(AF_INET, "127.0.0.1", &sin->sin_addr);
    socklen_t slen = sizeof(sockaddr_in);

    auto connect_res = sync_run_timeout(client_stream.connect_async(srvss, slen), 2000);
    ASSERT_TRUE((bool)connect_res);

    auto client_output = client_stream.buffered_output<64>();
    auto client_input = client_stream.buffered_input<64>();

    const char* msg = "udp-hello";
    auto write_res = sync_run_timeout(client_output.write_async(msg, strlen(msg)), 2000);
    ASSERT_TRUE((bool)write_res);
    ASSERT_EQ(write_res.value(), strlen(msg));

    auto flush_res = sync_run_timeout(client_output.flush_async(), 2000);
    ASSERT_TRUE((bool)flush_res);

    std::array<char, 64> server_buf{};
    auto server_read = sync_run_timeout(server->async_read(std::span<char>(server_buf.data(), server_buf.size())), 2000);
    ASSERT_TRUE((bool)server_read);
    ASSERT_EQ(server_read.value(), strlen(msg));
    ASSERT_EQ(std::string(server_buf.data(), server_read.value()), std::string(msg));

    ASSERT_TRUE(server->has_peer());
    auto server_write = sync_run_timeout(server->async_write(std::span<const char>(msg, strlen(msg))), 2000);
    ASSERT_TRUE((bool)server_write);
    ASSERT_EQ(server_write.value(), strlen(msg));

    std::array<char, 64> client_buf{};
    auto client_read = sync_run_timeout(client_input.read_async(client_buf.data(), strlen(msg)), 2000);
    ASSERT_TRUE((bool)client_read);
    ASSERT_EQ(client_read.value(), strlen(msg));
    ASSERT_EQ(std::string(client_buf.data(), client_read.value()), std::string(msg));
    EXPECT_EQ(upstream.allocation_count, 0u);

    client->close();
    server->close();
    loop.stop();
}
