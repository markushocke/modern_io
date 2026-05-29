// test_adapters.cpp
import net_io_adapters;
#include <gtest/gtest.h>
#include <sstream>

using namespace net_io_adapters;

static_assert(std::same_as<modern::net::adapters::SharedStream<std::stringstream>, net_io_adapters::SharedStream<std::stringstream>>);

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
