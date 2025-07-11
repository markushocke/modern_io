// modern_io_file.ixx
module;

#ifndef _MSC_VER
#include <fstream>
#include <stdexcept>
#include <string>
#include <span>
#include <vector>
#endif

export module modern_io:file;
import :concepts;

#ifdef _MSC_VER
import <fstream>;
import <stdexcept>;
import <string>;
import <span>;
import <vector>;
#endif

namespace modern_io
{

/**
 * @brief Binary output stream for files.
 */
export class FileOutputStream
{
public:
    /// Opens a file for writing (binary).
    explicit FileOutputStream(const std::string& path)
      : out_{ path, std::ios::binary }
    {
        if (!out_) throw std::runtime_error("Cannot open file for writing");
    }

    /// Move constructor
    FileOutputStream(FileOutputStream&& other) noexcept
      : out_(std::move(other.out_)) {}

    /// Move assignment
    FileOutputStream& operator=(FileOutputStream&& other) noexcept {
        if (this != &other) out_ = std::move(other.out_);
        return *this;
    }

    FileOutputStream(const FileOutputStream&) = delete;
    FileOutputStream& operator=(const FileOutputStream&) = delete;

    /// Write n bytes from data.
    void write(const char* data, std::size_t n)
    {
        out_.write(data, n);
        if (!out_) throw std::runtime_error("Write error");
    }

    /// Write a std::span<std::byte>.
    void write(std::span<const std::byte> data)
    {
        write(reinterpret_cast<const char*>(data.data()), data.size());
    }

    /// Write a std::span<char>.
    void write(std::span<const char> data)
    {
        write(data.data(), data.size());
    }

    /// Flush the stream.
    void flush()
    {
        out_.flush();
        if (!out_) throw std::runtime_error("Flush error");
    }

    /// Set the position in the stream.
    void seekp(std::streampos pos)
    {
        out_.seekp(pos);
        if (!out_) throw std::runtime_error("seekp error");
    }

    /// Get the current position in the stream.
    [[nodiscard]] std::streampos tellp()
    {
        return out_.tellp();
    }

    ~FileOutputStream() = default;

private:
    std::ofstream out_;
};

/**
 * @brief Binary input stream for files.
 */
export class FileInputStream
{
public:
    /// Opens a file for reading (binary).
    explicit FileInputStream(const std::string& path)
      : in_{ path, std::ios::binary }
    {
        if (!in_) throw std::runtime_error("Cannot open file for reading");
    }

    /// Move constructor
    FileInputStream(FileInputStream&& other) noexcept
      : in_(std::move(other.in_)) {}

    /// Move assignment
    FileInputStream& operator=(FileInputStream&& other) noexcept {
        if (this != &other) in_ = std::move(other.in_);
        return *this;
    }

    FileInputStream(const FileInputStream&) = delete;
    FileInputStream& operator=(const FileInputStream&) = delete;

    /// Read up to n bytes into data, return the number of bytes read.
    std::size_t read(char* data, std::size_t n)
    {
        in_.read(data, static_cast<std::streamsize>(n));
        if (in_.bad()) throw std::runtime_error("Read error");
        return static_cast<std::size_t>(in_.gcount());
    }

    /// Read into a std::span<std::byte>.
    std::size_t read(std::span<std::byte> data)
    {
        return read(reinterpret_cast<char*>(data.data()), data.size());
    }

    /// Read into a std::span<char>.
    std::size_t read(std::span<char> data)
    {
        return read(data.data(), data.size());
    }

    /// Set the position in the stream.
    void seekg(std::streampos pos)
    {
        in_.seekg(pos);
        if (!in_) throw std::runtime_error("seekg error");
    }

    /// Get the current position in the stream.
    [[nodiscard]] std::streampos tellg()
    {
        return in_.tellg();
    }

    /// Return true if end-of-file is reached.
    [[nodiscard]] bool eof() const noexcept
    {
        return in_.eof();
    }

    ~FileInputStream() = default;

private:
    std::ifstream in_;
};
static_assert(InputStream<FileInputStream>);
static_assert(OutputStream<FileOutputStream>);
} // namespace modern_io