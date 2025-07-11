module;

#ifndef _MSC_VER
#include <ostream>
#include <istream>
#include <span>
#endif

export module modern_io:iostream;
import :concepts;

#ifdef _MSC_VER
import <ostream>;
import <istream>;
import <span>;
#endif

namespace modern_io
{



// -----------------------------------------------------------------------------
// OutputStream Adapter: std::ostream -> OutputStream
// -----------------------------------------------------------------------------
export class OstreamOutputStream {
public:
    explicit OstreamOutputStream(std::ostream& out) : out_(out) {}

    void write(const char* ptr, std::size_t n) {
        out_.write(ptr, n);
    }

    void write(std::span<const std::byte> bspan) {
        out_.write(reinterpret_cast<const char*>(bspan.data()), bspan.size());
    }

    void write(std::span<const char> cspan) {
        out_.write(cspan.data(), cspan.size());
    }

    void flush() {
        out_.flush();
    }

private:
    std::ostream& out_;
};

// -----------------------------------------------------------------------------
// InputStream Adapter: std::istream -> InputStream (Standard-Lesen)
// -----------------------------------------------------------------------------
export class IstreamInputStream {
public:
    explicit IstreamInputStream(std::istream& in) : in_(in) {}

    std::size_t read(char* ptr, std::size_t n) {
        in_.read(ptr, n);
        return static_cast<std::size_t>(in_.gcount());
    }

    std::size_t read(std::span<std::byte> bspan) {
        return read(reinterpret_cast<char*>(bspan.data()), bspan.size());
    }

    std::size_t read(std::span<char> cspan) {
        return read(cspan.data(), cspan.size());
    }

    bool eof() const {
        return in_.eof();
    }

private:
    std::istream& in_;
};

// -----------------------------------------------------------------------------
// Lazy InputStream: std::istreambuf_iterator basiert (Lazy/Streaming)
// -----------------------------------------------------------------------------
export class LazyIstreamInputStream {
public:
    explicit LazyIstreamInputStream(std::istream& in)
        : current_(in), end_() {}

    std::size_t read(char* ptr, std::size_t n) {
        std::size_t count = 0;
        while (count < n && current_ != end_) {
            ptr[count++] = *current_++;
        }
        return count;
    }

    std::size_t read(std::span<std::byte> bspan) {
        return read(reinterpret_cast<char*>(bspan.data()), bspan.size());
    }

    std::size_t read(std::span<char> cspan) {
        return read(cspan.data(), cspan.size());
    }

    bool eof() const {
        return current_ == end_;
    }

private:
    std::istreambuf_iterator<char> current_;
    std::istreambuf_iterator<char> end_;
};

static_assert(InputStream<IstreamInputStream>);
static_assert(OutputStream<OstreamOutputStream>);
static_assert(InputStream<LazyIstreamInputStream>);

}
