// modern_io_buffered.ixx
module;

#include <system_error>

#ifndef _MSC_VER
#include <vector>
#include <memory_resource>
#include <cstring>
#include <span>
#endif

export module modern_io.buffered;
import modern_io.concepts;
export import modern_io.connection_arena;
import modern_io.exceptions;
// (imported above)

#ifdef _MSC_VER
import <vector>;
import <memory_resource>;
import <cstring>;
import <span>;
#endif

namespace modern_io
{

// ------------------------------------------------------------------------
// BufferedOutputStream<S, BufSize>  (synchron)
// ------------------------------------------------------------------------
export
template<OutputStream S, std::size_t BufSize = 8192>
class BufferedOutputStream
{
public:
        explicit BufferedOutputStream(S sink, std::pmr::memory_resource* resource = std::pmr::get_default_resource())
            : sink_(std::move(sink)), buffer_(resource), pos_(0)
    {
        buffer_.resize(BufSize);
    }

    explicit BufferedOutputStream(S sink, ConnectionArena& arena)
        : BufferedOutputStream(std::move(sink), arena.memory_resource()) {}

    BufferedOutputStream(BufferedOutputStream&& other) noexcept
      : sink_(std::move(other.sink_)), buffer_(std::move(other.buffer_)), pos_(other.pos_)
    {
        other.pos_ = 0;
    }

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

    void write(const char* data, std::size_t size)
    {
        std::size_t written = 0;
        while (written < size)
        {
            std::size_t chunk = std::min(BufSize - pos_, size - written);
            std::memcpy(buffer_.data() + pos_, data + written, chunk);
            pos_ += chunk;
            written += chunk;
            if (pos_ == BufSize) flush_buffer();
        }
    }

    void write(std::span<const std::byte> data)
    {
        write(reinterpret_cast<const char*>(data.data()), data.size());
    }

    void write(std::span<const char> data)
    {
        write(data.data(), data.size());
    }

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
    std::pmr::vector<char> buffer_;
    std::size_t        pos_;
};

// ------------------------------------------------------------------------
// BufferedInputStream<S, BufSize>  (synchron)
// ------------------------------------------------------------------------
export
template<InputStream S, std::size_t BufSize = 8192>
class BufferedInputStream
{
public:
        explicit BufferedInputStream(S source, std::pmr::memory_resource* resource = std::pmr::get_default_resource())
            : source_(std::move(source)), buffer_(resource), pos_(0), end_(0)
    {
        buffer_.resize(BufSize);
    }

    explicit BufferedInputStream(S source, ConnectionArena& arena)
        : BufferedInputStream(std::move(source), arena.memory_resource()) {}

    BufferedInputStream(BufferedInputStream&& other) noexcept
      : source_(std::move(other.source_)), buffer_(std::move(other.buffer_)), pos_(other.pos_), end_(other.end_)
    {
        other.pos_ = 0;
        other.end_ = 0;
    }

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

    std::size_t read(char* data, std::size_t size)
    {
        std::size_t total = 0;
        while (total < size)
        {
            if (pos_ == end_)
            {
                end_ = source_.read(std::span<char>(buffer_.data(), BufSize));
                pos_ = 0;
                if (end_ == 0) break; // EOF
            }
            std::size_t chunk = std::min(end_ - pos_, size - total);
            std::memcpy(data + total, buffer_.data() + pos_, chunk);
            pos_ += chunk;
            total += chunk;
        }
        return total;
    }

    std::size_t read(std::span<std::byte> data)
    {
        return read(reinterpret_cast<char*>(data.data()), data.size());
    }

    std::size_t read(std::span<char> data)
    {
        return read(data.data(), data.size());
    }

    [[nodiscard]] bool eof() const noexcept
    {
        return (pos_ == end_) && source_.eof();
    }

private:
    S                  source_;
    std::pmr::vector<char> buffer_;
    std::size_t        pos_;
    std::size_t        end_;
};

template<typename Stream>
BufferedOutputStream(Stream&&) -> BufferedOutputStream<std::decay_t<Stream>>;

template<typename Stream>
BufferedInputStream(Stream&&) -> BufferedInputStream<std::decay_t<Stream>>;

    // No cross-module static_asserts here: Buffered templates are generic and
    // should be validated with concrete adapter tests instead of compile-time
    // assertions that reference other modules.

} // namespace modern_io
