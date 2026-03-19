// test_udp_transport.cpp
import net_io;
import net_io.udp_transport;
import net_io.udp_endpoint;
#include <gtest/gtest.h>
#include <thread>
#include <cstring>
#include "tests/test_net_helpers.hpp"

using namespace net_io;

TEST(UdpTransportTest, SendReceiveConnected) {
    // Find ephemeral port
    uint16_t port = [](){
        auto s = ::socket(AF_INET, SOCK_DGRAM, 0);
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

    UdpEndpoint server_ep("127.0.0.1", port, true, port);
    UdpTransport server;
    server.open_bind(server_ep);

    UdpEndpoint client_ep("127.0.0.1", port);
    UdpTransport client;
    client.open_connect(client_ep);

    const char* msg = "ping";
    client.write(msg, 4);

    char buf[16]; sockaddr_storage from; socklen_t fromlen;
    auto n = server.read(buf, sizeof(buf), &from, &fromlen);
    EXPECT_EQ(n, 4u);
    EXPECT_EQ(std::string(buf, n), "ping");

    // echo back
    server.write_to(buf, n, from, fromlen);

    char rbuf[16]; auto rn = client.read(rbuf, sizeof(rbuf));
    EXPECT_EQ(rn, 4u);
    EXPECT_EQ(std::string(rbuf, rn), "ping");

    client.close(); server.close();
}
