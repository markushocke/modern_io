// test_base.cpp
import net_io;
import net_io_base;
#include <gtest/gtest.h>
#include <string>
#include "tests/test_net_helpers.hpp"

TEST(NetIOBaseTest, SocketExceptionBasic) {
    modern::net::SocketException ex("Test error", 42);
    
    EXPECT_EQ(ex.error_code(), 42);
    EXPECT_FALSE(ex.peer().has_value());
    EXPECT_NE(std::string(ex.what()).find("Test error"), std::string::npos);
}

TEST(NetIOBaseTest, SocketExceptionWithPeer) {
    modern::net::SocketException ex("Connection failed", 111, "192.168.1.1:8080");
    
    EXPECT_EQ(ex.error_code(), 111);
    EXPECT_TRUE(ex.peer().has_value());
    EXPECT_EQ(ex.peer().value(), "192.168.1.1:8080");
}

TEST(NetIOBaseTest, SocketTypeDefinition) {
    // Just verify that sock_t is defined
#ifdef _WIN32
    static_assert(std::is_same_v<modern::net::sock_t, SOCKET>, "sock_t should be SOCKET on Windows");
#else
    static_assert(std::is_same_v<modern::net::sock_t, int>, "sock_t should be int on POSIX");
#endif
    static_assert(std::is_same_v<modern::net::sock_t, net_io::sock_t>, "Legacy sock_t alias should remain compatible");
}

TEST(NetIOBaseTest, InvalidSocketConstant) {
    // Verify invalid_socket is properly defined
#ifdef _WIN32
    EXPECT_EQ(modern::net::invalid_socket, INVALID_SOCKET);
#else
    EXPECT_EQ(modern::net::invalid_socket, -1);
#endif
    EXPECT_EQ(modern::net::invalid_socket, net_io::invalid_socket);
}

// On Windows the helper ensures WSA is initialized; still verify calling ensure_wsa is safe
#ifdef _WIN32
TEST(NetIOBaseTest, WSAInitialization) {
    test_helpers::NetInit _netinit;
    EXPECT_NO_THROW(detail::ensure_wsa());
    EXPECT_NO_THROW(detail::ensure_wsa());
    EXPECT_NO_THROW(detail::ensure_wsa());
}
#endif

TEST(NetIOBaseTest, ExceptionInheritance) {
    modern::net::SocketException ex("test", 0);
    
    // Should be catchable as std::runtime_error
    try {
        throw ex;
    } catch (const std::runtime_error& e) {
        EXPECT_NE(std::string(e.what()).find("test"), std::string::npos);
    }
}

TEST(NetIOBaseTest, ExceptionCopyAndMove) {
    modern::net::SocketException ex1("Original", 100, "localhost:9000");
    
    // Copy
    modern::net::SocketException ex2 = ex1;
    EXPECT_EQ(ex2.error_code(), 100);
    EXPECT_EQ(ex2.peer().value(), "localhost:9000");
    
    // Move
    modern::net::SocketException ex3 = std::move(ex1);
    EXPECT_EQ(ex3.error_code(), 100);
    EXPECT_EQ(ex3.peer().value(), "localhost:9000");
}
