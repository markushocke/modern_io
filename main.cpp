import modern_io;
import net_io;
import net_io_adapters;

// This can be removed when msvc better supports umbrella imports
import net_io.tcp_endpoint;
import net_io.tcp_client;
import net_io.tcp_server;
import net_io.udp_endpoint;
import net_io.udp_transport;

#include <iostream>
#include <syncstream>
#include <thread>
#include <chrono>
#include <string>
#include <atomic>

using namespace modern_io;
using namespace net_io;
using namespace net_io_adapters;

constexpr uint16_t TCP_PORT = 9050;
constexpr uint16_t UDP_PORT = 9050;
std::string address = "127.0.0.1";

// TCP server: receives a message, replies, then exits
void tcp_server()
{
    std::atomic<bool> running{true};
    ThreadExecutor exec;

    // Use a named lambda so the type matches exactly for the template parameter
    auto tcp_handler = [](auto&& shared_stream) {
        DataInputStream<decltype(shared_stream)> in(std::move(shared_stream), std::endian::big);
        DataOutputStream<decltype(shared_stream)> out(std::move(shared_stream), std::endian::big);

        std::string msg = in.read_string();
        std::osyncstream(std::cout) << "[TCP-Server] Received: " << msg << std::endl;

        out.write_string("PONG");
        out.flush();
    };

    run_tcp_server(
        exec,
        std::move(tcp_handler),
        running,
        TcpEndpoint{address, TCP_PORT}
    );

    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    running = false;
}

// UDP server: receives a datagram, replies, then exits
void udp_server()
{
    UdpEndpoint ep{address, UDP_PORT, true, UDP_PORT};
    auto shared_stream = make_shared_stream_for_server(ep);
    DataInputStream<decltype(shared_stream)> in(shared_stream, std::endian::big);
    DataOutputStream<decltype(shared_stream)> out(shared_stream, std::endian::big);

    std::string msg = in.read_string();
    std::osyncstream(std::cout) << "[UDP-Server] Received: " << msg << std::endl;

    out.write_string("UDP-PONG");
    out.flush();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
}

int main()
{
    // TCP test
    {
        std::thread srv(tcp_server);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        TcpEndpoint ep({address, TCP_PORT});
        auto tcp_stream = make_shared_stream(ep);

        DataOutputStream<decltype(tcp_stream)> out(tcp_stream, std::endian::big);
        DataInputStream<decltype(tcp_stream)>  in(tcp_stream, std::endian::big);

        out.write_string("PING");
        out.flush();
        std::string reply = in.read_string();
        std::osyncstream(std::cout) << "[TCP-Client] Received: " << reply << std::endl;

        srv.join();
    }

    // UDP test
    {
        std::thread srv(udp_server);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        UdpEndpoint client_ep(address, UDP_PORT);
        auto udp_stream = make_shared_stream(client_ep);

        DataOutputStream<decltype(udp_stream)> out(udp_stream, std::endian::big);
        DataInputStream<decltype(udp_stream)>  in(udp_stream, std::endian::big);

        out.write_string("UDP-PING");
        out.flush();
        std::string reply = in.read_string();
        std::osyncstream(std::cout) << "[UDP-Client] Received: " << reply << std::endl;

        srv.join();
    }

    // File test: write and read a string (unbuffered)
    {
        FileOutputStream fsink("test.bin");
        DataOutputStream<FileOutputStream> dout(std::move(fsink), std::endian::big);
        dout.write_string("Hello File!");
        dout.flush();

        FileInputStream fsrc("test.bin");
        DataInputStream<FileInputStream> din(std::move(fsrc), std::endian::big);
        std::string s = din.read_string();
        std::osyncstream(std::cout) << "[File] Read: " << s << std::endl;
    }

    // File test: write and read a string (buffered)
    {
        FileOutputStream fsink("test2.bin");
        BufferedOutputStream<FileOutputStream> bos(std::move(fsink));
        DataOutputStream<decltype(bos)> dout(std::move(bos), std::endian::big);
        dout.write_string("Hello Buffer!");
        dout.flush();

        FileInputStream fsrc("test2.bin");
        BufferedInputStream<FileInputStream> bis(std::move(fsrc));
        DataInputStream<decltype(bis)> din(std::move(bis), std::endian::big);
        std::string s = din.read_string();
        std::osyncstream(std::cout) << "[File-Buffered] Read: " << s << std::endl;
    }

    return 0;
}
// File: main.cpp