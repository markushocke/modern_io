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
#include <stop_token>

export module net_io.async_utils;

import net_io_base;
import net_io.generic_awaiter;
import net_io.event_loop;
export import net_io.task;

export namespace modern::net {

    class IoWaitAwaiter {
    public:
        IoWaitAwaiter(
            EventReactor& reactor, int fd, IOEvent events,
            modern::scheduler completion_scheduler, std::stop_token token)
            : reactor_(&reactor), fd_(fd), events_(events),
              completion_scheduler_(std::move(completion_scheduler)), token_(token),
              state_(std::make_shared<IoOperationState>()) {}

        [[nodiscard]] bool await_ready() const noexcept { return false; }

        void await_suspend(std::coroutine_handle<> handle) {
            IoRegistration registration{
                fd_, events_, handle, state_, completion_scheduler_, token_, state_};
            (void)reactor_->register_io(registration);
        }

        [[nodiscard]] std::expected<void, std::error_code> await_resume() const noexcept {
            switch (state_->completion()) {
            case IoCompletion::ready:
                return {};
            case IoCompletion::cancelled:
                return std::unexpected(std::make_error_code(std::errc::operation_canceled));
            case IoCompletion::closed:
                return std::unexpected(std::make_error_code(std::errc::bad_file_descriptor));
            case IoCompletion::reactor_shutdown:
                return std::unexpected(std::make_error_code(std::errc::operation_canceled));
            case IoCompletion::pending:
                return std::unexpected(std::make_error_code(std::errc::state_not_recoverable));
            }
            return std::unexpected(std::make_error_code(std::errc::io_error));
        }

    private:
        EventReactor* reactor_;
        int fd_;
        IOEvent events_;
        modern::scheduler completion_scheduler_;
        std::stop_token token_;
        std::shared_ptr<IoOperationState> state_;
    };

    [[nodiscard]] inline IoWaitAwaiter wait_io(
        EventReactor& reactor, int fd, IOEvent events,
        modern::scheduler completion_scheduler, std::stop_token token = {}) {
        return {reactor, fd, events, std::move(completion_scheduler), token};
    }

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

    // Compatibility adapters for the runtime-facing EventReactor bridge. The
    // in-repo EventLoop is only one implementation of that bridge.
    template<typename StreamOrFd, typename Op>
    IoTask<std::expected<std::size_t, std::error_code>>
    read_some_async_on(EventReactor& loop, StreamOrFd&& s, Op op) {
        return read_some_async_generic(std::forward<StreamOrFd>(s), std::forward<Op>(op),
            [&loop](auto& fd, std::coroutine_handle<> h, std::shared_ptr<void> owner){
                loop.register_io(make_io_registration(fd, IOEvent::Read, h, std::move(owner)));
            }
        );
    }

    template<typename StreamOrFd, typename Op>
    IoTask<std::expected<std::size_t, std::error_code>>
    write_some_async_on(EventReactor& loop, StreamOrFd&& s, Op op) {
        return write_some_async_generic(std::forward<StreamOrFd>(s), std::forward<Op>(op),
            [&loop](auto& fd, std::coroutine_handle<> h, std::shared_ptr<void> owner){
                loop.register_io(make_io_registration(fd, IOEvent::Write, h, std::move(owner)));
            }
        );
    }

    // Convenience wrappers using the default legacy EventLoop directly.
    // Convenience wrappers using the default bridge reactor directly.
    template<typename StreamOrFd, typename Op>
    IoTask<std::expected<std::size_t, std::error_code>>
    read_some_async(StreamOrFd&& s, Op op) {
        return read_some_async_on(default_event_reactor(), std::forward<StreamOrFd>(s), std::forward<Op>(op));
    }

    template<typename StreamOrFd, typename Op>
    IoTask<std::expected<std::size_t, std::error_code>>
    write_some_async(StreamOrFd&& s, Op op) {
        return write_some_async_on(default_event_reactor(), std::forward<StreamOrFd>(s), std::forward<Op>(op));
    }

    // NOTE: High-level stream-style helpers `read_exact_async`/`write_all_async`
    // were moved into a dedicated AsyncStreamBase interface to avoid cycles
    // between low-level socket implementations and higher-level utils.
    // Keep only the low-level primitives here (make_io_task, read_some_async_generic,
    // write_some_async_generic and convenience wrappers).
}

export namespace net_io {

using modern::net::make_io_task;
using modern::net::read_some_async;
using modern::net::read_some_async_generic;
using modern::net::read_some_async_on;
using modern::net::run_registered_read;
using modern::net::run_registered_write;
using modern::net::set_task_metadata;
using modern::net::wrap_io_task_result;
using modern::net::wrap_io_task_void;
using modern::net::write_some_async;
using modern::net::write_some_async_generic;
using modern::net::write_some_async_on;
using modern::net::wait_io;

} // namespace net_io
