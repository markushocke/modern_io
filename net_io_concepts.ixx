module;

#ifndef _MSC_VER
#include <concepts>
#include <cstddef>
#include <utility>
#endif

export module net_io_concepts;

#ifdef _MSC_VER
import <concepts>;
import <cstddef>;
import <utility>;
#endif

namespace net_io_concepts
{
  /**
   * @brief Concept for readable types.
   *
   * A type satisfies this concept if it provides a member function
   * with the following signature:
   *   std::size_t read(char* buf, std::size_t n);
   *
   * This is used to constrain generic code to only accept types that
   * can read data into a buffer.
   *
   * Example:
   * @code
   * struct MyReader
   * {
   *     std::size_t read(char* buf, std::size_t n) { ... }
   * };
   * static_assert(Readable<MyReader>);
   * @endcode
   */
  export template<typename T>
  concept Readable = requires(T& t, char* buf, std::size_t n)
  {
    { t.read(buf, n) } -> std::convertible_to<std::size_t>;
  };

  /**
   * @brief Concept for writable types.
   *
   * A type satisfies this concept if it provides a member function
   * with the following signature:
   *   void write(const char* buf, std::size_t n);
   *
   * This is used to constrain generic code to only accept types that
   * can write data from a buffer.
   *
   * Example:
   * @code
   * struct MyWriter
   * {
   *     void write(const char* buf, std::size_t n) { ... }
   * };
   * static_assert(Writable<MyWriter>);
   * @endcode
   */
  export template<typename T>
  concept Writable = requires(T& t, const char* buf, std::size_t n)
  {
    { t.write(buf, n) } -> std::same_as<void>;
  };

  /**
   * @brief Concept for transportable types.
   *
   * A type satisfies this concept if it is both Readable and Writable,
   * and provides the following member functions:
   *   void open();
   *   void close();
   *
   * This is typically used for types representing network connections
   * or file handles that can be opened and closed.
   *
   * Example:
   * @code
   * struct MyTransport
   * {
   *     std::size_t read(char*, std::size_t);
   *     void write(const char*, std::size_t);
   *     void open();
   *     void close();
   * };
   * static_assert(Transportable<MyTransport>);
   * @endcode
   */
  export template<typename T>
  concept Transportable = Readable<T> && Writable<T> && requires(T& t)
  {
    { t.open() }  -> std::same_as<void>;
    { t.close() } -> std::same_as<void>;
  };

  /**
   * @brief Concept for server types that can accept connections.
   *
   * A type satisfies this concept if it provides the following member functions:
   *   void start();
   *   auto accept(); // must return a Transportable type
   *   void stop();
   *
   * This is typically used for TCP or UDP server classes.
   *
   * Example:
   * @code
   * struct MyServer
   * {
   *     void start();
   *     MyTransport accept();
   *     void stop();
   * };
   * static_assert(Acceptable<MyServer>);
   * @endcode
   */
  export template<typename S>
  concept Acceptable = requires(S& s)
  {
    { s.start()  } -> std::same_as<void>;
    { s.accept() } -> Transportable;
    { s.stop()   } -> std::same_as<void>;
  };
}
