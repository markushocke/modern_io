import modern_io;
import net_io;
import net_io_adapters;

#include <memory>
#include <sstream>

int main() {
    modern_io::ConnectionArena arena;
    net_io::TcpEndpoint endpoint{"127.0.0.1", 9100};
    auto storage = std::make_shared<std::stringstream>();
    net_io_adapters::SharedStream<std::stringstream> stream(storage);

    stream.write("ok", 2);
    stream.flush();

    if (arena.memory_resource() == nullptr) {
        return 1;
    }
    if (endpoint.port != 9100) {
        return 2;
    }
    if (storage->str() != "ok") {
        return 3;
    }

    return 0;
}