// modern_io_concepts.ixx
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

} // namespace modern_io