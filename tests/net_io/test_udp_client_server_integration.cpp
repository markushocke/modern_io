import net_io;
import net_io.udp_transport;

#include <gtest/gtest.h>
#include <thread>
#include <chrono>

using namespace std::chrono_literals;
using namespace net_io;

TEST(UdpIntegration, ClientConnectWriteServerReceives)
{
    // Find an ephemeral UDP port using central helper
    uint16_t port = net_io::get_free_udp_port();

    ASSERT_NE(port, 0u);

    UdpEndpoint server_ep("127.0.0.1", port, true, port);

    UdpTransport server;
    server.open_bind(server_ep);

    // Server thread: receive
    std::thread srv([&server]() {
        char buf[128]{};
        sockaddr_storage from{};
        socklen_t fromlen = sizeof(from);
        auto n = server.read(buf, sizeof(buf), &from, &fromlen);
        // Expect the client message
        ASSERT_GT(n, 0u);
        std::string s(buf, buf + n);
        EXPECT_EQ(s, "hello-from-client");
    });

    // Client: connect and write (without prior read)
    UdpTransport client;
    UdpEndpoint client_ep("127.0.0.1", port);
    client.open_connect(client_ep);

    std::this_thread::sleep_for(50ms);
    client.write("hello-from-client", strlen("hello-from-client"));

    srv.join();

    client.close();
    server.close();
}
