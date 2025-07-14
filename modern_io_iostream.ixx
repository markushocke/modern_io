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
/**
 * @brief Adapter class that wraps a std::ostream and exposes it as a modern_io OutputStream.
 *
 * This allows you to use any standard output stream (such as std::cout, std::ofstream, etc.)
 * wherever a modern_io OutputStream is expected.
 *
 * Example:
 * @code
 * std::ofstream file("output.txt");
 * OstreamOutputStream out(file);
 * out.write("Hello", 5);
 * out.flush();
 * @endcode
 */
export class OstreamOutputStream {
public:
    /**
     * @brief Constructs the adapter from a reference to a std::ostream.
     * @param out The output stream to wrap.
     */
    explicit OstreamOutputStream(std::ostream& out) : out_(out) {}

    /**
     * @brief Writes a block of data to the underlying std::ostream.
     * @param ptr Pointer to the data to write.
     * @param n Number of bytes to write.
     */
    void write(const char* ptr, std::size_t n) {
        out_.write(ptr, n);
    }

    /**
     * @brief Writes a span of bytes to the underlying std::ostream.
     * @param bspan Span of bytes to write.
     */
    void write(std::span<const std::byte> bspan) {
        out_.write(reinterpret_cast<const char*>(bspan.data()), bspan.size());
    }

    /**
     * @brief Writes a span of chars to the underlying std::ostream.
     * @param cspan Span of chars to write.
     */
    void write(std::span<const char> cspan) {
        out_.write(cspan.data(), cspan.size());
    }

    /**
     * @brief Flushes the underlying std::ostream.
     *
     * This ensures that all buffered output is written to the destination.
     */
    void flush() {
        out_.flush();
    }

private:
    std::ostream& out_; ///< Reference to the wrapped std::ostream.
};

// -----------------------------------------------------------------------------
// InputStream Adapter: std::istream -> InputStream (standard reading)
// -----------------------------------------------------------------------------
/**
 * @brief Adapter class that wraps a std::istream and exposes it as a modern_io InputStream.
 *
 * This allows you to use any standard input stream (such as std::cin, std::ifstream, etc.)
 * wherever a modern_io InputStream is expected.
 *
 * Example:
 * @code
 * std::ifstream file("input.txt");
 * IstreamInputStream in(file);
 * char buf[128];
 * std::size_t n = in.read(buf, sizeof(buf));
 * @endcode
 */
export class IstreamInputStream {
public:
    /**
     * @brief Constructs the adapter from a reference to a std::istream.
     * @param in The input stream to wrap.
     */
    explicit IstreamInputStream(std::istream& in) : in_(in) {}

    /**
     * @brief Reads up to n bytes from the underlying std::istream into the buffer.
     * @param ptr Pointer to the buffer to fill.
     * @param n Maximum number of bytes to read.
     * @return Number of bytes actually read.
     */
    std::size_t read(char* ptr, std::size_t n) {
        in_.read(ptr, n);
        return static_cast<std::size_t>(in_.gcount());
    }

    /**
     * @brief Reads into a span of bytes.
     * @param bspan Span of bytes to fill.
     * @return Number of bytes actually read.
     */
    std::size_t read(std::span<std::byte> bspan) {
        return read(reinterpret_cast<char*>(bspan.data()), bspan.size());
    }

    /**
     * @brief Reads into a span of chars.
     * @param cspan Span of chars to fill.
     * @return Number of bytes actually read.
     */
    std::size_t read(std::span<char> cspan) {
        return read(cspan.data(), cspan.size());
    }

    /**
     * @brief Checks for end-of-file (EOF) on the underlying std::istream.
     * @return True if EOF has been reached, false otherwise.
     */
    bool eof() const {
        return in_.eof();
    }

private:
    std::istream& in_; ///< Reference to the wrapped std::istream.
};

// -----------------------------------------------------------------------------
// Lazy InputStream: std::istreambuf_iterator based (lazy/streaming)
// -----------------------------------------------------------------------------
/**
 * @brief Adapter class that wraps a std::istream and provides lazy, iterator-based reading.
 *
 * This class reads data one character at a time using std::istreambuf_iterator,
 * which can be more efficient for certain streaming scenarios.
 *
 * Example:
 * @code
 * std::ifstream file("input.txt");
 * LazyIstreamInputStream lazy_in(file);
 * char buf[64];
 * std::size_t n = lazy_in.read(buf, sizeof(buf));
 * @endcode
 */
export class LazyIstreamInputStream {
public:
    /**
     * @brief Constructs the adapter from a reference to a std::istream.
     * @param in The input stream to wrap.
     */
    explicit LazyIstreamInputStream(std::istream& in)
        : current_(in), end_() {}

    /**
     * @brief Reads up to n bytes from the stream using an iterator.
     * @param ptr Pointer to the buffer to fill.
     * @param n Maximum number of bytes to read.
     * @return Number of bytes actually read.
     */
    std::size_t read(char* ptr, std::size_t n) {
        std::size_t count = 0;
        while (count < n && current_ != end_) {
            ptr[count++] = *current_++;
        }
        return count;
    }

    /**
     * @brief Reads into a span of bytes using the iterator.
     * @param bspan Span of bytes to fill.
     * @return Number of bytes actually read.
     */
    std::size_t read(std::span<std::byte> bspan) {
        return read(reinterpret_cast<char*>(bspan.data()), bspan.size());
    }

    /**
     * @brief Reads into a span of chars using the iterator.
     * @param cspan Span of chars to fill.
     * @return Number of bytes actually read.
     */
    std::size_t read(std::span<char> cspan) {
        return read(cspan.data(), cspan.size());
    }

    /**
     * @brief Checks for end-of-file (EOF) using the iterator.
     * @return True if the iterator has reached the end, false otherwise.
     */
    bool eof() const {
        return current_ == end_;
    }

private:
    std::istreambuf_iterator<char> current_; ///< Iterator pointing to the current position in the stream.
    std::istreambuf_iterator<char> end_;     ///< End iterator.
};

// Compile-time checks to ensure the adapters satisfy the modern_io concepts.
static_assert(InputStream<IstreamInputStream>);
static_assert(OutputStream<OstreamOutputStream>);
static_assert(InputStream<LazyIstreamInputStream>);

} // namespace modern_io
