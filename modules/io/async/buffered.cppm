module;

#include <system_error>

#ifndef _MSC_VER
#include <vector>
#include <memory_resource>
#include <cstring>
#include <span>
#include <expected>
#include <memory>
#include <coroutine>
#endif

export module modern_io.async_buffered;
import modern_io.concepts;
export import modern_io.connection_arena;
import modern_io.task;

#ifdef _MSC_VER
import <vector>;
import <memory_resource>;
import <cstring>;
import <span>;
import <expected>;
import <coroutine>;
#endif

namespace modern::io
{

// AsyncBufferedOutputStream.
export
template<AsyncOutputStream S, std::size_t BufSize = 8192>
class AsyncBufferedOutputStream
{
public:
    explicit AsyncBufferedOutputStream(S sink, std::pmr::memory_resource* resource = std::pmr::get_default_resource())
        : sink_(std::move(sink)), buffer_(resource), pos_(0) {
        buffer_.resize(BufSize);
    }

    explicit AsyncBufferedOutputStream(S sink, ConnectionArena& arena)
        : AsyncBufferedOutputStream(std::move(sink), arena.memory_resource()) {}

    ExpectedTask<std::size_t> write_async(const char* data, std::size_t n) {
        std::size_t written = 0;
        while (written < n) {
            std::size_t room = BufSize - pos_;
            if (room == 0) {
                auto r = co_await sink_.write_async(buffer_.data(), pos_);
                if (!r) co_return std::unexpected(r.error());
                if (*r != pos_) co_return std::unexpected(std::make_error_code(std::errc::io_error));
                pos_ = 0;
                room = BufSize;
            }
            std::size_t chunk = std::min(room, n - written);
            std::memcpy(buffer_.data() + pos_, data + written, chunk);
            pos_ += chunk;
            written += chunk;
        }
        co_return std::expected<std::size_t, std::error_code>{written};
    }

    ExpectedTask<std::size_t> write_async(std::span<const std::byte> data) {
        co_return co_await write_async(reinterpret_cast<const char*>(data.data()), data.size());
    }
    ExpectedTask<std::size_t> write_async(std::span<const char> data) {
        co_return co_await write_async(data.data(), data.size());
    }

    // Synchronous fast path that preserves std::expected.
    std::expected<void, std::error_code> flush() {
        if (pos_ == 0) {
            auto fr = sink_.flush();
            if (!fr) return fr;
            return {};
        }
        // Attempt an immediate drain only when the awaiter is ready.
        auto aw = sink_.write_async(buffer_.data(), pos_);
        if (!aw.await_ready()) {
            return std::unexpected(std::make_error_code(std::errc::operation_would_block));
        }
        auto r = aw.await_resume();
        if (!r) return std::unexpected(r.error());
        if (*r != pos_) return std::unexpected(std::make_error_code(std::errc::io_error));
        pos_ = 0;
        auto fr = sink_.flush();
        if (!fr) return fr;
        return {};
    }

    ExpectedTask<void> flush_async() {
        if (pos_ == 0) {
            auto fr = sink_.flush();
            if (!fr) co_return std::unexpected(fr.error());
            co_return {};
        }
        auto r = co_await sink_.write_async(buffer_.data(), pos_);
        if (!r) co_return std::unexpected(r.error());
        if (*r != pos_) co_return std::unexpected(std::make_error_code(std::errc::io_error));
        pos_ = 0;
        auto fr = sink_.flush();
        if (!fr) co_return std::unexpected(fr.error());
        co_return {};
    }

private:
    S                  sink_;
    std::pmr::vector<char> buffer_;
    std::size_t        pos_;
};

// AsyncBufferedInputStream.
export
template<AsyncInputStream S, std::size_t BufSize = 8192>
class AsyncBufferedInputStream
{
public:
    explicit AsyncBufferedInputStream(S source, std::pmr::memory_resource* resource = std::pmr::get_default_resource())
        : source_(std::move(source)), buffer_(resource), pos_(0), end_(0) {
        buffer_.resize(BufSize);
    }

    explicit AsyncBufferedInputStream(S source, ConnectionArena& arena)
        : AsyncBufferedInputStream(std::move(source), arena.memory_resource()) {}

    ExpectedTask<std::size_t> read_async(char* out, std::size_t n) {
        std::size_t total = 0;
        while (total < n) {
            if (pos_ == end_) {
                auto r = co_await source_.read_async(buffer_.data(), BufSize);
                if (!r) co_return std::unexpected(r.error());
                end_ = *r;
                pos_ = 0;
                if (end_ == 0) break; // EOF
            }
            std::size_t avail = end_ - pos_;
            if (avail == 0) continue;
            std::size_t chunk = std::min(avail, n - total);
            std::memcpy(out + total, buffer_.data() + pos_, chunk);
            pos_ += chunk;
            total += chunk;
        }
        co_return std::expected<std::size_t, std::error_code>{total};
    }

    ExpectedTask<std::size_t> read_async(std::span<std::byte> data) {
        co_return co_await read_async(reinterpret_cast<char*>(data.data()), data.size());
    }
    ExpectedTask<std::size_t> read_async(std::span<char> data) {
        co_return co_await read_async(data.data(), data.size());
    }

    std::expected<bool, std::error_code> eof() {
        auto e = source_.eof();
        if (!e) return e;
        return std::expected<bool, std::error_code>{ (pos_ == end_) && e.value() };
    }

private:
    S                  source_;
    std::pmr::vector<char> buffer_;
    std::size_t        pos_;
    std::size_t        end_;
};

// CTAD helpers.
template<typename Stream>
AsyncBufferedOutputStream(Stream&&) -> AsyncBufferedOutputStream<std::decay_t<Stream>>;

template<typename Stream>
AsyncBufferedInputStream(Stream&&) -> AsyncBufferedInputStream<std::decay_t<Stream>>;

} // namespace modern::io

export namespace modern_io {

using modern::io::AsyncBufferedInputStream;
using modern::io::AsyncBufferedOutputStream;

} // namespace modern_io