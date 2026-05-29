module;

#ifndef _MSC_VER
#include <cstddef>
#include <concepts>
#include <span>
#include <expected>
#include <system_error>
#include <coroutine>
#endif

export module modern_io.concepts;

#ifdef _MSC_VER
import <cstddef>;
import <concepts>;
import <span>;
import <expected>;
import <system_error>;
import <coroutine>;
#endif

namespace modern::io
{

// Sync stream concepts.
export
template<typename S>
concept OutputStream = requires(S s, const char* ptr, std::size_t n,
                                std::span<std::byte> bspan,
                                std::span<char> cspan) {
    { s.write(ptr, n) } -> std::same_as<void>;
    { s.write(bspan) }  -> std::same_as<void>;
    { s.write(cspan) }  -> std::same_as<void>;
    { s.flush() }       -> std::same_as<void>;
};

export
template<typename S>
concept InputStream = requires(S s, char* ptr, std::size_t n,
                               std::span<std::byte> bspan,
                               std::span<char> cspan) {
    { s.read(ptr, n) }  -> std::convertible_to<std::size_t>;
    { s.read(bspan) }   -> std::convertible_to<std::size_t>;
    { s.read(cspan) }   -> std::convertible_to<std::size_t>;
    { s.eof() }         -> std::same_as<bool>;
};

// Shared awaiter helpers.
template<typename A, typename R>
concept AwaiterOf = requires(A a, std::coroutine_handle<> h) {
    { a.await_ready() }  -> std::convertible_to<bool>;
    { a.await_suspend(h) };
    { a.await_resume() } -> std::same_as<R>;
};

template<typename A, typename V>
concept AwaiterOfExpected = requires(A a, std::coroutine_handle<> h) {
    { a.await_ready() }  -> std::convertible_to<bool>;
    { a.await_suspend(h) };
    { a.await_resume() } -> std::same_as<std::expected<V, std::error_code>>;
};

template<typename A>
concept AwaiterOfExpectedVoid = AwaiterOf<A, std::expected<void, std::error_code>>;

// Async stream concepts.
export
template<typename S>
concept AsyncOutputStream = requires(S s, const char* ptr, std::size_t n,
                                     std::span<const std::byte> bspan,
                                     std::span<const char> cspan) {
    { s.write_async(ptr, n) }  -> AwaiterOfExpected<std::size_t>;
    { s.write_async(bspan) }   -> AwaiterOfExpected<std::size_t>;
    { s.write_async(cspan) }   -> AwaiterOfExpected<std::size_t>;
    { s.flush() }              -> std::same_as<std::expected<void, std::error_code>>;
};

export
template<typename S>
concept AsyncInputStream = requires(S s, char* ptr, std::size_t n,
                                    std::span<std::byte> bspan,
                                    std::span<char> cspan) {
    { s.read_async(ptr, n) } -> AwaiterOfExpected<std::size_t>;
    { s.read_async(bspan) }  -> AwaiterOfExpected<std::size_t>;
    { s.read_async(cspan) }  -> AwaiterOfExpected<std::size_t>;
    { s.eof() }              -> std::same_as<std::expected<bool, std::error_code>>;
};

// Optional async variants for flush and eof checks.
export
template<typename S>
concept AsyncFlushable = requires(S s) {
    { s.flush_async() } -> AwaiterOfExpectedVoid;
};

export
template<typename S>
concept AsyncEOFAware = requires(S s) {
    { s.eof_async() } -> AwaiterOfExpected<bool>;
};

} // namespace modern::io

export namespace modern_io {

template<typename S>
concept OutputStream = modern::io::OutputStream<S>;

template<typename S>
concept InputStream = modern::io::InputStream<S>;

template<typename S>
concept AsyncOutputStream = modern::io::AsyncOutputStream<S>;

template<typename S>
concept AsyncInputStream = modern::io::AsyncInputStream<S>;

template<typename S>
concept AsyncFlushable = modern::io::AsyncFlushable<S>;

template<typename S>
concept AsyncEOFAware = modern::io::AsyncEOFAware<S>;

} // namespace modern_io