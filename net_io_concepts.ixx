module;

#include <concepts>
#include <cstddef>
#include <utility>

export module net_io_concepts;

namespace net_io_concepts
{
  /**
   * @brief Concept for readable types (must provide read).
   */
  export template<typename T>
  concept Readable = requires(T& t, char* buf, std::size_t n) {
    { t.read(buf, n) } -> std::convertible_to<std::size_t>;
  };

  /**
   * @brief Concept for writable types (must provide write).
   */
  export template<typename T>
  concept Writable = requires(T& t, const char* buf, std::size_t n) {
    { t.write(buf, n) } -> std::same_as<void>;
  };

  /**
   * @brief Concept for transportable types (read, write, open, close).
   */
  export template<typename T>
  concept Transportable = Readable<T> && Writable<T> && requires(T& t) {
    { t.open() }  -> std::same_as<void>;
    { t.close() } -> std::same_as<void>;
  };

  /**
   * @brief Concept for server types (start, accept, stop).
   */
  export template<typename S>
  concept Acceptable = requires(S& s) {
    { s.start()  } -> std::same_as<void>;
    { s.accept() } -> Transportable;
    { s.stop()   } -> std::same_as<void>;
  };
}
