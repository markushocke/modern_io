module;

#ifndef _MSC_VER
#include <cstddef>
#include <memory>
#include <memory_resource>
#include <vector>
#endif

export module modern_io.connection_arena;

#ifdef _MSC_VER
import <cstddef>;
import <memory>;
import <memory_resource>;
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

} // namespace modern::io

export namespace modern_io {

using modern::io::ConnectionArena;
using modern::io::ConnectionArenaHandle;
using modern::io::ConnectionArenaSettings;
using modern::io::make_connection_arena;

} // namespace modern_io