// test_file.cpp
import modern_io;
import modern_io.file;
import modern_io.exceptions;
#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include <string>
#include <span>

using namespace modern_io;
namespace fs = std::filesystem;

class FileIOTest : public ::testing::Test {
protected:
    std::string test_dir;
    std::string test_file;
    std::string nonexistent_file;
    
    void SetUp() override {
        const auto* test_info = ::testing::UnitTest::GetInstance()->current_test_info();
        test_dir = (fs::temp_directory_path() / "modern_io_tests" /
                    (std::string(test_info->test_suite_name()) + "_" + test_info->name())).string();
        test_file = test_dir + "/test.bin";
        nonexistent_file = test_dir + "/nonexistent.bin";

        fs::remove_all(test_dir);
        fs::create_directories(test_dir);
    }
    
    void TearDown() override {
        fs::remove_all(test_dir);
    }
};

TEST_F(FileIOTest, WriteAndReadBasic) {
    // Write data
    {
        FileOutputStream out(test_file);
        const char* data = "Hello, File!";
        out.write(data, 12);
        out.flush();
    }
    
    // Read data back
    {
        FileInputStream in(test_file);
        char buffer[20];
        std::size_t read_size = in.read(buffer, 12);
        
        EXPECT_EQ(read_size, 12);
        EXPECT_EQ(std::string(buffer, read_size), "Hello, File!");
    }
}

TEST_F(FileIOTest, WriteSpanOverloads) {
    {
        FileOutputStream out(test_file);
        
        // Write char span
        std::string str = "Test";
        std::span<const char> char_span(str.data(), str.size());
        out.write(char_span);
        
        // Write byte span
        std::vector<std::byte> bytes = {std::byte{' '}, std::byte{'D'}, std::byte{'a'}, std::byte{'t'}, std::byte{'a'}};
        std::span<const std::byte> byte_span(bytes);
        out.write(byte_span);
        
        out.flush();
    }
    
    {
        FileInputStream in(test_file);
        char buffer[20];
        std::size_t read_size = in.read(buffer, 9);
        
        EXPECT_EQ(read_size, 9);
        EXPECT_EQ(std::string(buffer, read_size), "Test Data");
    }
}

TEST_F(FileIOTest, ReadSpanOverloads) {
    // Prepare test file
    {
        std::ofstream out(test_file, std::ios::binary);
        out << "ABCDEFGH";
    }
    
    FileInputStream in(test_file);
    
    // Read into char span
    std::vector<char> char_buf(4);
    std::span<char> char_span(char_buf);
    std::size_t read1 = in.read(char_span);
    EXPECT_EQ(read1, 4);
    EXPECT_EQ(std::string(char_buf.data(), 4), "ABCD");
    
    // Read into byte span
    std::vector<std::byte> byte_buf(4);
    std::span<std::byte> byte_span(byte_buf);
    std::size_t read2 = in.read(byte_span);
    EXPECT_EQ(read2, 4);
    EXPECT_EQ(static_cast<char>(byte_buf[0]), 'E');
    EXPECT_EQ(static_cast<char>(byte_buf[3]), 'H');
}

TEST_F(FileIOTest, EOFDetection) {
    // Create small file
    {
        FileOutputStream out(test_file);
        out.write("12", 2);
        out.flush();
    }
    
    FileInputStream in(test_file);
    char buffer[10];
    
    EXPECT_FALSE(in.eof());
    in.read(buffer, 2);
    in.read(buffer, 1);
    EXPECT_TRUE(in.eof());
    
    // Reading past EOF should return 0
    std::size_t read_size = in.read(buffer, 5);
    EXPECT_EQ(read_size, 0);
}

TEST_F(FileIOTest, SeekOperations) {
    // Write test data
    {
        FileOutputStream out(test_file);
        out.write("0123456789", 10);
        out.flush();
    }
    
    // Test input seeking
    {
        FileInputStream in(test_file);
        
        // Seek to position 5
        in.seekg(5);
        EXPECT_EQ(in.tellg(), 5);
        
        char buffer[5];
        std::size_t read_size = in.read(buffer, 5);
        EXPECT_EQ(read_size, 5);
        EXPECT_EQ(std::string(buffer, 5), "56789");
        
        // Seek back to beginning
        in.seekg(0);
        EXPECT_EQ(in.tellg(), 0);
        
        read_size = in.read(buffer, 3);
        EXPECT_EQ(std::string(buffer, 3), "012");
    }
    
    // Test output seeking
    {
        FileOutputStream out(test_file);
        out.write("XXXXX", 5);
        
        // Seek to position 2
        out.seekp(2);
        EXPECT_EQ(out.tellp(), 2);
        
        out.write("YYY", 3);
        out.flush();
    }
    
    // Verify the result
    {
        FileInputStream in(test_file);
        char buffer[10];
        std::size_t read_size = in.read(buffer, 5);
        EXPECT_EQ(std::string(buffer, read_size), "XXYYY");
    }
}

TEST_F(FileIOTest, OpenNonexistentFileForReading) {
    EXPECT_THROW({
        FileInputStream in(nonexistent_file);
    }, FileIOException);
    
    try {
        FileInputStream in(nonexistent_file);
        FAIL() << "Expected FileIOException";
    } catch (const FileIOException& e) {
        EXPECT_NE(std::string(e.what()).find(nonexistent_file), std::string::npos);
        EXPECT_EQ(e.filepath(), nonexistent_file);
    }
}

TEST_F(FileIOTest, WriteToInvalidPath) {
    std::string invalid_path = "/invalid/path/that/does/not/exist/file.bin";
    
    EXPECT_THROW({
        FileOutputStream out(invalid_path);
    }, FileIOException);
    
    try {
        FileOutputStream out(invalid_path);
        FAIL() << "Expected FileIOException";
    } catch (const FileIOException& e) {
        EXPECT_EQ(e.filepath(), invalid_path);
    }
}

TEST_F(FileIOTest, LargeFileOperations) {
    const std::size_t large_size = 1024 * 1024;  // 1 MB
    std::vector<char> large_data(large_size, 'X');
    
    // Write large file
    {
        FileOutputStream out(test_file);
        out.write(large_data.data(), large_size);
        out.flush();
    }
    
    // Read it back
    {
        FileInputStream in(test_file);
        std::vector<char> read_buffer(large_size);
        std::size_t total_read = 0;
        
        while (total_read < large_size) {
            std::size_t chunk = in.read(read_buffer.data() + total_read, large_size - total_read);
            if (chunk == 0) break;
            total_read += chunk;
        }
        
        EXPECT_EQ(total_read, large_size);
        EXPECT_EQ(read_buffer, large_data);
    }
    
    // Verify file size
    EXPECT_EQ(fs::file_size(test_file), large_size);
}

TEST_F(FileIOTest, MoveSemantics) {
    // Test move constructor and assignment
    {
        FileOutputStream out1(test_file);
        out1.write("Test", 4);
        
        // Move constructor
        FileOutputStream out2(std::move(out1));
        out2.write(" Data", 5);
        out2.flush();
    }
    
    {
        FileInputStream in(test_file);
        char buffer[20];
        std::size_t read_size = in.read(buffer, 9);
        EXPECT_EQ(std::string(buffer, read_size), "Test Data");
    }
}

TEST_F(FileIOTest, MultipleFlushes) {
    FileOutputStream out(test_file);
    
    out.write("First", 5);
    out.flush();
    
    out.write("Second", 6);
    out.flush();
    
    out.write("Third", 5);
    out.flush();
    
    FileInputStream in(test_file);
    char buffer[20];
    std::size_t read_size = in.read(buffer, 16);
    EXPECT_EQ(std::string(buffer, read_size), "FirstSecondThird");
}
