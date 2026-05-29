// test_tcp_client.cpp
import net_io;
import net_io.tcp_client;
import net_io.tcp_endpoint;
import net_io.tcp_server;
#include <gtest/gtest.h>
#include <thread>
#include <chrono>
#include <cstring>
#include "tests/test_net_helpers.hpp"

using namespace net_io;

TEST(TcpClientTest, ConnectAndExchange) {
    // Start a simple server socket using TcpServer to accept one connection
    uint16_t port = [](){
        auto s = ::socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in a{}; a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(INADDR_LOOPBACK); a.sin_port = 0;
        ::bind(s, reinterpret_cast<sockaddr*>(&a), sizeof(a));
        socklen_t len = sizeof(a); ::getsockname(s, reinterpret_cast<sockaddr*>(&a), &len); uint16_t p = ntohs(a.sin_port);
#ifdef _WIN32
        closesocket(s);
#else
        ::close(s);
#endif
        return p;
    }();
    ASSERT_NE(port, 0u);

    TcpEndpoint ep("127.0.0.1", port);
    TcpServer server(ep);
    server.start();

    std::thread srv([&server](){
        try {
            auto conn = server.accept();
            char buf[16]; auto n = conn.read(buf, 5);
            conn.write(buf, n);
            conn.close();
        } catch (...) {}
    });

    modern::net::TcpClient client(modern::net::TcpEndpoint("127.0.0.1", port));
    client.open();
    const char* send = "abcde";
    client.write(send, 5);
    char recv[16]; auto rn = client.read(recv, 5);
    EXPECT_EQ(rn, 5u);
    EXPECT_EQ(std::string(recv, rn), "abcde");
    client.close();

    server.stop();
    if (srv.joinable()) srv.join();
}

static_assert(std::same_as<modern::net::TcpClient, net_io::TcpClient>);
