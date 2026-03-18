// test_buffered.cpp
import modern_io;
import modern_io.buffered;
#include <gtest/gtest.h>
#include <vector>
#include <span>
#include <cstring>

using namespace modern_io;

// Mock Stream for testing
class CountingOutputStream {
public:
    std::vector<char> data;
    std::size_t write_count = 0;
    std::size_t flush_count = 0;
    
    void write(const char* ptr, std::size_t size) {
        data.insert(data.end(), ptr, ptr + size);
        write_count++;
    }
    
    void write(std::span<const std::byte> span) {
        const char* ptr = reinterpret_cast<const char*>(span.data());
        write(ptr, span.size());
    }
    
    void write(std::span<const char> span) {
        write(span.data(), span.size());
    }
    
    void flush() {
        flush_count++;
    }
};

class CountingInputStream {
public:
    std::vector<char> data;
    std::size_t read_pos = 0;
    std::size_t read_count = 0;
    
    explicit CountingInputStream(std::vector<char> d) : data(std::move(d)) {}
    
    std::size_t read(char* ptr, std::size_t size) {
        std::size_t available = data.size() - read_pos;
        std::size_t to_read = std::min(size, available);
        std::memcpy(ptr, data.data() + read_pos, to_read);
        read_pos += to_read;
        read_count++;
        return to_read;
    }
    
    std::size_t read(std::span<std::byte> span) {
        return read(reinterpret_cast<char*>(span.data()), span.size());
    }
    
    std::size_t read(std::span<char> span) {
        return read(span.data(), span.size());
    }
    
    bool eof() const noexcept {
        return read_pos >= data.size();
    }
};

TEST(BufferedOutputStreamTest, BasicBuffering) {
    CountingOutputStream counter;
    BufferedOutputStream<CountingOutputStream, 16> buffered(std::move(counter));
    
    // Write less than buffer size - should not trigger underlying write
    buffered.write("Hello", 5);
    // Cannot access counter directly after move, need to test differently
}

TEST(BufferedOutputStreamTest, BufferFlushOnFull) {
    CountingOutputStream counter;
    constexpr std::size_t BUF_SIZE = 8;
    BufferedOutputStream<CountingOutputStream, BUF_SIZE> buffered(std::move(counter));
    
    // Write exactly buffer size
    buffered.write("12345678", 8);
    // Buffer should auto-flush
    
    // Write more
    buffered.write("ABCD", 4);
    buffered.flush();
    
    // Total should be written correctly
}

TEST(BufferedOutputStreamTest, SmallWrites) {
    CountingOutputStream counter;
    BufferedOutputStream<CountingOutputStream, 1024> buffered(std::move(counter));
    
    // Many small writes should be buffered
    for (int i = 0; i < 100; ++i) {
        buffered.write("x", 1);
    }
    
    buffered.flush();
    
    // Verify data size
}

TEST(BufferedOutputStreamTest, LargeWrite) {
    CountingOutputStream counter;
    constexpr std::size_t BUF_SIZE = 16;
    BufferedOutputStream<CountingOutputStream, BUF_SIZE> buffered(std::move(counter));
    
    // Write larger than buffer size
    std::string large_data(100, 'A');
    buffered.write(large_data.data(), large_data.size());
    buffered.flush();
}

TEST(BufferedOutputStreamTest, SpanWrites) {
    CountingOutputStream counter;
    BufferedOutputStream<CountingOutputStream, 32> buffered(std::move(counter));
    
    // Test char span
    std::string str = "Hello";
    std::span<const char> char_span(str.data(), str.size());
    buffered.write(char_span);
    
    // Test byte span
    std::vector<std::byte> bytes = {std::byte{' '}, std::byte{'W'}, std::byte{'o'}, std::byte{'r'}, std::byte{'l'}, std::byte{'d'}};
    std::span<const std::byte> byte_span(bytes);
    buffered.write(byte_span);
    
    buffered.flush();
}

TEST(BufferedInputStreamTest, BasicBuffering) {
    std::vector<char> data = {'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H'};
    CountingInputStream counter(data);
    BufferedInputStream<CountingInputStream, 16> buffered(std::move(counter));
    
    char buf[4];
    std::size_t read_size = buffered.read(buf, 4);
    
    EXPECT_EQ(read_size, 4);
    EXPECT_EQ(std::string(buf, 4), "ABCD");
}

TEST(BufferedInputStreamTest, MultipleReads) {
    std::vector<char> data(100, 'X');
    CountingInputStream counter(data);
    constexpr std::size_t BUF_SIZE = 32;
    BufferedInputStream<CountingInputStream, BUF_SIZE> buffered(std::move(counter));
    
    // Multiple small reads should use buffer
    char buf[10];
    for (int i = 0; i < 10; ++i) {
        std::size_t read_size = buffered.read(buf, 10);
        EXPECT_EQ(read_size, 10);
    }
}

TEST(BufferedInputStreamTest, ReadPastEOF) {
    std::vector<char> data = {'1', '2', '3'};
    CountingInputStream counter(data);
    BufferedInputStream<CountingInputStream, 8> buffered(std::move(counter));
    
    char buf[10];
    
    // Read all data
    std::size_t read1 = buffered.read(buf, 3);
    EXPECT_EQ(read1, 3);
    
    // Try to read more - should return 0
    std::size_t read2 = buffered.read(buf, 10);
    EXPECT_EQ(read2, 0);
    
    EXPECT_TRUE(buffered.eof());
}

TEST(BufferedInputStreamTest, SpanReads) {
    std::vector<char> data = {'T', 'e', 's', 't', ' ', 'D', 'a', 't', 'a'};
    CountingInputStream counter(data);
    BufferedInputStream<CountingInputStream, 16> buffered(std::move(counter));
    
    // Read into char span
    std::vector<char> char_buf(4);
    std::span<char> char_span(char_buf);
    std::size_t read1 = buffered.read(char_span);
    EXPECT_EQ(read1, 4);
    EXPECT_EQ(std::string(char_buf.data(), 4), "Test");
    
    // Read into byte span
    std::vector<std::byte> byte_buf(5);
    std::span<std::byte> byte_span(byte_buf);
    std::size_t read2 = buffered.read(byte_span);
    EXPECT_EQ(read2, 5);
    EXPECT_EQ(static_cast<char>(byte_buf[0]), ' ');
    EXPECT_EQ(static_cast<char>(byte_buf[1]), 'D');
}

TEST(BufferedStreamTest, IntegrationWithDataStream) {
    // This test shows BufferedOutputStream working with DataOutputStream
    CountingOutputStream counter;
    BufferedOutputStream<CountingOutputStream, 64> buffered(std::move(counter));
    // Would need to adapt DataOutputStream to work with moved streams
    // For now, this is a placeholder
}

TEST(BufferedOutputStreamTest, FlushInDestructor) {
    CountingOutputStream counter;
    std::vector<char> expected_data;
    
    {
        BufferedOutputStream<CountingOutputStream, 16> buffered(std::move(counter));
        buffered.write("Test", 4);
        // Destructor should flush automatically
    }
    
    // After destruction, data should be flushed
}

TEST(BufferedOutputStreamTest, ExplicitFlush) {
    CountingOutputStream counter;
    BufferedOutputStream<CountingOutputStream, 32> buffered(std::move(counter));
    
    buffered.write("Data1", 5);
    buffered.flush();  // Explicit flush
    
    buffered.write("Data2", 5);
    buffered.flush();  // Another flush
}

TEST(BufferedStreamTest, DifferentBufferSizes) {
    // Test with very small buffer
    {
        CountingOutputStream counter;
        BufferedOutputStream<CountingOutputStream, 4> buffered(std::move(counter));
        buffered.write("12345678", 8);
        buffered.flush();
    }
    
    // Test with large buffer
    {
        CountingOutputStream counter;
        BufferedOutputStream<CountingOutputStream, 8192> buffered(std::move(counter));
        buffered.write("Test", 4);
        buffered.flush();
    }
}
