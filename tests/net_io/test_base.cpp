// test_base.cpp
import net_io;
import net_io_base;
#include <gtest/gtest.h>
#include <string>
#include "tests/test_net_helpers.hpp"

using namespace net_io;

TEST(NetIOBaseTest, SocketExceptionBasic) {
    SocketException ex("Test error", 42);
    
    EXPECT_EQ(ex.error_code(), 42);
    EXPECT_FALSE(ex.peer().has_value());
    EXPECT_NE(std::string(ex.what()).find("Test error"), std::string::npos);
}

TEST(NetIOBaseTest, SocketExceptionWithPeer) {
    SocketException ex("Connection failed", 111, "192.168.1.1:8080");
    
    EXPECT_EQ(ex.error_code(), 111);
    EXPECT_TRUE(ex.peer().has_value());
    EXPECT_EQ(ex.peer().value(), "192.168.1.1:8080");
}

TEST(NetIOBaseTest, SocketTypeDefinition) {
    // Just verify that sock_t is defined
#ifdef _WIN32
    static_assert(std::is_same_v<sock_t, SOCKET>, "sock_t should be SOCKET on Windows");
#else
    static_assert(std::is_same_v<sock_t, int>, "sock_t should be int on POSIX");
#endif
}

TEST(NetIOBaseTest, InvalidSocketConstant) {
    // Verify invalid_socket is properly defined
#ifdef _WIN32
    EXPECT_EQ(invalid_socket, INVALID_SOCKET);
#else
    EXPECT_EQ(invalid_socket, -1);
#endif
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
    SocketException ex("test", 0);
    
    // Should be catchable as std::runtime_error
    try {
        throw ex;
    } catch (const std::runtime_error& e) {
        EXPECT_NE(std::string(e.what()).find("test"), std::string::npos);
    }
}

TEST(NetIOBaseTest, ExceptionCopyAndMove) {
    SocketException ex1("Original", 100, "localhost:9000");
    
    // Copy
    SocketException ex2 = ex1;
    EXPECT_EQ(ex2.error_code(), 100);
    EXPECT_EQ(ex2.peer().value(), "localhost:9000");
    
    // Move
    SocketException ex3 = std::move(ex1);
    EXPECT_EQ(ex3.error_code(), 100);
    EXPECT_EQ(ex3.peer().value(), "localhost:9000");
}
