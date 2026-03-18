module;

#include <coroutine>
#include <expected>
#include <span>
#include <system_error>
#include <cstddef>
#include <memory>
#include <functional>
#include <sstream>
#include <future>
#include <type_traits>

export module net_io.async_utils;

import net_io_base;
import net_io.generic_awaiter;
import net_io.event_loop;
export import net_io.task;

export namespace net_io
{
    // IoTask is the compatibility name for the public low-level Task type.

    template<typename FD, typename InnerTask>
    void set_task_metadata(FD fd, bool write, InnerTask& inner) {
        if constexpr (std::is_integral_v<FD>) {
            if constexpr (requires { inner.promise().set_io_metadata(static_cast<int>(fd), write); }) {
                inner.promise().set_io_metadata(static_cast<int>(fd), write);
            }
        } else {
            if constexpr (requires { inner.promise().set_io_metadata(-1, write); }) {
                inner.promise().set_io_metadata(-1, write);
            }
        }
    }

    template<typename InnerTask>
    IoTask<void> wrap_io_task_void(InnerTask inner) {
        co_await inner;
    }

    template<typename ResultType, typename InnerTask>
    IoTask<ResultType> wrap_io_task_result(InnerTask inner) {
        co_return co_await inner;
    }

    // Generic IoTask wrapper for any async operation
    template<typename FD, typename Body>
    auto make_io_task(FD fd, bool write, Body&& body) {
        auto inner = std::invoke(std::forward<Body>(body));
        set_task_metadata(fd, write, inner);

        using InnerTask = decltype(inner);
        using ResultType = decltype(std::declval<InnerTask>().await_resume());

        if constexpr (std::is_same_v<ResultType, void>) {
            return wrap_io_task_void(std::move(inner));
        } else {
            return wrap_io_task_result<ResultType>(std::move(inner));
        }
    }

    template<typename Registrar, typename OpCallable>
    IoTask<std::expected<std::size_t, std::error_code>>
    run_registered_read(int fd, OpCallable op_callable, Registrar reg) {
        co_return co_await make_awaiter(fd, op_callable, reg, false);
    }

    template<typename Registrar, typename OpCallable>
    IoTask<std::expected<std::size_t, std::error_code>>
    run_registered_write(int fd, OpCallable op_callable, Registrar reg) {
        co_return co_await make_awaiter(fd, op_callable, reg, true);
    }

    // ---------------- Generic read_some_async / write_some_async ----------------

    template<typename StreamOrFd, typename Op, typename Registrar>
    IoTask<std::expected<std::size_t, std::error_code>>
    read_some_async_generic(StreamOrFd&& s, Op op, Registrar reg) {
        // Allow s to be either an object with native_handle() or a callable returning FD
        int fd = -1;
        if constexpr (std::is_invocable_v<StreamOrFd>) {
            if constexpr (std::is_convertible_v<std::invoke_result_t<StreamOrFd>, int>) {
                fd = static_cast<int>(std::invoke(std::forward<StreamOrFd>(s)));
            }
        } else if constexpr (requires(StreamOrFd& x){ x.native_handle(); }) {
            fd = static_cast<int>(std::forward<StreamOrFd>(s).native_handle());
        }
        auto op_callable = [op = std::forward<Op>(op)](auto& ctx) -> std::expected<std::size_t, std::error_code> {
            if constexpr (std::is_invocable_v<Op, decltype(ctx)&>)
                return std::invoke(op, ctx);
            else
                return std::unexpected(std::make_error_code(std::errc::invalid_argument));
        };

        return run_registered_read(fd, std::move(op_callable), std::move(reg));
    }

    template<typename StreamOrFd, typename Op, typename Registrar>
    IoTask<std::expected<std::size_t, std::error_code>>
    write_some_async_generic(StreamOrFd&& s, Op op, Registrar reg) {
        int fd = -1;
        if constexpr (std::is_invocable_v<StreamOrFd>) {
            if constexpr (std::is_convertible_v<std::invoke_result_t<StreamOrFd>, int>) {
                fd = static_cast<int>(std::invoke(std::forward<StreamOrFd>(s)));
            }
        } else if constexpr (requires(StreamOrFd& x){ x.native_handle(); }) {
            fd = static_cast<int>(std::forward<StreamOrFd>(s).native_handle());
        }
        auto op_callable = [op = std::forward<Op>(op)](auto& ctx) -> std::expected<std::size_t, std::error_code> {
            if constexpr (std::is_invocable_v<Op, decltype(ctx)&>)
                return std::invoke(op, ctx);
            else
                return std::unexpected(std::make_error_code(std::errc::invalid_argument));
        };

        return run_registered_write(fd, std::move(op_callable), std::move(reg));
    }

    // Convenience wrappers using EventLoop directly
    template<typename StreamOrFd, typename Op>
    IoTask<std::expected<std::size_t, std::error_code>>
    read_some_async(StreamOrFd&& s, Op op) {
        return read_some_async_generic(std::forward<StreamOrFd>(s), std::forward<Op>(op),
            [](auto& fd, std::coroutine_handle<> h, std::shared_ptr<void> owner){
                EventLoop::instance().register_read(fd, h, owner);
            }
        );
    }

    template<typename StreamOrFd, typename Op>
    IoTask<std::expected<std::size_t, std::error_code>>
    write_some_async(StreamOrFd&& s, Op op) {
        return write_some_async_generic(std::forward<StreamOrFd>(s), std::forward<Op>(op),
            [](auto& fd, std::coroutine_handle<> h, std::shared_ptr<void> owner){
                EventLoop::instance().register_write(fd, h, owner);
            }
        );
    }

    // NOTE: High-level stream-style helpers `read_exact_async`/`write_all_async`
    // were moved into a dedicated AsyncStreamBase interface to avoid cycles
    // between low-level socket implementations and higher-level utils.
    // Keep only the low-level primitives here (make_io_task, read_some_async_generic,
    // write_some_async_generic and convenience wrappers).
}