module;

#ifndef _MSC_VER
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>
#include <span>
#include <expected>
#include <system_error>
#include <coroutine>
#include <atomic>
#include <stop_token>
#endif

export module net_io.async_concepts;

export import modern.exec;

#ifdef _MSC_VER
import <concepts>;
import <cstddef>;
import <cstdint>;
import <memory>;
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

export namespace modern::net
{
  enum class IOEvent : std::uint8_t {
      None  = 0,
      Read  = 1u << 0,
      Write = 1u << 1,
      Error = 1u << 2,
      Hangup = 1u << 3
  };

  [[nodiscard]] constexpr IOEvent operator|(IOEvent lhs, IOEvent rhs) noexcept {
      return static_cast<IOEvent>(
          static_cast<std::uint8_t>(lhs) | static_cast<std::uint8_t>(rhs));
  }

  [[nodiscard]] constexpr IOEvent operator&(IOEvent lhs, IOEvent rhs) noexcept {
      return static_cast<IOEvent>(
          static_cast<std::uint8_t>(lhs) & static_cast<std::uint8_t>(rhs));
  }

  [[nodiscard]] constexpr bool has_event(IOEvent events, IOEvent event) noexcept {
      return (static_cast<std::uint8_t>(events) & static_cast<std::uint8_t>(event)) != 0;
  }

#ifdef _WIN32
  using NativeIoHandleType = void*;
#else
  using NativeIoHandleType = int;
#endif

  struct IoRegistrationToken {
      std::uint64_t value{};
      [[nodiscard]] explicit constexpr operator bool() const noexcept { return value != 0; }
      friend constexpr bool operator==(IoRegistrationToken, IoRegistrationToken) noexcept = default;
  };

  enum class IoCompletion : std::uint8_t {
      pending,
      ready,
      cancelled,
      closed,
      reactor_shutdown
  };

  class IoOperationState {
  public:
      [[nodiscard]] bool complete(IoCompletion desired) noexcept {
          auto expected = IoCompletion::pending;
          return completion_.compare_exchange_strong(
              expected, desired, std::memory_order_acq_rel, std::memory_order_acquire);
      }
      [[nodiscard]] IoCompletion completion() const noexcept {
          return completion_.load(std::memory_order_acquire);
      }
  private:
      std::atomic<IoCompletion> completion_{IoCompletion::pending};
  };

  struct IoRegistration {
      NativeIoHandleType handle{};
      IOEvent events{IOEvent::None};
      std::coroutine_handle<> resume_handle{};
      std::shared_ptr<void> owner{};
      modern::scheduler completion_scheduler{};
      std::stop_token stop_token{};
      std::shared_ptr<IoOperationState> operation_state{};
  };

    class EventReactor {
    public:
      virtual ~EventReactor() = default;
      [[nodiscard]] virtual IoRegistrationToken register_io(const IoRegistration& registration) = 0;
      virtual void deregister(IoRegistrationToken token) = 0;
      virtual void deregister(int fd) = 0;
      [[nodiscard]] virtual bool is_debug_enabled() const noexcept { return false; }
    };

    template<typename T>
    concept NativeIoHandle = requires(T& t) {
      { t.native_handle() } -> std::convertible_to<NativeIoHandleType>;
    };

    template<typename Handle>
    requires std::convertible_to<Handle, NativeIoHandleType>
    [[nodiscard]] inline IoRegistration make_io_registration(
      Handle handle,
      IOEvent events,
      std::coroutine_handle<> resume_handle,
      std::shared_ptr<void> owner = {}) {
      return IoRegistration{
        static_cast<NativeIoHandleType>(handle),
        events,
        resume_handle,
        std::move(owner),
        {},
        {},
        {}
      };
    }

  template<NativeIoHandle T>
    [[nodiscard]] inline IoRegistration make_io_registration(
      T& io_device,
      IOEvent events,
      std::coroutine_handle<> resume_handle,
      std::shared_ptr<void> owner = {}) {
      return make_io_registration(io_device.native_handle(), events, resume_handle, std::move(owner));
    }

    template<typename T>
  concept IoRegistrationLike = requires(T r) {
      { r.handle } -> std::convertible_to<NativeIoHandleType>;
      { r.events } -> std::convertible_to<IOEvent>;
      { r.resume_handle } -> std::same_as<std::coroutine_handle<>&>;
      { r.owner } -> std::same_as<std::shared_ptr<void>&>;
  };

  template<typename T>
  concept EventReactorLike = requires(T& reactor, const IoRegistration& registration, int fd) {
      { reactor.register_io(registration) } -> std::same_as<IoRegistrationToken>;
      reactor.deregister(IoRegistrationToken{});
      { reactor.deregister(fd) } -> std::same_as<void>;
  };
} // namespace modern::net

export namespace net_io {

using NativeIoHandleType = modern::net::NativeIoHandleType;
using modern::net::EventReactor;
using modern::net::has_event;
using modern::net::IOEvent;
using modern::net::IoRegistration;
using modern::net::IoRegistrationToken;
using modern::net::IoCompletion;
using modern::net::IoOperationState;
using modern::net::make_io_registration;

template<typename T>
concept NativeIoHandle = modern::net::NativeIoHandle<T>;

template<typename T>
concept IoRegistrationLike = modern::net::IoRegistrationLike<T>;

template<typename T>
concept EventReactorLike = modern::net::EventReactorLike<T>;

} // namespace net_io
