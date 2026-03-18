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

import net_io.async_tcp_server;
import net_io.async_tcp_socket;
import net_io.tcp_endpoint;
import net_io.event_loop;
import net_io.async_utils;
import net_io.async_stream_adapters;

// Use net_io::Task as the coroutine-based helper
using net_io::Task;

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
