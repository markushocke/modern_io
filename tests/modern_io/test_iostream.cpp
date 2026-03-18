// test_iostream.cpp
import modern_io;
import modern_io.iostream;
#include <gtest/gtest.h>
#include <sstream>
#include <string>
#include <span>

using namespace modern_io;

TEST(IOStreamAdapterTest, OstreamOutputStreamBasic) {
    std::ostringstream oss;
    OstreamOutputStream adapter(oss);
    
    const char* data = "Hello, Stream!";
    adapter.write(data, 14);
    adapter.flush();
    
    EXPECT_EQ(oss.str(), "Hello, Stream!");
}

TEST(IOStreamAdapterTest, OstreamOutputStreamMultipleWrites) {
    std::ostringstream oss;
    OstreamOutputStream adapter(oss);
    
    adapter.write("First", 5);
    adapter.write(" ", 1);
    adapter.write("Second", 6);
    adapter.flush();
    
    EXPECT_EQ(oss.str(), "First Second");
}

TEST(IOStreamAdapterTest, OstreamOutputStreamSpanOverloads) {
    std::ostringstream oss;
    OstreamOutputStream adapter(oss);
    
    // Char span
    std::string str = "Test ";
    std::span<const char> char_span(str.data(), str.size());
    adapter.write(char_span);
    
    // Byte span
    std::vector<std::byte> bytes = {
        std::byte{'D'}, std::byte{'a'}, std::byte{'t'}, std::byte{'a'}
    };
    std::span<const std::byte> byte_span(bytes);
    adapter.write(byte_span);
    
    adapter.flush();
    
    EXPECT_EQ(oss.str(), "Test Data");
}

TEST(IOStreamAdapterTest, IstreamInputStreamBasic) {
    std::istringstream iss("Hello, World!");
    IstreamInputStream adapter(iss);
    
    char buffer[20];
    std::size_t read_size = adapter.read(buffer, 13);
    
    EXPECT_EQ(read_size, 13);
    EXPECT_EQ(std::string(buffer, read_size), "Hello, World!");
}

TEST(IOStreamAdapterTest, IstreamInputStreamPartialReads) {
    std::istringstream iss("ABCDEFGHIJ");
    IstreamInputStream adapter(iss);
    
    char buf1[5];
    std::size_t read1 = adapter.read(buf1, 5);
    EXPECT_EQ(read1, 5);
    EXPECT_EQ(std::string(buf1, 5), "ABCDE");
    
    char buf2[3];
    std::size_t read2 = adapter.read(buf2, 3);
    EXPECT_EQ(read2, 3);
    EXPECT_EQ(std::string(buf2, 3), "FGH");
    
    char buf3[5];
    std::size_t read3 = adapter.read(buf3, 5);
    EXPECT_EQ(read3, 2);  // Only 2 chars left
    EXPECT_EQ(std::string(buf3, 2), "IJ");
}

TEST(IOStreamAdapterTest, IstreamInputStreamEOF) {
    std::istringstream iss("12");
    IstreamInputStream adapter(iss);
    
    EXPECT_FALSE(adapter.eof());
    
    char buffer[10];
    adapter.read(buffer, 2);
    
    EXPECT_TRUE(adapter.eof());
}

TEST(IOStreamAdapterTest, IstreamInputStreamSpanOverloads) {
    std::istringstream iss("Test Data");
    IstreamInputStream adapter(iss);
    
    // Char span
    std::vector<char> char_buf(4);
    std::span<char> char_span(char_buf);
    std::size_t read1 = adapter.read(char_span);
    EXPECT_EQ(read1, 4);
    EXPECT_EQ(std::string(char_buf.data(), 4), "Test");
    
    // Byte span
    std::vector<std::byte> byte_buf(5);
    std::span<std::byte> byte_span(byte_buf);
    std::size_t read2 = adapter.read(byte_span);
    EXPECT_EQ(read2, 5);
    EXPECT_EQ(static_cast<char>(byte_buf[0]), ' ');
    EXPECT_EQ(static_cast<char>(byte_buf[1]), 'D');
}

TEST(IOStreamAdapterTest, RoundTripWithStringStreams) {
    std::stringstream ss;
    
    // Write
    {
        OstreamOutputStream out(ss);
        out.write("Round", 5);
        out.write(" Trip", 5);
        out.flush();
    }
    
    // Read
    {
        IstreamInputStream in(ss);
        char buffer[20];
        std::size_t read_size = in.read(buffer, 10);
        
        EXPECT_EQ(read_size, 10);
        EXPECT_EQ(std::string(buffer, read_size), "Round Trip");
    }
}

TEST(IOStreamAdapterTest, IntegrationWithDataStream) {
    std::stringstream ss;
    
    // Write structured data
    {
        OstreamOutputStream stream_out(ss);
        DataOutputStream data_out(std::move(stream_out), std::endian::big);
        
        data_out.write_int32(12345);
        data_out.write_string("Test");
        data_out.write_double(3.14159);
        data_out.flush();
    }
    
    // Read it back
    {
        IstreamInputStream stream_in(ss);
        DataInputStream data_in(std::move(stream_in), std::endian::big);
        
        EXPECT_EQ(data_in.read_int32(), 12345);
        EXPECT_EQ(data_in.read_string(), "Test");
        EXPECT_DOUBLE_EQ(data_in.read_double(), 3.14159);
    }
}

TEST(IOStreamAdapterTest, EmptyStream) {
    std::istringstream iss("");
    IstreamInputStream adapter(iss);
    
    EXPECT_TRUE(adapter.eof());
    
    char buffer[10];
    std::size_t read_size = adapter.read(buffer, 10);
    EXPECT_EQ(read_size, 0);
}

TEST(IOStreamAdapterTest, BinaryData) {
    std::stringstream ss(std::ios::in | std::ios::out | std::ios::binary);
    
    // Write binary data
    {
        OstreamOutputStream out(ss);
        std::vector<std::byte> binary = {
            std::byte{0x00}, std::byte{0xFF}, std::byte{0x80}, std::byte{0x7F}
        };
        std::span<const std::byte> span(binary);
        out.write(span);
        out.flush();
    }
    
    // Read binary data
    {
        IstreamInputStream in(ss);
        std::vector<std::byte> buffer(4);
        std::span<std::byte> span(buffer);
        std::size_t read_size = in.read(span);
        
        EXPECT_EQ(read_size, 4);
        EXPECT_EQ(buffer[0], std::byte{0x00});
        EXPECT_EQ(buffer[1], std::byte{0xFF});
        EXPECT_EQ(buffer[2], std::byte{0x80});
        EXPECT_EQ(buffer[3], std::byte{0x7F});
    }
}

TEST(IOStreamAdapterTest, LargeDataTransfer) {
    std::stringstream ss;
    
    const std::size_t size = 10000;
    std::vector<char> large_data(size, 'X');
    
    // Write
    {
        OstreamOutputStream out(ss);
        out.write(large_data.data(), size);
        out.flush();
    }
    
    // Read
    {
        IstreamInputStream in(ss);
        std::vector<char> read_buffer(size);
        std::size_t total_read = 0;
        
        while (total_read < size && !in.eof()) {
            std::size_t chunk = in.read(read_buffer.data() + total_read, size - total_read);
            total_read += chunk;
        }
        
        EXPECT_EQ(total_read, size);
        EXPECT_EQ(read_buffer, large_data);
    }
}
