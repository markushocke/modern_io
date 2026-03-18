module;

#ifndef _MSC_VER
#include <concepts>
#include <cstddef>
#include <utility>
#include <span>
#include <expected>
#include <system_error>
#include <coroutine>
#endif

export module net_io.async_concepts;

#ifdef _MSC_VER
import <concepts>;
import <cstddef>;
import <utility>;
import <span>;
import <expected>;
import <system_error>;
import <coroutine>;
#endif

namespace net_io_concepts
{
    // Base awaiter with a result type
  template<typename A, typename R>
  concept AwaiterOf = requires(A a, std::coroutine_handle<> h) {
      { a.await_ready() } -> std::convertible_to<bool>;
      { a.await_suspend(h) };
      { a.await_resume() } -> std::same_as<R>;
  };

    // Awaiter for std::expected<void, std::error_code>
  template<typename A>
  concept AwaiterOfExpectedVoid = requires(A a, std::coroutine_handle<> h) {
      { a.await_ready() } -> std::convertible_to<bool>;
      { a.await_suspend(h) };
      { a.await_resume() } -> std::same_as<std::expected<void,std::error_code>>;
  };

    // Generic expected<V, std::error_code>
  template<typename A, typename V>
  concept AwaiterOfExpected = requires(A a, std::coroutine_handle<> h) {
      { a.await_ready() } -> std::convertible_to<bool>;
      { a.await_suspend(h) };
      { a.await_resume() } -> std::same_as<std::expected<V,std::error_code>>;
  };

    // Async read/write (use decltype(expr) as the awaiter type)
  export template<typename T>
  concept AsyncReadable = requires(T& t, char* buf, std::size_t n, std::span<char> s) {
      { t.read_async(buf, n) } -> std::same_as<decltype(t.read_async(buf, n))>; // syntaktisch ok
      requires AwaiterOfExpected<decltype(t.read_async(buf, n)), std::size_t>;
      { t.read_async(s) }     -> std::same_as<decltype(t.read_async(s))>;
      requires AwaiterOfExpected<decltype(t.read_async(s)), std::size_t>;
  };

  export template<typename T>
  concept AsyncWritable = requires(T& t, const char* buf, std::size_t n, std::span<const char> s) {
      { t.write_async(buf, n) } -> std::same_as<decltype(t.write_async(buf, n))>;
      requires AwaiterOfExpected<decltype(t.write_async(buf, n)), std::size_t>;
      { t.write_async(s) }      -> std::same_as<decltype(t.write_async(s))>;
      requires AwaiterOfExpected<decltype(t.write_async(s)), std::size_t>;
  };

        // Combination concepts
  export template<typename T>
  concept AsyncTransportable =
      AsyncReadable<T> && AsyncWritable<T> &&
      requires(T& t) {
        { t.open() }  -> std::same_as<void>;
        { t.close() } -> std::same_as<void>;
      };
    // Awaiter whose await_resume() returns an std::expected<V,std::error_code> where V is AsyncTransportable
  template<typename A>
  concept AwaiterOfExpectedTransport = requires(A a, std::coroutine_handle<> h) {
      { a.await_ready() } -> std::convertible_to<bool>;
      { a.await_suspend(h) };
      { a.await_resume() } -> std::same_as<std::expected<typename decltype(a.await_resume())::value_type, std::error_code>>;
      requires AsyncTransportable<typename decltype(a.await_resume())::value_type>;
  };

    // Optional synchronous additions (for hybrid adapters)
  template<typename T>
  concept HasSyncFlush = requires(T& t) {
      { t.flush() } -> std::same_as<std::expected<void,std::error_code>>;
  };
  template<typename T>
  concept HasSyncEof = requires(T& t) {
      { t.eof() } -> std::same_as<std::expected<bool,std::error_code>>;
  };

    // Async lifecycle
  export template<typename T>
  concept AsyncConnectable = requires(T& t) {
      { t.connect_async() } -> std::same_as<decltype(t.connect_async())>;
      requires AwaiterOfExpectedVoid<decltype(t.connect_async())>;
  };
  export template<typename T>
  concept AsyncClosable = requires(T& t) {
      { t.close_async() } -> std::same_as<decltype(t.close_async())>;
      requires AwaiterOfExpectedVoid<decltype(t.close_async())>;
  };
  export template<typename T, typename Endpoint>
  concept AsyncConnectableTo = requires(T& t, const Endpoint& ep) {
      { t.connect_async(ep) } -> std::same_as<decltype(t.connect_async(ep))>;
      requires AwaiterOfExpectedVoid<decltype(t.connect_async(ep))>;
  };

    // Flush/EOF asynchronous
  export template<typename T>
  concept AsyncFlushable = requires(T& t) {
      { t.flush_async() } -> std::same_as<decltype(t.flush_async())>;
      requires AwaiterOfExpectedVoid<decltype(t.flush_async())>;
  };
  export template<typename T>
  concept AsyncEOFAware = requires(T& t) {
      { t.eof_async() } -> std::same_as<decltype(t.eof_async())>;
      requires AwaiterOfExpected<decltype(t.eof_async()), bool>;
  };

  export template<typename T>
  concept PureAsyncTransport =
      AsyncReadable<T> && AsyncWritable<T> &&
      AsyncConnectable<T> && AsyncClosable<T>;

  export template<typename T>
  concept AsyncTransportWithFlush =
      AsyncTransportable<T> &&
      (AsyncFlushable<T> || HasSyncFlush<T>);

  export template<typename T>
  concept AsyncFullDuplex =
      AsyncReadable<T> &&
      AsyncWritable<T> &&
      (AsyncConnectable<T> || true) &&
      (AsyncClosable<T>   || true) &&
      (AsyncFlushable<T>  || HasSyncFlush<T> || true) &&
      (AsyncEOFAware<T>   || HasSyncEof<T>   || true);

  // Server (async accept)
  export template<typename S>
  concept AsyncAcceptable = requires(S& s) {
      { s.start() } -> std::same_as<void>;
      { s.stop() } -> std::same_as<void>;
  };
}