// test_concepts.cpp
import modern_io;
import modern_io.concepts;
#include <gtest/gtest.h>
#include <span>
#include <vector>
#include <cstddef>
#include <cstring>

using namespace modern_io;

// Mock OutputStream for testing
class MockOutputStream {
public:
    std::vector<char> data;
    
    void write(const char* ptr, std::size_t size) {
        data.insert(data.end(), ptr, ptr + size);
    }
    
    void write(std::span<const std::byte> span) {
        const char* ptr = reinterpret_cast<const char*>(span.data());
        data.insert(data.end(), ptr, ptr + span.size());
    }
    
    void write(std::span<const char> span) {
        data.insert(data.end(), span.begin(), span.end());
    }
    
    void flush() {}
};

// Mock InputStream for testing
class MockInputStream {
public:
    std::vector<char> data;
    std::size_t pos = 0;
    
    explicit MockInputStream(std::vector<char> d) : data(std::move(d)) {}
    
    std::size_t read(char* ptr, std::size_t size) {
        std::size_t available = data.size() - pos;
        std::size_t to_read = std::min(size, available);
        std::memcpy(ptr, data.data() + pos, to_read);
        pos += to_read;
        return to_read;
    }
    
    std::size_t read(std::span<std::byte> span) {
        return read(reinterpret_cast<char*>(span.data()), span.size());
    }
    
    std::size_t read(std::span<char> span) {
        return read(span.data(), span.size());
    }
    
    bool eof() const noexcept {
        return pos >= data.size();
    }
};

// Incomplete type (should NOT satisfy concepts)
class IncompleteOutputStream {
public:
    void write(const char*, std::size_t) {}
    // Missing flush()
};

class IncompleteInputStream {
public:
    std::size_t read(char*, std::size_t) { return 0; }
    // Missing eof()
};

TEST(ConceptsTest, OutputStreamConcept) {
    // MockOutputStream should satisfy OutputStream
    static_assert(OutputStream<MockOutputStream>, "MockOutputStream should satisfy OutputStream concept");
    
    // IncompleteOutputStream should NOT satisfy OutputStream
    static_assert(!OutputStream<IncompleteOutputStream>, "IncompleteOutputStream should not satisfy OutputStream concept");
}

TEST(ConceptsTest, InputStreamConcept) {
    // MockInputStream should satisfy InputStream
    static_assert(InputStream<MockInputStream>, "MockInputStream should satisfy InputStream concept");
    
    // IncompleteInputStream should NOT satisfy InputStream
    static_assert(!InputStream<IncompleteInputStream>, "IncompleteInputStream should not satisfy InputStream concept");
}

TEST(ConceptsTest, MockOutputStreamBasicOperation) {
    MockOutputStream out;
    const char* test_data = "Hello, World!";
    out.write(test_data, 13);
    
    ASSERT_EQ(out.data.size(), 13);
    EXPECT_EQ(std::string(out.data.data(), out.data.size()), "Hello, World!");
}

TEST(ConceptsTest, MockInputStreamBasicOperation) {
    std::vector<char> test_data = {'T', 'e', 's', 't'};
    MockInputStream in(test_data);
    
    char buffer[10];
    std::size_t read_size = in.read(buffer, 4);
    
    EXPECT_EQ(read_size, 4);
    EXPECT_EQ(std::string(buffer, read_size), "Test");
    EXPECT_TRUE(in.eof());
}

TEST(ConceptsTest, MockInputStreamPartialRead) {
    std::vector<char> test_data = {'A', 'B', 'C', 'D', 'E'};
    MockInputStream in(test_data);
    
    char buffer[3];
    std::size_t read1 = in.read(buffer, 3);
    EXPECT_EQ(read1, 3);
    EXPECT_EQ(std::string(buffer, 3), "ABC");
    EXPECT_FALSE(in.eof());
    
    std::size_t read2 = in.read(buffer, 3);
    EXPECT_EQ(read2, 2);  // Only 2 bytes remaining
    EXPECT_EQ(std::string(buffer, 2), "DE");
    EXPECT_TRUE(in.eof());
}

TEST(ConceptsTest, SpanOverloads) {
    MockOutputStream out;
    
    // Test std::span<const char>
    std::string str = "span test";
    std::span<const char> char_span(str.data(), str.size());
    out.write(char_span);
    
    EXPECT_EQ(out.data.size(), 9);
    EXPECT_EQ(std::string(out.data.data(), out.data.size()), "span test");
    
    // Test std::span<const std::byte>
    std::vector<std::byte> byte_vec = {std::byte{'!'}, std::byte{' '}};
    std::span<const std::byte> byte_span(byte_vec);
    out.write(byte_span);
    
    EXPECT_EQ(out.data.size(), 11);
    EXPECT_EQ(out.data[9], '!');
    EXPECT_EQ(out.data[10], ' ');
}
