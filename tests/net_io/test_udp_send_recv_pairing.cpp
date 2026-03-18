import net_io;
import net_io.udp_transport;

#include <gtest/gtest.h>
#include <thread>
#include <chrono>

using namespace std::chrono_literals;
using namespace net_io;

// Regression test: ensure pairing of send/recv for connected vs unconnected sockets
TEST(UdpSendRecvPairing, ConnectedClientUsesRecvAndServerUsesRecvfrom)
{
    // allocate ephemeral port using helper
    uint16_t port = net_io::get_free_udp_port();
    ASSERT_NE(port, 0u);

    // Server binds (unconnected) and uses recvfrom
    UdpEndpoint server_ep("127.0.0.1", port, true, port);
    UdpTransport server;
    server.open_bind(server_ep);

    // Client connects (connected socket) and uses send/recv
    UdpTransport client;
    UdpEndpoint client_ep("127.0.0.1", port);
    client.open_connect(client_ep);

    std::thread srv([&server] {
        char buf[128]{};
        sockaddr_storage from{}; socklen_t fromlen = sizeof(from);
        auto n = server.read(buf, sizeof(buf), &from, &fromlen);
        ASSERT_GT(n, 0u);
        std::string s(buf, buf + n);
        EXPECT_EQ(s, "pair-test");

        // reply using write_to (server must use sendto)
        server.write_to("reply", 5, from, fromlen);
    });

    // client send (connected) and receive (connected)
    client.write("pair-test", strlen("pair-test"));

    char rbuf[64]{};
    auto rn = client.read(rbuf, sizeof(rbuf));
    ASSERT_GT(rn, 0u);
    EXPECT_EQ(std::string(rbuf, rbuf + rn), "reply");

    srv.join();
    client.close(); server.close();
}
