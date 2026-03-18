// test_data.cpp
import modern_io;
import modern_io.data;
import modern_io.exceptions;
#include <gtest/gtest.h>
#include <vector>
#include <span>
#include <bit>
#include <cstring>

using namespace modern_io;

// Mock Stream for testing (copyable for DataOutputStream/DataInputStream)
class MemoryStream {
public:
    std::shared_ptr<std::vector<char>> data;
    std::size_t read_pos = 0;
    
    MemoryStream() : data(std::make_shared<std::vector<char>>()) {}
    
    void write(const char* ptr, std::size_t size) {
        data->insert(data->end(), ptr, ptr + size);
    }
    
    void write(std::span<const std::byte> span) {
        const char* ptr = reinterpret_cast<const char*>(span.data());
        write(ptr, span.size());
    }
    
    void write(std::span<const char> span) {
        write(span.data(), span.size());
    }
    
    std::size_t read(char* ptr, std::size_t size) {
        std::size_t available = data->size() - read_pos;
        std::size_t to_read = std::min(size, available);
        std::memcpy(ptr, data->data() + read_pos, to_read);
        read_pos += to_read;
        return to_read;
    }
    
    std::size_t read(std::span<std::byte> span) {
        return read(reinterpret_cast<char*>(span.data()), span.size());
    }
    
    std::size_t read(std::span<char> span) {
        return read(span.data(), span.size());
    }
    
    void flush() {}
    
    bool eof() const noexcept {
        return read_pos >= data->size();
    }
    
    void reset() {
        read_pos = 0;
    }
};

TEST(DataStreamTest, Int32BigEndian) {
    MemoryStream stream;
    
    // Write using DataOutputStream
    {
        DataOutputStream out(stream, std::endian::big);
        out.write_int32(0x12345678);
        out.flush();
    }
    
    // Verify the bytes are in big endian order
    EXPECT_EQ(stream.data->size(), 4);
    EXPECT_EQ(static_cast<unsigned char>((*stream.data)[0]), 0x12);
    EXPECT_EQ(static_cast<unsigned char>((*stream.data)[1]), 0x34);
    EXPECT_EQ(static_cast<unsigned char>((*stream.data)[2]), 0x56);
    EXPECT_EQ(static_cast<unsigned char>((*stream.data)[3]), 0x78);
}

TEST(DataStreamTest, Int32BigEndianRoundTrip) {
    MemoryStream stream;
    
    // Write
    {
        DataOutputStream out(stream, std::endian::big);
        out.write_int32(0x12345678);
        out.write_int32(-42);
        out.write_int32(0);
        out.flush();
    }
    
    // Read
    {
        DataInputStream in(stream, std::endian::big);
        EXPECT_EQ(in.read_int32(), 0x12345678);
        EXPECT_EQ(in.read_int32(), -42);
        EXPECT_EQ(in.read_int32(), 0);
    }
}

TEST(DataStreamTest, Int32LittleEndianRoundTrip) {
    MemoryStream stream;
    
    {
        DataOutputStream out(stream, std::endian::little);
        out.write_int32(0x12345678);
        out.write_int32(-999);
        out.flush();
    }
    
    {
        DataInputStream in(stream, std::endian::little);
        EXPECT_EQ(in.read_int32(), 0x12345678);
        EXPECT_EQ(in.read_int32(), -999);
    }
}

TEST(DataStreamTest, UInt32RoundTrip) {
    MemoryStream stream;
    
    {
        DataOutputStream out(stream, std::endian::big);
        out.write_uint32(0xFFFFFFFF);
        out.write_uint32(0);
        out.write_uint32(0x80000000);
        out.flush();
    }
    
    {
        DataInputStream in(stream, std::endian::big);
        EXPECT_EQ(in.read_uint32(), 0xFFFFFFFF);
        EXPECT_EQ(in.read_uint32(), 0);
        EXPECT_EQ(in.read_uint32(), 0x80000000);
    }
}

TEST(DataStreamTest, Int64RoundTrip) {
    MemoryStream stream;
    
    {
        DataOutputStream out(stream, std::endian::big);
        out.write_int64(0x123456789ABCDEF0LL);
        out.write_int64(-1LL);
        out.write_int64(0LL);
        out.flush();
    }
    
    {
        DataInputStream in(stream, std::endian::big);
        EXPECT_EQ(in.read_int64(), 0x123456789ABCDEF0LL);
        EXPECT_EQ(in.read_int64(), -1LL);
        EXPECT_EQ(in.read_int64(), 0LL);
    }
}

TEST(DataStreamTest, UInt64RoundTrip) {
    MemoryStream stream;
    
    {
        DataOutputStream out(stream, std::endian::little);
        out.write_uint64(0xFFFFFFFFFFFFFFFFULL);
        out.write_uint64(0x8000000000000000ULL);
        out.flush();
    }
    
    {
        DataInputStream in(stream, std::endian::little);
        EXPECT_EQ(in.read_uint64(), 0xFFFFFFFFFFFFFFFFULL);
        EXPECT_EQ(in.read_uint64(), 0x8000000000000000ULL);
    }
}

TEST(DataStreamTest, FloatRoundTrip) {
    MemoryStream stream;
    
    {
        DataOutputStream out(stream, std::endian::big);
        out.write_float(3.14159f);
        out.write_float(-2.71828f);
        out.write_float(0.0f);
        out.flush();
    }
    
    {
        DataInputStream in(stream, std::endian::big);
        EXPECT_FLOAT_EQ(in.read_float(), 3.14159f);
        EXPECT_FLOAT_EQ(in.read_float(), -2.71828f);
        EXPECT_FLOAT_EQ(in.read_float(), 0.0f);
    }
}

TEST(DataStreamTest, DoubleRoundTrip) {
    MemoryStream stream;
    
    {
        DataOutputStream out(stream, std::endian::little);
        out.write_double(3.141592653589793);
        out.write_double(-2.718281828459045);
        out.write_double(1.0e100);
        out.flush();
    }
    
    {
        DataInputStream in(stream, std::endian::little);
        EXPECT_DOUBLE_EQ(in.read_double(), 3.141592653589793);
        EXPECT_DOUBLE_EQ(in.read_double(), -2.718281828459045);
        EXPECT_DOUBLE_EQ(in.read_double(), 1.0e100);
    }
}

TEST(DataStreamTest, StringRoundTrip) {
    MemoryStream stream;
    
    {
        DataOutputStream out(stream, std::endian::big);
        out.write_string("Hello, World!");
        out.write_string("");
        out.write_string("Unicode: äöü 中文");
        out.flush();
    }
    
    {
        DataInputStream in(stream, std::endian::big);
        EXPECT_EQ(in.read_string(), "Hello, World!");
        EXPECT_EQ(in.read_string(), "");
        EXPECT_EQ(in.read_string(), "Unicode: äöü 中文");
    }
}

TEST(DataStreamTest, MixedTypesRoundTrip) {
    MemoryStream stream;
    
    {
        DataOutputStream out(stream, std::endian::big);
        out.write_int32(42);
        out.write_string("Test");
        out.write_double(3.14);
        out.write_uint64(999ULL);
        out.flush();
    }
    
    {
        DataInputStream in(stream, std::endian::big);
        EXPECT_EQ(in.read_int32(), 42);
        EXPECT_EQ(in.read_string(), "Test");
        EXPECT_DOUBLE_EQ(in.read_double(), 3.14);
        EXPECT_EQ(in.read_uint64(), 999ULL);
    }
}

TEST(DataStreamTest, UnexpectedEOF) {
    MemoryStream stream;
    
    // Manually set only 2 bytes - int32 needs 4
    *stream.data = {0x12, 0x34};
    
    DataInputStream in(stream, std::endian::big);
    EXPECT_THROW({
        (void)in.read_int32();
    }, UnexpectedEOFException);
}

TEST(DataStreamTest, InvalidStringLength) {
    MemoryStream stream;
    
    // Write negative length
    {
        DataOutputStream out(stream, std::endian::big);
        out.write_int32(-1);  // Invalid string length
        out.flush();
    }
    
    DataInputStream in(stream, std::endian::big);
    EXPECT_THROW({
        (void)in.read_string();
    }, DataFormatException);
}

TEST(DataStreamTest, StringLengthTooLarge) {
    MemoryStream stream;
    
    {
        DataOutputStream out(stream, std::endian::big);
        out.write_int32(200'000'000);  // Exceeds 100MB limit
        out.flush();
    }
    
    DataInputStream in(stream, std::endian::big);
    EXPECT_THROW({
        (void)in.read_string();
    }, DataFormatException);
}

TEST(DataStreamTest, BytesRoundTrip) {
    MemoryStream stream;
    
    std::vector<std::byte> test_bytes = {
        std::byte{0x01}, std::byte{0x02}, std::byte{0xFF}, std::byte{0xAA}
    };
    
    {
        DataOutputStream out(stream, std::endian::big);
        out.write_bytes(test_bytes);
        out.flush();
    }
    
    {
        DataInputStream in(stream, std::endian::big);
        auto read_bytes = in.read_bytes(4);
        EXPECT_EQ(read_bytes, test_bytes);
    }
}

TEST(DataStreamTest, EOFDetection) {
    MemoryStream stream;
    
    {
        DataOutputStream out(stream, std::endian::big);
        out.write_int32(123);
        out.flush();
    }
    
    DataInputStream in(stream, std::endian::big);
    EXPECT_FALSE(in.eof());
    
    int32_t value = in.read_int32();
    EXPECT_EQ(value, 123);
    EXPECT_TRUE(in.eof());
}

TEST(DataStreamTest, MoveSemantics) {
    MemoryStream stream1;
    
    {
        DataOutputStream out1(stream1, std::endian::big);
        out1.write_int32(111);
        
        // Move constructor
        DataOutputStream out2(std::move(out1));
        out2.write_int32(222);
        out2.flush();
    }
    
    DataInputStream in(stream1, std::endian::big);
    EXPECT_EQ(in.read_int32(), 111);
    EXPECT_EQ(in.read_int32(), 222);
}

TEST(DataStreamTest, LargeDataTransfer) {
    MemoryStream stream;
    
    const std::size_t count = 10000;
    
    {
        DataOutputStream out(stream, std::endian::big);
        for (std::size_t i = 0; i < count; ++i) {
            out.write_uint32(static_cast<uint32_t>(i));
        }
        out.flush();
    }
    
    {
        DataInputStream in(stream, std::endian::big);
        for (std::size_t i = 0; i < count; ++i) {
            EXPECT_EQ(in.read_uint32(), static_cast<uint32_t>(i));
        }
    }
}
