// modern_io_concepts.ixx
module;

import <cstddef>;
import <concepts>;
import <string>;
import <vector>;
import <stdexcept>;
import <fstream>;
import <cstdint>;
import <cstring>;
import <span>;
import <type_traits>;
import <utility>;
import <limits>;
import <bit>;
import <future>;

export module modern_io:concepts;
namespace modern_io
{

/**
 * @brief OutputStream concept for sinks that can write bytes and flush.
 */
export
template<typename S>
concept OutputStream = requires(S s, const char* ptr, std::size_t n, std::span<std::byte> bspan, std::span<char> cspan) {
    { s.write(ptr, n) } -> std::same_as<void>;
    { s.write(bspan) } -> std::same_as<void>;
    { s.write(cspan) } -> std::same_as<void>;
    { s.flush()    } -> std::same_as<void>;
};

/**
 * @brief InputStream concept for sources that can read bytes.
 */
export
template<typename S>
concept InputStream = requires(S s, char* ptr, std::size_t n, std::span<std::byte> bspan, std::span<char> cspan) {
    { s.read(ptr, n) } -> std::convertible_to<std::size_t>;
    { s.read(bspan) } -> std::convertible_to<std::size_t>;
    { s.read(cspan) } -> std::convertible_to<std::size_t>;
    { s.eof() } -> std::same_as<bool>;
};

export
template<typename S>
concept AsyncOutputStream = requires(S s, const char* ptr, std::size_t n, std::span<const std::byte> bspan, std::span<const char> cspan) {
    { s.write_async(ptr, n) } -> std::same_as<std::future<void>>;
    { s.write_async(bspan) } -> std::same_as<std::future<void>>;
    { s.write_async(cspan) } -> std::same_as<std::future<void>>;
    { s.flush_async() }     -> std::same_as<std::future<void>>;
};

export
template<typename S>
concept AsyncInputStream = requires(S s, char* ptr, std::size_t n, std::span<std::byte> bspan, std::span<char> cspan) {
    { s.read_async(ptr, n) } -> std::same_as<std::future<std::size_t>>;
    { s.read_async(bspan) }  -> std::same_as<std::future<std::size_t>>;
    { s.read_async(cspan) }  -> std::same_as<std::future<std::size_t>>;
    { s.eof_async() }        -> std::same_as<std::future<bool>>;
};


} // namespace modern_io