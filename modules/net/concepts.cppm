module;

#ifndef _MSC_VER
#include <concepts>
#include <cstddef>
#include <utility>
#include <span>
#endif

export module net_io_concepts;

#ifdef _MSC_VER
import <concepts>;
import <cstddef>;
import <utility>;
import <span>;
#endif

namespace net_io_concepts
{
  // Sync Readable
  export template<typename T>
  concept Readable = requires(T& t, char* buf, std::size_t n)
  {
    { t.read(buf, n) } -> std::convertible_to<std::size_t>;
  };

  // Sync Writable
  export template<typename T>
  concept Writable = requires(T& t, const char* buf, std::size_t n)
  {
    { t.write(buf, n) } -> std::same_as<void>;
  };

  // Sync Transportable
  export template<typename T>
  concept Transportable = Readable<T> && Writable<T> && requires(T& t)
  {
    { t.open() }  -> std::same_as<void>;
    { t.close() } -> std::same_as<void>;
  };

  // Sync Acceptable (Server)
  export template<typename S>
  concept Acceptable = requires(S& s)
  {
    { s.start()  } -> std::same_as<void>;
    { s.accept() } -> Transportable;
    { s.stop()   } -> std::same_as<void>;
  };
}
