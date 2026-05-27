#include "tests/test_net_async_helpers.hpp"
#include <gtest/gtest.h>
#include <coroutine>
#include <expected>
#include <system_error>
#include <future>
#include <span>
#include <array>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <thread>
#include <string>
#include <chrono>
#include <memory_resource>

import modern_io.connection_arena;
import net_io.async_tcp_server;
import net_io.async_tcp_socket;
import net_io.tcp_endpoint;
import net_io.event_loop;
import net_io.async_utils;
import net_io.async_stream_adapters;

// Use net_io::Task as the coroutine-based helper
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
auto sync_run_timeout(Awaitable awaitable, int ms) {
    auto runner = test_helpers::run_awaitable(std::move(awaitable));
    auto fut = std::async(std::launch::async, [runner = std::move(runner)]() mutable {
        return runner.get();
    });
    if (fut.wait_for(std::chrono::milliseconds(ms)) == std::future_status::ready) {
        return fut.get();
    }
    throw std::runtime_error("timeout");
}

static Task<void> accept_and_echo_once(net_io::AsyncTcpServer& server) {
    auto asp = co_await server.accept();
    EXPECT_TRUE((bool)asp);
    auto server_sock = asp.value();

    char buf[5];

    net_io::AsyncTcpStreamAdapter stream(server_sock);
    auto r = co_await stream.read_exact_async(std::span<char>(buf, 5));
    EXPECT_TRUE((bool)r);
    EXPECT_EQ(std::string(buf, 5), "hello");

    auto w = co_await stream.write_all_async(std::span<const char>(buf, 5));
    EXPECT_TRUE((bool)w);
}

static Task<void> accept_and_echo_buffered_once(net_io::AsyncTcpServer& server) {
    auto asp = co_await server.accept();
    EXPECT_TRUE((bool)asp);
    auto server_sock = asp.value();
    EXPECT_NE(server_sock->connection_arena_handle(), nullptr);

    net_io::AsyncTcpStreamAdapter stream(server_sock);
    EXPECT_EQ(stream.connection_arena_handle().get(), server_sock->connection_arena_handle().get());
    auto input = stream.buffered_input<64>();
    auto output = stream.buffered_output<64>();

    char buf[5];
    std::size_t total = 0;
    while (total < sizeof(buf)) {
        auto r = co_await input.read_async(buf + total, sizeof(buf) - total);
        EXPECT_TRUE((bool)r);
        if (!r || r.value() == 0) {
            co_return;
        }
        total += r.value();
    }

    EXPECT_EQ(std::string(buf, sizeof(buf)), "hello");

    auto w = co_await output.write_async(buf, sizeof(buf));
    EXPECT_TRUE((bool)w);
    if (!w) {
        co_return;
    }
    EXPECT_EQ(w.value(), sizeof(buf));

    auto flush = co_await output.flush_async();
    EXPECT_TRUE((bool)flush);
}

static Task<void> accept_and_check_connection_arena_settings(
    net_io::AsyncTcpServer& server,
    std::size_t expected_initial_buffer_size) {
    auto asp = co_await server.accept();
    EXPECT_TRUE((bool)asp);
    if (!asp) {
        co_return;
    }

    auto server_sock = asp.value();
    EXPECT_NE(server_sock->connection_arena_handle(), nullptr);
    EXPECT_EQ(server_sock->connection_arena().initial_buffer_size(), expected_initial_buffer_size);

    net_io::AsyncTcpStreamAdapter stream(server_sock);
    EXPECT_EQ(stream.connection_arena_handle().get(), server_sock->connection_arena_handle().get());
}

TEST(AsyncTcpTest, AcceptAndEcho) {
    // Ensure network initialization on Windows
    test_helpers::NetInit netinit;

    // Start the EventLoop (background poller thread)
    net_io::EventLoop& loop = net_io::EventLoop::instance();
    loop.start();

    // Start the TCP server
    net_io::AsyncTcpServer server;
    uint16_t port = net_io::get_free_tcp_port();
    net_io::TcpEndpoint server_ep("127.0.0.1", port);
    auto start_res = server.start(server_ep, 1);
    ASSERT_TRUE((bool)start_res);

    auto task = accept_and_echo_once(server);

    // Create a blocking client and connect to trigger the server accept
    int c = ::socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in sa{};
    sa.sin_family = AF_INET;
    sa.sin_port = htons(port);
    inet_pton(AF_INET, "127.0.0.1", &sa.sin_addr);
    int rc = ::connect(c, reinterpret_cast<sockaddr*>(&sa), sizeof(sa));
    ASSERT_EQ(rc, 0);

    // Send the message to the server to drive the coroutine
    const char* msg = "hello";
    ::send(c, msg, 5, 0);

    task.get();


    // Receive echoed data from the server
    char rbuf[32];
    int rn = ::recv(c, rbuf, sizeof(rbuf), 0);
    ASSERT_EQ(rn, 5);
    ASSERT_EQ(std::string(rbuf, rn), "hello");

    // Close client socket
#if defined(_WIN32)
    ::closesocket(c);
#else
    ::close(c);
#endif

    // Stop the server and EventLoop
    server.stop();
    loop.stop();
}

TEST(AsyncTcpTest, AcceptAndEchoWithInjectedLoop) {
    test_helpers::NetInit netinit;

    net_io::EventLoop loop;
    loop.start();

    net_io::AsyncTcpServer server(loop);
    uint16_t port = net_io::get_free_tcp_port();
    net_io::TcpEndpoint server_ep("127.0.0.1", port);
    auto start_res = server.start(server_ep, 1);
    ASSERT_TRUE((bool)start_res);

    auto task = accept_and_echo_once(server);

    int c = ::socket(AF_INET, SOCK_STREAM, 0);
    ASSERT_GE(c, 0);

    sockaddr_in sa{};
    sa.sin_family = AF_INET;
    sa.sin_port = htons(port);
    inet_pton(AF_INET, "127.0.0.1", &sa.sin_addr);
    int rc = ::connect(c, reinterpret_cast<sockaddr*>(&sa), sizeof(sa));
    ASSERT_EQ(rc, 0);

    const char* msg = "hello";
    ::send(c, msg, 5, 0);

    task.get();

    char rbuf[32];
    int rn = ::recv(c, rbuf, sizeof(rbuf), 0);
    ASSERT_EQ(rn, 5);
    ASSERT_EQ(std::string(rbuf, rn), "hello");

#if defined(_WIN32)
    ::closesocket(c);
#else
    ::close(c);
#endif

    server.stop();
    loop.stop();
}

TEST(AsyncTcpTest, AsyncConnectWithInjectedLoop) {
    test_helpers::NetInit netinit;

    net_io::EventLoop loop;
    loop.start();

    int listener = ::socket(AF_INET, SOCK_STREAM, 0);
    ASSERT_GE(listener, 0);

    int reuse = 1;
    ASSERT_EQ(::setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)), 0);

    uint16_t port = net_io::get_free_tcp_port();
    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    inet_pton(AF_INET, "127.0.0.1", &server_addr.sin_addr);

    ASSERT_EQ(::bind(listener, reinterpret_cast<sockaddr*>(&server_addr), sizeof(server_addr)), 0);
    ASSERT_EQ(::listen(listener, 1), 0);

    auto accept_future = std::async(std::launch::async, [listener]() {
        sockaddr_in peer{};
        socklen_t peer_len = sizeof(peer);
        int accepted = ::accept(listener, reinterpret_cast<sockaddr*>(&peer), &peer_len);
        if (accepted >= 0) {
            ::close(accepted);
        }
        return accepted;
    });

    net_io::AsyncTcpSocket client(loop);
    sockaddr_storage target{};
    auto* target_in = reinterpret_cast<sockaddr_in*>(&target);
    target_in->sin_family = AF_INET;
    target_in->sin_port = htons(port);
    inet_pton(AF_INET, "127.0.0.1", &target_in->sin_addr);

    auto connect_res = sync_run_timeout(client.async_connect(target, sizeof(sockaddr_in)), 2000);
    ASSERT_TRUE((bool)connect_res);

    ASSERT_EQ(accept_future.wait_for(std::chrono::seconds(2)), std::future_status::ready);
    ASSERT_GE(accept_future.get(), 0);

    client.close();
    ::close(listener);
    loop.stop();
}

TEST(AsyncTcpTest, AcceptUsesConfiguredConnectionArenaSettings) {
    test_helpers::NetInit netinit;

    net_io::EventLoop loop;
    loop.start();

    CountingMemoryResource upstream;
    net_io::AsyncTcpServer server(loop, modern_io::ConnectionArenaSettings{256, &upstream});
    uint16_t port = net_io::get_free_tcp_port();
    net_io::TcpEndpoint server_ep("127.0.0.1", port);
    auto start_res = server.start(server_ep, 1);
    ASSERT_TRUE((bool)start_res);

    auto task = accept_and_check_connection_arena_settings(server, 256);

    int c = ::socket(AF_INET, SOCK_STREAM, 0);
    ASSERT_GE(c, 0);

    sockaddr_in sa{};
    sa.sin_family = AF_INET;
    sa.sin_port = htons(port);
    inet_pton(AF_INET, "127.0.0.1", &sa.sin_addr);
    int rc = ::connect(c, reinterpret_cast<sockaddr*>(&sa), sizeof(sa));
    ASSERT_EQ(rc, 0);

    task.get();

#if defined(_WIN32)
    ::closesocket(c);
#else
    ::close(c);
#endif

    server.stop();
    loop.stop();
}

TEST(AsyncTcpTest, BufferedAdapterUsesSharedConnectionArena) {
    test_helpers::NetInit netinit;

    net_io::EventLoop loop;
    loop.start();

    net_io::AsyncTcpServer server(loop);
    uint16_t port = net_io::get_free_tcp_port();
    net_io::TcpEndpoint server_ep("127.0.0.1", port);
    auto start_res = server.start(server_ep, 1);
    ASSERT_TRUE((bool)start_res);

    auto task = accept_and_echo_buffered_once(server);

    int c = ::socket(AF_INET, SOCK_STREAM, 0);
    ASSERT_GE(c, 0);

    sockaddr_in sa{};
    sa.sin_family = AF_INET;
    sa.sin_port = htons(port);
    inet_pton(AF_INET, "127.0.0.1", &sa.sin_addr);
    int rc = ::connect(c, reinterpret_cast<sockaddr*>(&sa), sizeof(sa));
    ASSERT_EQ(rc, 0);

    const char* msg = "hello";
    ::send(c, msg, 5, 0);

    task.get();

    char rbuf[32];
    int rn = ::recv(c, rbuf, sizeof(rbuf), 0);
    ASSERT_EQ(rn, 5);
    ASSERT_EQ(std::string(rbuf, rn), "hello");

#if defined(_WIN32)
    ::closesocket(c);
#else
    ::close(c);
#endif

    server.stop();
    loop.stop();
}

#ifndef _WIN32
TEST(AsyncTcpTest, StopClearsPendingAcceptRegistration) {
    test_helpers::NetInit netinit;

    net_io::EventLoop& loop = net_io::EventLoop::instance();
    loop.start();

    net_io::AsyncTcpServer first_server;
    uint16_t first_port = net_io::get_free_tcp_port();
    auto first_start = first_server.start(net_io::TcpEndpoint("127.0.0.1", first_port), 1);
    ASSERT_TRUE((bool)first_start);

    const int stale_fd = first_server.native_handle();
    {
        auto pending_accept = first_server.accept();
        pending_accept.native_handle().resume();
        ASSERT_FALSE(pending_accept.native_handle().done());
        first_server.stop();
    }

    bool reused_fd = false;
    for (int attempt = 0; attempt < 16 && !reused_fd; ++attempt) {
        net_io::AsyncTcpServer second_server;
        uint16_t second_port = net_io::get_free_tcp_port();
        auto second_start = second_server.start(net_io::TcpEndpoint("127.0.0.1", second_port), 1);
        ASSERT_TRUE((bool)second_start);

        if (second_server.native_handle() != stale_fd) {
            second_server.stop();
            continue;
        }

        reused_fd = true;
        auto task = accept_and_echo_once(second_server);

        int c = ::socket(AF_INET, SOCK_STREAM, 0);
        ASSERT_GE(c, 0);

        sockaddr_in sa{};
        sa.sin_family = AF_INET;
        sa.sin_port = htons(second_port);
        inet_pton(AF_INET, "127.0.0.1", &sa.sin_addr);

        int rc = ::connect(c, reinterpret_cast<sockaddr*>(&sa), sizeof(sa));
        ASSERT_EQ(rc, 0);

        const char* msg = "hello";
        ::send(c, msg, 5, 0);

        task.get();

        char rbuf[32];
        int rn = ::recv(c, rbuf, sizeof(rbuf), 0);
        ASSERT_EQ(rn, 5);
        ASSERT_EQ(std::string(rbuf, rn), "hello");

        ::close(c);
        second_server.stop();
    }

    EXPECT_TRUE(reused_fd) << "expected the OS to reuse the closed listening fd during regression test";
    loop.stop();
}
#endif
