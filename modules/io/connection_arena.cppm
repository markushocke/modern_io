module;

#ifndef _MSC_VER
#include <cstddef>
#include <memory>
#include <memory_resource>
#include <span>
#include <vector>
#endif

export module modern_io.connection_arena;

#ifdef _MSC_VER
import <cstddef>;
import <memory>;
import <memory_resource>;
import <span>;
import <vector>;
#endif

export namespace modern::io {

struct ConnectionArenaSettings {
    std::size_t initial_buffer_size = 8192;
    std::pmr::memory_resource* upstream = std::pmr::get_default_resource();
};

class ConnectionArena {
public:
    explicit ConnectionArena(
        std::size_t initial_buffer_size = 8192,
        std::pmr::memory_resource* upstream = std::pmr::get_default_resource())
        : initial_storage_(initial_buffer_size == 0 ? 1 : initial_buffer_size),
          resource_(initial_storage_.data(), initial_storage_.size(), upstream) {}

    ConnectionArena(const ConnectionArena&) = delete;
    ConnectionArena& operator=(const ConnectionArena&) = delete;
    ConnectionArena(ConnectionArena&&) = delete;
    ConnectionArena& operator=(ConnectionArena&&) = delete;

    [[nodiscard]] std::pmr::memory_resource* memory_resource() noexcept {
        return &resource_;
    }

    [[nodiscard]] std::size_t initial_buffer_size() const noexcept {
        return initial_storage_.size();
    }

    void reset() noexcept {
        resource_.release();
    }

private:
    std::vector<std::byte> initial_storage_;
    std::pmr::monotonic_buffer_resource resource_;
};

using ConnectionArenaHandle = std::shared_ptr<ConnectionArena>;

[[nodiscard]] inline ConnectionArenaHandle
make_connection_arena(ConnectionArenaSettings settings = {}) {
    return std::make_shared<ConnectionArena>(settings.initial_buffer_size, settings.upstream);
}

class OwnedByteBuffer {
public:
    explicit OwnedByteBuffer(
        std::size_t size,
        ConnectionArenaHandle arena = make_connection_arena())
        : arena_(arena ? std::move(arena) : make_connection_arena()),
          bytes_(arena_->memory_resource()) {
        bytes_.resize(size);
    }

    explicit OwnedByteBuffer(
        std::span<const std::byte> source,
        ConnectionArenaHandle arena = make_connection_arena())
        : arena_(arena ? std::move(arena) : make_connection_arena()),
          bytes_(source.begin(), source.end(), arena_->memory_resource()) {}

    OwnedByteBuffer(OwnedByteBuffer&&) noexcept = default;
    OwnedByteBuffer& operator=(OwnedByteBuffer&&) noexcept = default;
    OwnedByteBuffer(const OwnedByteBuffer&) = delete;
    OwnedByteBuffer& operator=(const OwnedByteBuffer&) = delete;

    [[nodiscard]] std::span<std::byte> bytes() noexcept { return bytes_; }
    [[nodiscard]] std::span<const std::byte> bytes() const noexcept { return bytes_; }
    [[nodiscard]] std::size_t size() const noexcept { return bytes_.size(); }
    [[nodiscard]] ConnectionArenaHandle arena() const noexcept { return arena_; }

private:
    ConnectionArenaHandle arena_;
    std::pmr::vector<std::byte> bytes_;
};

using SharedByteBuffer = std::shared_ptr<const OwnedByteBuffer>;

[[nodiscard]] inline SharedByteBuffer make_shared_byte_buffer(
    std::span<const std::byte> source,
    ConnectionArenaHandle arena = make_connection_arena()) {
    return std::make_shared<const OwnedByteBuffer>(source, std::move(arena));
}

} // namespace modern::io

export namespace modern_io {

using modern::io::ConnectionArena;
using modern::io::ConnectionArenaHandle;
using modern::io::ConnectionArenaSettings;
using modern::io::make_connection_arena;
using modern::io::OwnedByteBuffer;
using modern::io::SharedByteBuffer;
using modern::io::make_shared_byte_buffer;

} // namespace modern_io
