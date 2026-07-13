// test_adapters.cpp
import net_io_adapters;
import net_io;
import modern_io.stream;
#include <gtest/gtest.h>
#include <algorithm>
#include <cstring>
#include <memory>
#include <sstream>
#include <utility>
#include <vector>

using namespace net_io_adapters;

static_assert(std::same_as<modern::net::adapters::SharedStream<std::stringstream>, net_io_adapters::SharedStream<std::stringstream>>);

namespace {

struct TestTransport {
    std::vector<char> data;
    std::size_t position = 0;

    void write(const char* input, std::size_t size) {
        data.insert(data.end(), input, input + size);
    }

    std::size_t read(char* output, std::size_t size) {
        const auto count = std::min(size, data.size() - position);
        std::memcpy(output, data.data() + position, count);
        position += count;
        return count;
    }
};

template<class T>
concept StreamAdaptable = requires(T&& value) {
    modern::net::as_stream(std::forward<T>(value));
};

static_assert(!StreamAdaptable<modern::net::TcpEndpoint>);
static_assert(!StreamAdaptable<modern::net::UdpEndpoint>);
static_assert(!StreamAdaptable<TestTransport*>);

} // namespace

TEST(AdaptersTest, SharedStreamStringStream) {
    // Use a stringstream as a simple transport and wrap it with SharedStream
    auto ss = std::make_shared<std::stringstream>();

    // SharedStream forwards to the underlying stream
    auto shared = SharedStream<std::stringstream>(ss);

    // Write via shared
    const char* txt = "hello";
    shared.write(txt, 5);
    shared.flush();

    // Read back directly from the stringstream
    std::string out = ss->str();
    EXPECT_EQ(out, "hello");
}

TEST(AdaptersTest, CanonicalAdapterNamespaceWorks) {
    auto ss = std::make_shared<std::stringstream>();
    modern::net::adapters::SharedStream<std::stringstream> shared(ss);
    shared.write("ok", 2);
    shared.flush();
    EXPECT_EQ(ss->str(), "ok");
}

TEST(AdaptersTest, AsStreamAdaptsAnOpenedOwnedTransport) {
    auto transport = std::make_shared<TestTransport>();
    std::weak_ptr<TestTransport> lifetime = transport;

    {
        auto stream = modern::net::as_stream(transport);
        static_assert(modern::io::DuplexStream<decltype(stream)>);

        transport.reset();
        ASSERT_FALSE(lifetime.expired());

        stream.write("hello", 5);
        char output[5]{};
        EXPECT_EQ(stream.read(output, sizeof(output)), sizeof(output));
        EXPECT_EQ(std::string_view(output, sizeof(output)), "hello");
    }

    EXPECT_TRUE(lifetime.expired());
}
