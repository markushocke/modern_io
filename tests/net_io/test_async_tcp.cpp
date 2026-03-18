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
