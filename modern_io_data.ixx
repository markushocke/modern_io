// modern_io_data.ixx
module;
#include <cstddef>
#include <concepts>
#include <string>
#include <vector>
#include <stdexcept>
#include <fstream>
#include <stdint.h>
#include <cstdint>
#include <cstring>
#include <span>
#include <type_traits>
#include <utility>
#include <limits>
#include <bit>

export module modern_io:data;
import :concepts;
namespace modern_io
{

/**
 * @brief Binary output stream for primitive types and strings.
 */
export
template<OutputStream S>
class DataOutputStream
{
public:
    /// Constructor with sink and endian.
    explicit DataOutputStream(S sink, std::endian order)
      : sink_(std::move(sink))
      , order_(order)
    {}

    /// Move constructor
    DataOutputStream(DataOutputStream&& other) noexcept
      : sink_(std::move(other.sink_)), order_(other.order_) {}

    /// Move assignment
    DataOutputStream& operator=(DataOutputStream&& other) noexcept {
        if (this != &other) {
            sink_ = std::move(other.sink_);
            order_ = other.order_;
        }
        return *this;
    }

    DataOutputStream(const DataOutputStream&) = delete;
    DataOutputStream& operator=(const DataOutputStream&) = delete;

    /// Write a std::vector<std::byte>.
    void write_bytes(const std::vector<std::byte>& data)
    {
        sink_.write(std::span<const std::byte>(data.data(), data.size()));
    }

    /// Write a std::span<std::byte>.
    void write_bytes(std::span<const std::byte> data)
    {
        sink_.write(data);
    }

    /// Flush the stream.
    void flush()
    {
        sink_.flush();
    }

    /// Write an int32_t.
    void write_int32(int32_t v)
    {
        std::byte buf[4];
        if (order_ == std::endian::big)
        {
            buf[0] = std::byte((v >> 24) & 0xFF);
            buf[1] = std::byte((v >> 16) & 0xFF);
            buf[2] = std::byte((v >>  8) & 0xFF);
            buf[3] = std::byte((v      ) & 0xFF);
        }
        else
        {
            buf[3] = std::byte((v >> 24) & 0xFF);
            buf[2] = std::byte((v >> 16) & 0xFF);
            buf[1] = std::byte((v >>  8) & 0xFF);
            buf[0] = std::byte((v      ) & 0xFF);
        }
        sink_.write(std::span<const std::byte>(buf, 4));
    }

    /// Write a uint32_t.
    void write_uint32(uint32_t v)
    {
        write_int32(static_cast<int32_t>(v));
    }

    /// Write an int64_t.
    void write_int64(int64_t v)
    {
        std::byte buf[8];
        if (order_ == std::endian::big)
        {
            for (int i = 0; i < 8; ++i)
                buf[i] = std::byte((v >> (56 - 8 * i)) & 0xFF);
        }
        else
        {
            for (int i = 0; i < 8; ++i)
                buf[7 - i] = std::byte((v >> (56 - 8 * i)) & 0xFF);
        }
        sink_.write(std::span<const std::byte>(buf, 8));
    }

    /// Write a uint64_t.
    void write_uint64(uint64_t v)
    {
        write_int64(static_cast<int64_t>(v));
    }

    /// Write a float.
    void write_float(float v)
    {
        static_assert(sizeof(float) == 4);
        uint32_t as_int;
        std::memcpy(&as_int, &v, 4);
        write_uint32(as_int);
    }

    /// Write a double.
    void write_double(double v)
    {
        static_assert(sizeof(double) == 8);
        uint64_t as_int;
        std::memcpy(&as_int, &v, 8);
        write_uint64(as_int);
    }

    /// Write a string (length + data).
    void write_string(const std::string& s)
    {
        write_int32(static_cast<int32_t>(s.size()));
        sink_.write(std::span<const char>(s.data(), s.size()));
    }

private:
    S             sink_;
    std::endian   order_;
};

// --- DataInputStream ---
/**
 * @brief Binary input stream for primitive types and strings.
 */
export
template<InputStream S>
class DataInputStream
{
public:
    /// Constructor with source and endian.
    explicit DataInputStream(S source, std::endian order)
      : source_(std::move(source))
      , order_(order)
    {}

    /// Move constructor
    DataInputStream(DataInputStream&& other) noexcept
      : source_(std::move(other.source_)), order_(other.order_) {}

    /// Move assignment
    DataInputStream& operator=(DataInputStream&& other) noexcept {
        if (this != &other) {
            source_ = std::move(other.source_);
            order_ = other.order_;
        }
        return *this;
    }

    DataInputStream(const DataInputStream&) = delete;
    DataInputStream& operator=(const DataInputStream&) = delete;

    /// Read n bytes and return as std::vector<std::byte>.
    [[nodiscard]] std::vector<std::byte> read_bytes(std::size_t n)
    {
        std::vector<std::byte> buf(n);
        std::size_t got = source_.read(std::span<std::byte>(buf.data(), n));
        if (got != n)
            throw std::runtime_error("Unexpected EOF");
        return buf;
    }

    /// Read an int32_t.
    [[nodiscard]] int32_t read_int32()
    {
        auto buf = read_bytes(4);
        auto ptr = reinterpret_cast<const uint8_t*>(buf.data());
        int32_t v = 0;
        if (order_ == std::endian::big)
        {
            v = (ptr[0] << 24)
              | (ptr[1] << 16)
              | (ptr[2] <<  8)
              | (ptr[3]      );
        }
        else
        {
            v = (ptr[3] << 24)
              | (ptr[2] << 16)
              | (ptr[1] <<  8)
              | (ptr[0]      );
        }
        return v;
    }

    /// Read a uint32_t.
    [[nodiscard]] uint32_t read_uint32()
    {
        return static_cast<uint32_t>(read_int32());
    }

    /// Read an int64_t.
    [[nodiscard]] int64_t read_int64()
    {
        auto buf = read_bytes(8);
        auto ptr = reinterpret_cast<const uint8_t*>(buf.data());
        int64_t v = 0;
        if (order_ == std::endian::big)
        {
            for (int i = 0; i < 8; ++i)
                v |= static_cast<int64_t>(ptr[i]) << (56 - 8 * i);
        }
        else
        {
            for (int i = 0; i < 8; ++i)
                v |= static_cast<int64_t>(ptr[7 - i]) << (56 - 8 * i);
        }
        return v;
    }

    /// Read a uint64_t.
    [[nodiscard]] uint64_t read_uint64()
    {
        return static_cast<uint64_t>(read_int64());
    }

    /// Read a float.
    [[nodiscard]] float read_float()
    {
        uint32_t as_int = read_uint32();
        float v;
        std::memcpy(&v, &as_int, 4);
        return v;
    }

    /// Read a double.
    [[nodiscard]] double read_double()
    {
        uint64_t as_int = read_uint64();
        double v;
        std::memcpy(&v, &as_int, 8);
        return v;
    }

    /// Read a string (length + data).
    [[nodiscard]] std::string read_string()
    {
        int32_t len = read_int32();
        if (len < 0 || len > std::numeric_limits<int32_t>::max())
            throw std::runtime_error("Invalid string length");
        auto buf = read_bytes(static_cast<std::size_t>(len));
        return std::string(reinterpret_cast<char*>(buf.data()), buf.size());
    }

    /// Return true if end-of-file is reached.
    [[nodiscard]] bool eof() const noexcept
    {
        return source_.eof();
    }

private:
    S             source_;
    std::endian   order_;
};

} // namespace modern_io