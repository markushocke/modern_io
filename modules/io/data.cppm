// modern_io_data.ixx
module;

#ifndef _MSC_VER
#include <vector>
#include <cstring>
#include <limits>
#include <bit>
#include <span>
#include <stdint.h>
#include <string>
#include <stdexcept>
#include <expected>
#endif

export module modern_io.data;
import modern_io.concepts;
import modern_io.exceptions;

#ifdef _MSC_VER
import <vector>;
import <cstring>;
import <limits>;
import <bit>;
import <span>;
import <string>;
import <expected>;
#endif

namespace modern::io
{

/**
 * @brief Binary output stream for primitive types and strings (synchronous).
 */
export
template<OutputStream S>
class DataOutputStream
{
public:
    explicit DataOutputStream(S sink, std::endian order = std::endian::big)
      : sink_(std::move(sink))
      , order_(order)
    {}

    DataOutputStream(DataOutputStream&& other) noexcept
      : sink_(std::move(other.sink_)), order_(other.order_) {}

    DataOutputStream& operator=(DataOutputStream&& other) noexcept {
        if (this != &other) {
            sink_ = std::move(other.sink_);
            order_ = other.order_;
        }
        return *this;
    }

    DataOutputStream(const DataOutputStream&) = delete;
    DataOutputStream& operator=(const DataOutputStream&) = delete;

    void write_bytes(const std::vector<std::byte>& data)
    {
        sink_.write(std::span<const std::byte>(data.data(), data.size()));
    }

    void write_bytes(std::span<const std::byte> data)
    {
        sink_.write(data);
    }

    void flush()
    {
        sink_.flush();
    }

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

    void write_uint32(uint32_t v) { write_int32(static_cast<int32_t>(v)); }

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

    void write_uint64(uint64_t v) { write_int64(static_cast<int64_t>(v)); }

    void write_float(float v)
    {
        static_assert(sizeof(float) == 4);
        uint32_t as_int;
        std::memcpy(&as_int, &v, 4);
        write_uint32(as_int);
    }

    void write_double(double v)
    {
        static_assert(sizeof(double) == 8);
        uint64_t as_int;
        std::memcpy(&as_int, &v, 8);
        write_uint64(as_int);
    }

    void write_string(const std::string& s)
    {
        write_int32(static_cast<int32_t>(s.size()));
        sink_.write(std::span<const char>(s.data(), s.size()));
    }

private:
    S             sink_;
    std::endian   order_;
};


// --- DataInputStream (synchronous) ---
export
template<InputStream S>
class DataInputStream
{
public:
    explicit DataInputStream(S source, std::endian order = std::endian::big)
      : source_(std::move(source))
      , order_(order)
    {}

    DataInputStream(DataInputStream&& other) noexcept
      : source_(std::move(other.source_)), order_(other.order_) {}

    DataInputStream& operator=(DataInputStream&& other) noexcept {
        if (this != &other) {
            source_ = std::move(other.source_);
            order_ = other.order_;
        }
        return *this;
    }

    DataInputStream(const DataInputStream&) = delete;
    DataInputStream& operator=(const DataInputStream&) = delete;

    [[nodiscard]] std::vector<std::byte> read_bytes(std::size_t n)
    {
        std::vector<std::byte> buf(n);
        std::size_t got = source_.read(std::span<std::byte>(buf.data(), n));
        if (got != n)
            throw UnexpectedEOFException("Failed to read " + std::to_string(n) + " bytes, got " + std::to_string(got));
        return buf;
    }

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

    [[nodiscard]] uint32_t read_uint32() { return static_cast<uint32_t>(read_int32()); }

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

    [[nodiscard]] uint64_t read_uint64() { return static_cast<uint64_t>(read_int64()); }

    [[nodiscard]] float read_float()
    {
        uint32_t as_int = read_uint32();
        float v;
        std::memcpy(&v, &as_int, 4);
        return v;
    }

    [[nodiscard]] double read_double()
    {
        uint64_t as_int = read_uint64();
        double v;
        std::memcpy(&v, &as_int, 8);
        return v;
    }

    [[nodiscard]] std::string read_string()
    {
        int32_t len = read_int32();
        if (len < 0)
            throw DataFormatException("Invalid string length: " + std::to_string(len));
        if (static_cast<uint32_t>(len) > 100'000'000)  // Sanity check: 100MB limit
            throw DataFormatException("String length too large: " + std::to_string(len));
        auto buf = read_bytes(static_cast<std::size_t>(len));
        return std::string(reinterpret_cast<char*>(buf.data()), buf.size());
    }

    [[nodiscard]] bool eof() const noexcept
    {
        return source_.eof();
    }

private:
    S             source_;
    std::endian   order_;
};

// Deduction Guides (nur sync)
template<typename Stream>
DataOutputStream(Stream&&, std::endian) -> DataOutputStream<std::decay_t<Stream>>;

template<typename Stream>
DataInputStream(Stream&&, std::endian) -> DataInputStream<std::decay_t<Stream>>;

} // namespace modern::io

export namespace modern_io {

using modern::io::DataInputStream;
using modern::io::DataOutputStream;

} // namespace modern_io