// test_tcp_server.cpp
import net_io;
import net_io.tcp_server;
#include <gtest/gtest.h>

using namespace net_io;

// TCP server tests: start server on ephemeral port, accept a connection and echo data.
#include <thread>
#include <chrono>
#include <cstring>

using namespace net_io;

// Use portable helper from net_io_base

#include "tests/test_net_helpers.hpp"

TEST(TcpServerTest, EchoSingleClient) {
    uint16_t port = net_io::get_free_tcp_port();
    ASSERT_NE(port, 0u);

    TcpEndpoint ep("127.0.0.1", port);
    TcpServer server(ep);
    server.start();

    std::thread server_thread([&server]() {
        try {
            auto conn = server.accept();
            char buf[128];
            std::size_t n = conn.read(buf, sizeof(buf));
            // echo
            conn.write(buf, n);
            conn.close();
        } catch (...) {
            // swallow for test
        }
    });

    TcpClient client(TcpEndpoint("127.0.0.1", port));
    client.open();
    const char* msg = "hello";
    client.write(msg, 5);
    char rbuf[128];
    std::size_t rn = client.read(rbuf, sizeof(rbuf));
    EXPECT_EQ(rn, 5u);
    EXPECT_EQ(std::string(rbuf, rn), "hello");
    client.close();

    server.stop();
    if (server_thread.joinable()) server_thread.join();
}
