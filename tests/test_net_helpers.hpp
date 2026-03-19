#pragma once

// tests/test_net_helpers.hpp
// Centralized test helpers for networking tests.

// Import the net_io base module which provides detail::ensure_wsa() on Windows.
import net_io_base;

// Provide a tiny RAII helper that ensures platform socket subsystems are initialized.
namespace test_helpers {

struct NetInit {
    NetInit() {
#if defined(_WIN32)
        detail::ensure_wsa();
#endif
    }
};

} // namespace test_helpers

// Platform socket headers for tests that need sockaddr, sockaddr_storage, etc.
#if defined(_WIN32)
    // Include winsock before windows.h usage and related headers
    #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
    #endif
    #include <winsock2.h>
    #include <ws2tcpip.h>
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
#endif
