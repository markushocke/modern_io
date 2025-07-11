// modern_io_buffered.ixx
module;

#ifndef _MSC_VER
#include <vector>
#include <cstring>
#include <span>
#endif

export module modern_io:buffered;
import :concepts;

#ifdef _MSC_VER
import <vector>;
import <cstring>;
#endif
namespace modern_io
{

// ------------------------------------------------------------------------
// BufferedOutputStream<S, BufSize>
//   wraps any OutputStream<S> and buffers writes in memory
// ------------------------------------------------------------------------
/**
 * @brief Buffered output stream wrapper.
 */
export
template<OutputStream S, std::size_t BufSize = 8192>
class BufferedOutputStream
{
public:
    /// Constructor with sink.
    explicit BufferedOutputStream(S sink)
      : sink_(std::move(sink))
      , buffer_()
      , pos_(0)
    {
        buffer_.resize(BufSize);
    }

    /// Move constructor
    BufferedOutputStream(BufferedOutputStream&& other) noexcept
      : sink_(std::move(other.sink_)), buffer_(std::move(other.buffer_)), pos_(other.pos_)
    {
        other.pos_ = 0;
    }

    /// Move assignment
    BufferedOutputStream& operator=(BufferedOutputStream&& other) noexcept {
        if (this != &other) {
            flush();
            sink_ = std::move(other.sink_);
            buffer_ = std::move(other.buffer_);
            pos_ = other.pos_;
            other.pos_ = 0;
        }
        return *this;
    }

    BufferedOutputStream(const BufferedOutputStream&) = delete;
    BufferedOutputStream& operator=(const BufferedOutputStream&) = delete;

    /// Write up to BufSize bytes.
    void write(const char* data, std::size_t size)
    {
        std::size_t written = 0;
        while (written < size)
        {
            std::size_t chunk = std::min(BufSize - pos_, size - written);
            std::memcpy(buffer_.data() + pos_,
                        data + written,
                        chunk);
            pos_ += chunk;
            written += chunk;
            if (pos_ == BufSize)
            {
                flush_buffer();
            }
        }
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

    /// Flush all remaining data.
    void flush()
    {
        flush_buffer();
        sink_.flush();
    }

    ~BufferedOutputStream() noexcept
    {
        try { flush(); } catch (...) {}
    }

private:
    void flush_buffer()
    {
        if (pos_ > 0)
        {
            sink_.write(std::span<const char>(buffer_.data(), pos_));
            pos_ = 0;
        }
    }

    S                  sink_;
    std::vector<char>  buffer_;
    std::size_t        pos_;
};

// ------------------------------------------------------------------------
// BufferedInputStream<S, BufSize>
//   wraps any InputStream<S> and buffers reads in memory
// ------------------------------------------------------------------------
/**
 * @brief Buffered input stream wrapper.
 */
export
template<InputStream S, std::size_t BufSize = 8192>
class BufferedInputStream
{
public:
    /// Constructor with source.
    explicit BufferedInputStream(S source)
      : source_(std::move(source))
      , buffer_()
      , pos_(0)
      , end_(0)
    {
        buffer_.resize(BufSize);
    }

    /// Move constructor
    BufferedInputStream(BufferedInputStream&& other) noexcept
      : source_(std::move(other.source_)), buffer_(std::move(other.buffer_)), pos_(other.pos_), end_(other.end_)
    {
        other.pos_ = 0;
        other.end_ = 0;
    }

    /// Move assignment
    BufferedInputStream& operator=(BufferedInputStream&& other) noexcept {
        if (this != &other) {
            source_ = std::move(other.source_);
            buffer_ = std::move(other.buffer_);
            pos_ = other.pos_;
            end_ = other.end_;
            other.pos_ = 0;
            other.end_ = 0;
        }
        return *this;
    }

    BufferedInputStream(const BufferedInputStream&) = delete;
    BufferedInputStream& operator=(const BufferedInputStream&) = delete;

    /// Read up to size bytes into data, return the number of bytes read.
    std::size_t read(char* data, std::size_t size)
    {
        std::size_t total = 0;
        while (total < size)
        {
            if (pos_ == end_)
            {
                // refill buffer
                end_ = source_.read(std::span<char>(buffer_.data(), BufSize));
                pos_ = 0;
                if (end_ == 0)
                {
                    break;  // EOF
                }
            }
            std::size_t chunk = std::min(end_ - pos_, size - total);
            std::memcpy(data + total,
                        buffer_.data() + pos_,
                        chunk);
            pos_   += chunk;
            total  += chunk;
        }
        return total;
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

    /// Return true if end-of-file is reached.
    [[nodiscard]] bool eof() const noexcept
    {
        return (pos_ == end_) && source_.eof();
    }

private:
    S                  source_;
    std::vector<char>  buffer_;
    std::size_t        pos_;
    std::size_t        end_;
};
} // namespace modern_io