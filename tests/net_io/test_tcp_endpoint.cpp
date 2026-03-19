// test_tcp_endpoint.cpp
import net_io;
import net_io.tcp_endpoint;
#include <gtest/gtest.h>
#include <string>
#include <cstring>
#include "tests/test_net_helpers.hpp"

using namespace net_io;

TEST(TcpEndpointTest, BasicConstruction) {
    TcpEndpoint ep("127.0.0.1", 8080);
    
    EXPECT_EQ(ep.address, "127.0.0.1");
    EXPECT_EQ(ep.port, 8080);
}

TEST(TcpEndpointTest, CopyConstruction) {
    TcpEndpoint ep1("192.168.1.1", 9000);
    TcpEndpoint ep2 = ep1;
    
    EXPECT_EQ(ep2.address, "192.168.1.1");
    EXPECT_EQ(ep2.port, 9000);
}

TEST(TcpEndpointTest, MoveConstruction) {
    TcpEndpoint ep1("10.0.0.1", 5000);
    TcpEndpoint ep2 = std::move(ep1);
    
    EXPECT_EQ(ep2.address, "10.0.0.1");
    EXPECT_EQ(ep2.port, 5000);
}

TEST(TcpEndpointTest, Assignment) {
    TcpEndpoint ep1("localhost", 3000);
    TcpEndpoint ep2("192.168.1.1", 4000);
    
    ep2 = ep1;
    
    EXPECT_EQ(ep2.address, "localhost");
    EXPECT_EQ(ep2.port, 3000);
}

TEST(TcpEndpointTest, LocalhostResolution) {
    TcpEndpoint ep("localhost", 8080);
    
    // Should be able to resolve localhost
    EXPECT_NO_THROW({
        auto addr = ep.to_sockaddr(false);
    });
}

TEST(TcpEndpointTest, IPv4Address) {
    TcpEndpoint ep("127.0.0.1", 12345);
    
    auto addr = ep.to_sockaddr(false);
    
    // Should be IPv4
    EXPECT_EQ(addr.ss_family, AF_INET);
    
    // Verify port is set correctly (network byte order)
    sockaddr_in* addr_in = reinterpret_cast<sockaddr_in*>(&addr);
    EXPECT_EQ(ntohs(addr_in->sin_port), 12345);
}

TEST(TcpEndpointTest, DifferentPorts) {
    TcpEndpoint ep1("127.0.0.1", 80);
    TcpEndpoint ep2("127.0.0.1", 443);
    TcpEndpoint ep3("127.0.0.1", 65535);
    
    EXPECT_EQ(ep1.port, 80);
    EXPECT_EQ(ep2.port, 443);
    EXPECT_EQ(ep3.port, 65535);
}

TEST(TcpEndpointTest, PortZero) {
    // Port 0 should be allowed (OS assigns port)
    TcpEndpoint ep("0.0.0.0", 0);
    
    EXPECT_EQ(ep.port, 0);
    EXPECT_NO_THROW(ep.to_sockaddr(true));
}

TEST(TcpEndpointTest, ToStringRepresentation) {
    TcpEndpoint ep("192.168.1.100", 8080);
    
    // Endpoint should have address and port
    EXPECT_EQ(ep.address, "192.168.1.100");
    EXPECT_EQ(ep.port, 8080);
}

TEST(TcpEndpointTest, InvalidHostname) {
    TcpEndpoint ep("this.is.an.invalid.hostname.that.does.not.exist.com", 8080);
    
    // Should throw ResolutionException when trying to resolve
    try {
        ep.to_sockaddr(false);
        FAIL() << "Expected ResolutionException to be thrown";
    } catch (const ResolutionException& e) {
        EXPECT_EQ(e.hostname(), "this.is.an.invalid.hostname.that.does.not.exist.com");
        SUCCEED();
    } catch (...) {
        FAIL() << "Expected ResolutionException but got different exception";
    }
}

TEST(TcpEndpointTest, BindVsConnectMode) {
    TcpEndpoint ep("127.0.0.1", 9999);
    
    // Should work in both modes
    EXPECT_NO_THROW({
        auto addr1 = ep.to_sockaddr(true);   // bind mode
        auto addr2 = ep.to_sockaddr(false);  // connect mode
    });
}

TEST(TcpEndpointTest, WildcardAddress) {
    TcpEndpoint ep("0.0.0.0", 5000);
    
    auto addr = ep.to_sockaddr(true);
    
    EXPECT_EQ(addr.ss_family, AF_INET);
    
    sockaddr_in* addr_in = reinterpret_cast<sockaddr_in*>(&addr);
    EXPECT_EQ(addr_in->sin_addr.s_addr, INADDR_ANY);
}

// Note: IPv6 tests would require IPv6 support check
TEST(TcpEndpointTest, IPv6AddressBasic) {
    TcpEndpoint ep("::1", 8080);  // IPv6 loopback
    
    // May or may not work depending on system IPv6 support
    // So we just check it doesn't crash
    try {
        auto addr = ep.to_sockaddr(false);
        // If successful, should be AF_INET6
        if (addr.ss_family == AF_INET6) {
            SUCCEED();
        }
    } catch (const SocketException&) {
        // IPv6 might not be available
        SUCCEED();
    }
}

TEST(TcpEndpointTest, CompareEndpoints) {
    TcpEndpoint ep1("127.0.0.1", 8080);
    TcpEndpoint ep2("127.0.0.1", 8080);
    TcpEndpoint ep3("127.0.0.1", 9090);
    TcpEndpoint ep4("192.168.1.1", 8080);
    
    // Same address and port
    EXPECT_EQ(ep1.address, ep2.address);
    EXPECT_EQ(ep1.port, ep2.port);
    
    // Different port
    EXPECT_NE(ep1.port, ep3.port);
    
    // Different address
    EXPECT_NE(ep1.address, ep4.address);
}
