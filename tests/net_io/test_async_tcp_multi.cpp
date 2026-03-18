#include "tests/test_net_async_helpers.hpp"
#include <gtest/gtest.h>
#include <expected>
#include <system_error>
#include <coroutine>
#include <future>
#include <span>
#include <array>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <fcntl.h>
import net_io.async_tcp_server;
import net_io.async_tcp_socket;
import net_io.tcp_endpoint;
import net_io.event_loop;
#include <thread>
#include <vector>

import net_io.async_utils;
import net_io.async_stream_adapters;

using net_io::Task;
template<typename MakeAwaitable>
auto sync_run3(MakeAwaitable make_awaitable) {
    // Single-attempt synchronous runner: create a fresh awaitable and run it
    // to completion via IoTask. Keep the IoTask alive via shared_ptr to avoid
    // premature destruction when the caller runs the task in another thread.
    using Awaitable = decltype(make_awaitable());
    using R = decltype(std::declval<Awaitable>().await_resume());
    auto runner = test_helpers::run_awaitable(make_awaitable());
    auto sp = std::make_shared<decltype(runner)>(std::move(runner));
    return sp->get();
}

// ...existing code...

TEST(AsyncTcpMultiTest, AcceptMultipleClients) {
    test_helpers::NetInit netinit;
    net_io::EventLoop::instance().start();

    net_io::AsyncTcpServer server;
    uint16_t port = net_io::get_free_tcp_port();
    net_io::TcpEndpoint server_ep("127.0.0.1", port);
    auto sres = server.start(server_ep, 8);
    ASSERT_TRUE((bool)sres);

    // spawn multiple accept tasks and give them a short head start before connecting clients
    const int N = 3;
    std::vector<std::future<std::expected<std::shared_ptr<net_io::AsyncTcpSocket>, std::error_code>>> futures;
    for (int i = 0; i < N; ++i) {
        futures.push_back(std::async(std::launch::async, [&] {
            auto task = server.accept();
            return task.get();
        }));    
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // create N blocking clients and connect
    std::vector<int> clients;
    for (int i = 0; i < N; ++i) {
        int c = ::socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in sa{}; sa.sin_family = AF_INET; sa.sin_port = htons(port); inet_pton(AF_INET, "127.0.0.1", &sa.sin_addr);
        int rc = ::connect(c, reinterpret_cast<sockaddr*>(&sa), sizeof(sa));
        ASSERT_EQ(rc, 0);
        clients.push_back(c);
    }

    // gather accept results
    std::vector<std::shared_ptr<net_io::AsyncTcpSocket>> server_socks;
    for (auto &f : futures) {
        auto res = f.get();
        ASSERT_TRUE((bool)res);
        server_socks.push_back(res.value());
    }

    // Diagnostic: ensure accepted sockets are non-blocking to avoid recv() blocking
    for (auto &s : server_socks) {
        int fd = s->native_handle();
#if defined(_WIN32)
        // skipping windows F_GETFL check in this diagnostic
#else
        int flags = ::fcntl(fd, F_GETFL, 0);
        ASSERT_NE(flags, -1);
        ASSERT_TRUE(flags & O_NONBLOCK);
#endif
    }

    // each client send a short message, server should read it
    for (int i = 0; i < N; ++i) {
        const char* msg = "m";
        ::send(clients[i], msg, 1, 0);
        std::array<char, 8> buf{};
        // use read_exact_async to ensure we receive exactly 1 byte for stream sockets
    // Wrap accepted socket into adapter and use high-level helper
    net_io::AsyncTcpStreamAdapter stream(server_socks[i]);
    auto rres = sync_run3([&](){ return stream.read_exact_async(std::span<char>(buf.data(), 1)); });
    ASSERT_TRUE((bool)rres);
    ASSERT_TRUE(rres.has_value());
    ASSERT_EQ(buf[0], 'm');
    }

    for (int c : clients) {
#if defined(_WIN32)
        ::closesocket(c);
#else
        ::close(c);
#endif
    }
    server.stop();
    net_io::EventLoop::instance().stop();
}
