import net_io;
import net_io.udp_transport;

#include <gtest/gtest.h>
#include <thread>
#include <chrono>
#include "tests/test_net_helpers.hpp"

using namespace std::chrono_literals;
using namespace net_io;

TEST(UdpPingPongIntegration, PingPong)
{
    test_helpers::NetInit _netinit;
    uint16_t port = net_io::get_free_udp_port();
    ASSERT_NE(port, 0u);

    UdpEndpoint server_ep("127.0.0.1", port, true, port);
    UdpTransport server;
    server.open_bind(server_ep);

    // Server thread: receive and reply
    std::thread srv([&server]() {
        char buf[128]{};
        sockaddr_storage from{};
        socklen_t fromlen = sizeof(from);
        auto n = server.read(buf, sizeof(buf), &from, &fromlen);
        ASSERT_GT(n, 0u);
        std::string s(buf, buf + n);
        EXPECT_EQ(s, "UDP-PING");

        server.write_to("UDP-PONG", strlen("UDP-PONG"), from, fromlen);
    });

    // Client: connect and send
    UdpEndpoint client_ep("127.0.0.1", port);
    UdpTransport client;
    client.open_connect(client_ep);

    std::this_thread::sleep_for(10ms);
    client.write("UDP-PING", strlen("UDP-PING"));

    char rbuf[128]{};
    auto rn = client.read(rbuf, sizeof(rbuf));
    ASSERT_GT(rn, 0u);
    std::string resp(rbuf, rbuf + rn);
    EXPECT_EQ(resp, "UDP-PONG");

    srv.join();

    client.close();
    server.close();
}
